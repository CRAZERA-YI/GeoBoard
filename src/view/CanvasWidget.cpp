/**
 * @file    view/CanvasWidget.cpp
 * @brief   作图画布的实现。
 * @details 绘制策略：**所有几何都在屏幕坐标系下绘制**。
 *          场景坐标先经 Camera 换算成屏幕坐标，然后：
 *          - 线宽、点半径、字体都用固定像素值 → 缩放时线宽不会跟着变粗；
 *          - 无界直线先按可见矩形裁剪再画 → 不会出现坐标溢出；
 *          - 圆弧按屏幕上的弧长自适应采样 → 放大后不会变成折线。
 */

#include "CanvasWidget.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

/** @brief 点标记的半径（像素）。 */
const double kPointRadius = 4.0;

/** @brief 选中点的外圈高亮半径增量（像素）。 */
const double kPointHighlightGrow = 3.0;

/** @brief 命中测试的屏幕容差（像素）。 */
const double kHitTolerancePixels = 8.0;

/** @brief 直线/射线裁剪时的延伸倍率：把可见矩形对角线放大后作为参数上限。 */
const double kClipExtensionFactor = 4.0;

/** @brief 圆弧采样时每段的目标像素长度。 */
const double kArcSegmentPixels = 6.0;

/** @brief 圆弧采样的最大段数（防止极限缩放下绘制量爆炸）。 */
const int kArcMaxSegments = 256;

/** @brief 滚轮一格对应的缩放倍数。 */
const double kWheelZoomFactor = 1.15;

// 注意：isDragConstructionTool 已收敛到 controller/ToolId.h 里唯一定义，
// 这里直接用那个版本，不再本地复制一份（复制过，然后分叉出真实缺陷）。

/**
 * @brief 把一个场景点换算为屏幕点。
 * @param camera 相机。
 * @param scenePos 场景坐标。
 * @return 屏幕坐标。
 */
QPointF toScreenPoint(const Camera &camera, const Vec2 &scenePos)
{
    return camera.toScreen(scenePos);
}

/**
 * @brief 把线段裁剪到屏幕矩形内（Liang-Barsky 算法）。
 * @param a 线段起点（屏幕坐标）。
 * @param b 线段终点（屏幕坐标）。
 * @param rect 裁剪矩形。
 * @param clippedA 输出参数：裁剪后的起点。
 * @param clippedB 输出参数：裁剪后的终点。
 * @return 线段与矩形有交集时返回 true。
 * @note  无界直线必须经此裁剪才能绘制：直接把方向向量乘一个大数会溢出，
 *        也会在极端缩放下画出跨越屏幕的巨大坐标。
 */
bool clipSegmentToRect(const QPointF &a,
                       const QPointF &b,
                       const QRectF &rect,
                       QPointF *clippedA,
                       QPointF *clippedB)
{
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();

    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {a.x() - rect.left(), rect.right() - a.x(),
                         a.y() - rect.top(), rect.bottom() - a.y()};

    for (int i = 0; i < 4; ++i) {
        if (std::fabs(p[i]) < 1e-12) {
            if (q[i] < 0.0) {
                return false; // 平行于该边界且在外侧
            }
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0) {
            if (r > t1) {
                return false;
            }
            if (r > t0) {
                t0 = r;
            }
        } else {
            if (r < t0) {
                return false;
            }
            if (r < t1) {
                t1 = r;
            }
        }
    }

    *clippedA = QPointF(a.x() + t0 * dx, a.y() + t0 * dy);
    *clippedB = QPointF(a.x() + t1 * dx, a.y() + t1 * dy);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 构造与绑定
// ---------------------------------------------------------------------------

CanvasWidget::CanvasWidget(QWidget *parent)
    : QWidget(parent)
    , m_document(nullptr)
    , m_camera()
    , m_activeTool(ToolId::Select)
    , m_previewHint()
    , m_hasPreviewHint(false)
    , m_previewAsCircle(false)
    , m_hoveredId(kInvalidShapeId)
    , m_highlightId(kInvalidShapeId)
    , m_dragCandidateId(kInvalidShapeId)
    , m_isDragging(false)
    , m_lastDragScreenPos()
    , m_lastHoverScenePos()
    , m_constructPressScenePos()
    , m_isPanning(false)
    , m_panButton(Qt::NoButton)
    , m_lastPanScreenPos()
    , m_rightPressScreenPos()
    , m_rightDragMoved(false)
    , m_constructionStartId(kInvalidShapeId)
    , m_constructionArmed(false)
    , m_constructionStartScenePos()
    , m_isConstructionDragging(false)
    , m_gridVisible(true)
    , m_axesVisible(true)
    , m_labelsVisible(true)
    , m_snapEnabled(true)
    , m_hasArcPreview(false)
    , m_arcPreviewCenter()
    , m_arcPreviewRim()
    , m_arcPreviewArcPoint()
{
    setFocusPolicy(Qt::StrongFocus); // 必须能接收键盘事件（Delete / Esc）
    setMouseTracking(true);          // 悬停高亮需要不按键也能收到移动事件
    setAutoFillBackground(false);    // 背景在 paintEvent 里用调色板填充
    setMinimumSize(240, 180);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    // 先给出一个视口尺寸：resizeEvent 到来之前相机也不会算出错误的场景坐标
    m_camera.setViewportSize(width(), height());
}

CanvasWidget::~CanvasWidget() = default;

void CanvasWidget::setDocument(GeoDocument *document)
{
    if (m_document == document) {
        return;
    }

    if (m_document != nullptr) {
        disconnect(m_document, nullptr, this, nullptr);
    }

    m_document = document;
    m_hoveredId = kInvalidShapeId;
    m_dragCandidateId = kInvalidShapeId;
    m_isDragging = false;

    if (m_document != nullptr) {
        // 文档变动就重绘：画布不关心"变了什么"，只关心"要重画"
        const bool connected = connect(m_document, &GeoDocument::documentChanged,
                                      this, QOverload<>::of(&QWidget::update));
        Q_ASSERT(connected);
        Q_UNUSED(connected);
    }

    update();
}

void CanvasWidget::setActiveTool(ToolId tool)
{
    m_activeTool = tool;
    // 切换工具时清掉上一件工具留下的作图状态与预览，避免"拖到一半换工具"造成的错位
    m_constructionStartId = kInvalidShapeId;
    m_constructionArmed = false;
    m_isConstructionDragging = false;
    if (tool != ToolId::Select) {
        clearPreview();
    }
    update();
}

void CanvasWidget::setPreview(const QList<QPointF> &points, const QPointF &hint, bool showCircle)
{
    m_previewPoints = points;
    m_previewHint = hint;
    m_hasPreviewHint = true;
    m_previewAsCircle = showCircle;
    update();
}

void CanvasWidget::clearPreview()
{
    if (m_previewPoints.isEmpty() && !m_hasPreviewHint) {
        return;
    }
    m_previewPoints.clear();
    m_hasPreviewHint = false;
    m_previewAsCircle = false;
    update();
}

void CanvasWidget::setGridVisible(bool visible)
{
    if (m_gridVisible == visible) {
        return;
    }
    m_gridVisible = visible;
    update();
}

void CanvasWidget::setAxesVisible(bool visible)
{
    if (m_axesVisible == visible) {
        return;
    }
    m_axesVisible = visible;
    update();
}

void CanvasWidget::setLabelsVisible(bool visible)
{
    if (m_labelsVisible == visible) {
        return;
    }
    m_labelsVisible = visible;
    update();
}

void CanvasWidget::setSnapEnabled(bool enabled)
{
    if (m_snapEnabled == enabled) {
        return;
    }
    m_snapEnabled = enabled;
    update();
}

CanvasWidget::SnapResult CanvasWidget::snapScenePos(const QPointF &scenePos) const
{
    SnapResult result;
    result.position = scenePos;
    if (!m_snapEnabled || m_document == nullptr) {
        return result;
    }

    const Vec2 probe(scenePos);

    // ① 已有点：最优先。复用已有点是作图里最高频的意图，而且它同时保证
    //    "点在点上"的连通性（否则会有两个几乎重合却互不相关的点）。
    const ShapeId pointId = m_document->hitTestPoint(probe, hitTolerance());
    if (pointId != kInvalidShapeId) {
        const Shape *shape = m_document->shape(pointId);
        if (shape != nullptr && shape->type() == ShapeType::Point) {
            result.position = static_cast<const PointShape *>(shape)->position().toPointF();
            result.snapped = true;
            return result;
        }
    }

    // ② 栅格：只在鼠标已经"贴到格点附近"时才吸附，否则自由作图会变得处处被拽走。
    //
    //    两条纪律（各踩过一次）：
    //    · 判定必须**逐轴**做（|dx| 与 |dy| 各自不超过阈值）。先前用欧氏距离判过一次，
    //      格子正中央的点也会因为"到某个角点的斜向距离刚好压线"而被吸走。
    //    · 阈值必须**小于半个分辨率**。曾经写成"每轴 ≤ 半格"，结果整个格子都被覆盖，
    //      不存在任何自由区域——那就等于取消了自由作图，用户没有"不吸附"的办法。
    //
    //    分辨率取 step/2 而不是 step：相机缩小时会把网格步长抬到 2、5、10 这样的整数倍，
    //    若直接吸到 step 的整数倍，用户就只能得到 0、2、4……而**点不到 1 和 3**——
    //    栅格作图最常用的恰恰是整数坐标。
    const double step = m_camera.gridStep();
    if (step > 0.0) {
        const double resolution = step * 0.5;
        const double snapRadius = resolution * 0.25;
        const double snappedX = std::floor(scenePos.x() / resolution + 0.5) * resolution;
        const double snappedY = std::floor(scenePos.y() / resolution + 0.5) * resolution;
        if (std::fabs(snappedX - scenePos.x()) <= snapRadius
            && std::fabs(snappedY - scenePos.y()) <= snapRadius) {
            result.position = QPointF(snappedX, snappedY);
            result.snapped = true;
            return result;
        }
    }

    // ③ 曲线：把点吸到曲线上，并**带上曲线 id**，让控制器创建附着点——
    //    这样"点在线上"是结构性约束，拖动时不会掉下来。
    const ShapeId curveId = m_document->hitTestCurve(probe, hitTolerance());
    if (curveId != kInvalidShapeId) {
        const Shape *curve = m_document->shape(curveId);
        switch (curve->type()) {
        case ShapeType::Line:
        case ShapeType::PerpendicularLine:
        case ShapeType::ParallelLine:
        case ShapeType::AngleBisector: {
            const LineData &line = static_cast<const LineShape *>(curve)->lineData();
            if (line.isDefined) {
                const double t = line.clampParameter(line.parameterAt(probe));
                result.position = (line.origin + line.direction * t).toPointF();
                result.snapped = true;
                result.hostCurveId = curveId;
            }
            break;
        }
        case ShapeType::Circle: {
            const CircleData &circle = static_cast<const CircleShape *>(curve)->circleData();
            if (circle.isDefined) {
                result.position = GeoMath::projectPointOnCircle(probe, circle.center,
                                                                circle.radius).toPointF();
                result.snapped = true;
                result.hostCurveId = curveId;
            }
            break;
        }
        case ShapeType::Arc: {
            const ArcData &arc = static_cast<const ArcShape *>(curve)->arcData();
            if (arc.isDefined) {
                Vec2 projected = GeoMath::projectPointOnCircle(probe, arc.center, arc.radius);
                const double angle = GeoMath::angleOf(projected - arc.center);
                if (!GeoMath::isAngleBetween(angle, arc.startAngle, arc.endAngle)) {
                    // 投影落在缺口一侧：吸到较近的端点，保证点仍在弧上
                    const Vec2 startPoint =
                        arc.center + Vec2(std::cos(arc.startAngle), std::sin(arc.startAngle)) * arc.radius;
                    const Vec2 endPoint =
                        arc.center + Vec2(std::cos(arc.endAngle), std::sin(arc.endAngle)) * arc.radius;
                    projected = (probe.distanceTo(startPoint) <= probe.distanceTo(endPoint))
                                    ? startPoint
                                    : endPoint;
                }
                result.position = projected.toPointF();
                result.snapped = true;
                result.hostCurveId = curveId;
            }
            break;
        }
        case ShapeType::Polygon: {
            const QList<Vec2> &vertices = static_cast<const PolygonShape *>(curve)->vertices();
            const int count = vertices.size();
            if (count >= 3) {
                Vec2 best;
                double bestDistance = -1.0;
                for (int i = 0; i < count; ++i) {
                    const Vec2 candidate = GeoMath::projectPointOnSegment(
                        probe, vertices.at(i), vertices.at((i + 1) % count));
                    const double distance = probe.distanceTo(candidate);
                    if (bestDistance < 0.0 || distance < bestDistance) {
                        bestDistance = distance;
                        best = candidate;
                    }
                }
                if (bestDistance >= 0.0) {
                    result.position = best.toPointF();
                    result.snapped = true;
                    result.hostCurveId = curveId;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    return result;
}

void CanvasWidget::setArcPreview(const QPointF &center, const QPointF &rim, const QPointF &arcPoint)
{
    m_hasArcPreview = true;
    m_arcPreviewCenter = center;
    m_arcPreviewRim = rim;
    m_arcPreviewArcPoint = arcPoint;
    update();
}

void CanvasWidget::clearArcPreview()
{
    if (!m_hasArcPreview) {
        return;
    }
    m_hasArcPreview = false;
    update();
}

void CanvasWidget::setHighlight(ShapeId id)
{
    if (m_highlightId == id) {
        return;
    }
    m_highlightId = id;
    update();
}

bool CanvasWidget::fitContent()
{
    if (m_document == nullptr) {
        return false;
    }
    Vec2 minCorner;
    Vec2 maxCorner;
    if (!m_document->contentBounds(&minCorner, &maxCorner)) {
        return false;
    }
    const bool fitted = m_camera.fitToBounds(minCorner, maxCorner, 0.12);
    if (fitted) {
        update();
    }
    return fitted;
}

bool CanvasWidget::contentBounds(Vec2 *minCorner, Vec2 *maxCorner) const
{
    if (m_document == nullptr) {
        return false;
    }
    return m_document->contentBounds(minCorner, maxCorner);
}

bool CanvasWidget::renderToDevice(QPaintDevice *device, const QSize &targetSize, bool drawBackground)
{
    if (device == nullptr || !targetSize.isValid()) {
        return false;
    }

    // 导出时临时把相机视口切到目标尺寸：于是"适配内容""网格步长""线宽像素"等
    // 全部按导出尺寸重新计算，得到一张自洽的图，而不是把屏幕那幅拉伸放大。
    const int savedWidth = m_camera.viewportWidth();
    const int savedHeight = m_camera.viewportHeight();
    const Vec2 savedCenter = m_camera.sceneCenter();
    const double savedZoom = m_camera.zoom();

    m_camera.setViewportSize(targetSize.width(), targetSize.height());

    // 按内容重新框一次，避免导出到一张只有空白（或图形被切掉）的图
    Vec2 minCorner;
    Vec2 maxCorner;
    if (m_document != nullptr && m_document->contentBounds(&minCorner, &maxCorner)) {
        m_camera.fitToBounds(minCorner, maxCorner, 0.08);
    }

    // 编辑期的临时状态不属于作品内容：导出时一律不画
    const ShapeId savedHover = m_hoveredId;
    const ShapeId savedHighlight = m_highlightId;
    const QList<QPointF> savedPreviewPoints = m_previewPoints;
    const bool savedHasArcPreview = m_hasArcPreview;
    m_hoveredId = kInvalidShapeId;
    m_highlightId = kInvalidShapeId;
    m_previewPoints.clear();
    m_hasArcPreview = false;

    {
        QPainter painter(device);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        if (drawBackground) {
            painter.fillRect(QRect(QPoint(0, 0), targetSize), palette().color(QPalette::Base));
        }

        // 与 paintEvent 完全相同的绘制顺序：网格 → 坐标轴 → 刻度 → 图元
        if (m_gridVisible) {
            drawGrid(&painter);
        }
        if (m_axesVisible) {
            drawAxes(&painter);
        }
        if (m_labelsVisible && (m_gridVisible || m_axesVisible)) {
            drawGridLabels(&painter);
        }
        drawShapes(&painter);
    }

    m_hoveredId = savedHover;
    m_highlightId = savedHighlight;
    m_previewPoints = savedPreviewPoints;
    m_hasArcPreview = savedHasArcPreview;

    // 恢复相机：导出不应改变用户当前的视图
    m_camera.setViewportSize(savedWidth, savedHeight);
    m_camera.setZoom(savedZoom);
    m_camera.centerOn(savedCenter);
    return true;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

void CanvasWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);

    // 抗锯齿：圆、圆弧与派生曲线在低缩放下会有明显锯齿（用户反馈），
    // 开启后边缘按背景色混合，圆的轮廓才平滑。代价是大图形重绘略慢，
    // 但实测 1000 图元量级下仍在亚毫秒级，可接受。
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.fillRect(rect(), palette().color(QPalette::Base));

    if (m_gridVisible) {
        drawGrid(&painter);
    }
    if (m_axesVisible) {
        drawAxes(&painter);
    }
    if (m_labelsVisible && (m_gridVisible || m_axesVisible)) {
        drawGridLabels(&painter);
    }

    drawShapes(&painter);
    drawPreview(&painter);
}

void CanvasWidget::drawGrid(QPainter *painter)
{
    const double step = m_camera.gridStep();
    if (step <= 0.0) {
        return;
    }

    Vec2 minCorner;
    Vec2 maxCorner;
    m_camera.visibleSceneRect(&minCorner, &maxCorner);

    QColor gridColor = palette().color(QPalette::Mid);
    gridColor.setAlpha(90);
    painter->setPen(QPen(gridColor, 1.0));

    // 用整数索引遍历，避免浮点累加误差让网格线缓慢漂移
    const long firstX = static_cast<long>(std::floor(minCorner.x / step));
    const long lastX = static_cast<long>(std::ceil(maxCorner.x / step));
    const long firstY = static_cast<long>(std::floor(minCorner.y / step));
    const long lastY = static_cast<long>(std::ceil(maxCorner.y / step));

    // 极端缩放下格数可能爆炸，这里设一个上限（正常视口远达不到）
    const long maxLines = 4000;
    if ((lastX - firstX) > maxLines || (lastY - firstY) > maxLines) {
        return;
    }

    for (long i = firstX; i <= lastX; ++i) {
        const double x = i * step;
        const QPointF top = toScreenPoint(m_camera, Vec2(x, maxCorner.y));
        const QPointF bottom = toScreenPoint(m_camera, Vec2(x, minCorner.y));
        painter->drawLine(top, bottom);
    }
    for (long j = firstY; j <= lastY; ++j) {
        const double y = j * step;
        const QPointF left = toScreenPoint(m_camera, Vec2(minCorner.x, y));
        const QPointF right = toScreenPoint(m_camera, Vec2(maxCorner.x, y));
        painter->drawLine(left, right);
    }
}

void CanvasWidget::drawAxes(QPainter *painter)
{
    Vec2 minCorner;
    Vec2 maxCorner;
    m_camera.visibleSceneRect(&minCorner, &maxCorner);

    const QColor axisColor = palette().color(QPalette::WindowText);
    painter->setPen(QPen(axisColor, 1.4));

    if (minCorner.y <= 0.0 && maxCorner.y >= 0.0) {
        const QPointF left = toScreenPoint(m_camera, Vec2(minCorner.x, 0.0));
        const QPointF right = toScreenPoint(m_camera, Vec2(maxCorner.x, 0.0));
        painter->drawLine(left, right);
    }
    if (minCorner.x <= 0.0 && maxCorner.x >= 0.0) {
        const QPointF top = toScreenPoint(m_camera, Vec2(0.0, maxCorner.y));
        const QPointF bottom = toScreenPoint(m_camera, Vec2(0.0, minCorner.y));
        painter->drawLine(top, bottom);
    }
}

void CanvasWidget::drawGridLabels(QPainter *painter)
{
    const double step = m_camera.gridStep();
    if (step <= 0.0) {
        return;
    }

    Vec2 minCorner;
    Vec2 maxCorner;
    m_camera.visibleSceneRect(&minCorner, &maxCorner);

    // 标签稀疏化：屏幕上间距过密时按整数倍抽稀，否则文字会糊成一团
    const double labelPixels = 64.0;
    long stride = 1;
    while (step * stride * m_camera.zoom() < labelPixels && stride < 1000) {
        stride *= 2;
    }

    const int decimals = (step >= 1.0) ? 0 : static_cast<int>(std::ceil(-std::log10(step))) + 1;
    const QColor labelColor = palette().color(QPalette::WindowText);
    painter->setPen(labelColor);

    const QPointF originScreen = toScreenPoint(m_camera, Vec2(0.0, 0.0));
    const double axisY = std::min(std::max(originScreen.y(), 14.0), static_cast<double>(height() - 4));
    const double axisX = std::min(std::max(originScreen.x(), 4.0), static_cast<double>(width() - 44));

    const long firstX = static_cast<long>(std::floor(minCorner.x / step));
    const long lastX = static_cast<long>(std::ceil(maxCorner.x / step));
    const long firstY = static_cast<long>(std::floor(minCorner.y / step));
    const long lastY = static_cast<long>(std::ceil(maxCorner.y / step));

    for (long i = firstX; i <= lastX; ++i) {
        if (i % stride != 0 || i == 0) {
            continue;
        }
        const double x = i * step;
        const QPointF screen = toScreenPoint(m_camera, Vec2(x, 0.0));
        painter->drawText(QPointF(screen.x() - 12.0, axisY + 13.0),
                          QString::number(x, 'f', decimals));
    }
    for (long j = firstY; j <= lastY; ++j) {
        if (j % stride != 0 || j == 0) {
            continue;
        }
        const double y = j * step;
        const QPointF screen = toScreenPoint(m_camera, Vec2(0.0, y));
        painter->drawText(QPointF(axisX + 5.0, screen.y() + 4.0),
                          QString::number(y, 'f', decimals));
    }
}

void CanvasWidget::drawShapes(QPainter *painter)
{
    if (m_document == nullptr) {
        return;
    }

    const QList<Shape *> shapes = m_document->shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        drawShape(painter, shapes.at(i));
    }
}

void CanvasWidget::drawShape(QPainter *painter, const Shape *shape)
{
    if (shape == nullptr || !shape->isValid()) {
        return; // 退化或停用的图元不绘制，但保留在文档里（拖动可以把它拖回来）
    }

    const bool isSelected = m_document->isSelected(shape->id());
    const bool isHovered = (m_hoveredId == shape->id());
    const bool isHighlighted = (m_highlightId == shape->id());

    QColor curveColor = palette().color(QPalette::WindowText);
    if (isSelected || isHighlighted) {
        curveColor = palette().color(QPalette::Highlight);
    } else if (isHovered) {
        curveColor = palette().color(QPalette::Highlight).lighter(120);
    }

    const QPen pen = makePen(curveColor, (isSelected || isHighlighted) ? 2.4 : 1.6);
    const QPen dashedPen = makePen(curveColor, 1.3, true);

    switch (shape->type()) {
    case ShapeType::Point: {
        const PointShape *point = static_cast<const PointShape *>(shape);
        drawPointMarker(painter, point->position(), isSelected, isHovered, false);
        if (m_labelsVisible) {
            drawLabel(painter, toScreenPoint(m_camera, point->position()) + QPointF(7.0, -7.0),
                      QStringLiteral("P%1").arg(shape->id()));
        }
        return;
    }
    case ShapeType::MidPoint:
    case ShapeType::IntersectionPoint: {
        // 派生点用"不可自由拖动"的观感：空心/不同色，避免用户白费力气去拖
        const Vec2 position = (shape->type() == ShapeType::MidPoint)
                                  ? static_cast<const MidPointShape *>(shape)->position()
                                  : static_cast<const IntersectionPointShape *>(shape)->position();
        drawPointMarker(painter, position, isSelected, isHovered, true);
        if (m_labelsVisible && shape->type() == ShapeType::IntersectionPoint) {
            drawLabel(painter, toScreenPoint(m_camera, position) + QPointF(7.0, -7.0),
                      QStringLiteral("I%1").arg(shape->id()));
        }
        return;
    }
    case ShapeType::Line:
    case ShapeType::PerpendicularLine:
    case ShapeType::ParallelLine:
    case ShapeType::AngleBisector: {
        const LineShape *line = static_cast<const LineShape *>(shape);
        // 派生线用虚线区分于用户直接画的线
        const bool derived = (shape->type() != ShapeType::Line);
        drawLineData(painter, line->lineData(), derived ? dashedPen : pen);
        return;
    }
    case ShapeType::Circle: {
        const CircleData &circle = static_cast<const CircleShape *>(shape)->circleData();
        if (!circle.isDefined) {
            return;
        }
        const QPointF center = toScreenPoint(m_camera, circle.center);
        const double radiusPixels = circle.radius * m_camera.zoom();
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(center, radiusPixels, radiusPixels);
        return;
    }
    case ShapeType::Arc: {
        drawArcData(painter, static_cast<const ArcShape *>(shape)->arcData(), pen);
        return;
    }
    case ShapeType::Polygon: {
        const QList<Vec2> &vertices = static_cast<const PolygonShape *>(shape)->vertices();
        if (vertices.size() < 3) {
            return;
        }
        QPolygonF polygon;
        for (int i = 0; i < vertices.size(); ++i) {
            polygon.append(toScreenPoint(m_camera, vertices.at(i)));
        }
        QColor fill = curveColor;
        fill.setAlpha(28);
        painter->setPen(pen);
        painter->setBrush(fill);
        painter->drawPolygon(polygon);
        painter->setBrush(Qt::NoBrush);
        return;
    }
    case ShapeType::Measurement: {
        const MeasurementShape *measurement = static_cast<const MeasurementShape *>(shape);
        drawLabel(painter, toScreenPoint(m_camera, measurement->anchor()),
                  measurement->displayText());
        return;
    }
    }
}

void CanvasWidget::drawPointMarker(QPainter *painter,
                                   const Vec2 &scenePos,
                                   bool isSelected,
                                   bool isHovered,
                                   bool isDerived) const
{
    const QPointF screen = toScreenPoint(m_camera, scenePos);

    if (isSelected || isHovered) {
        QColor halo = palette().color(QPalette::Highlight);
        halo.setAlpha(isSelected ? 120 : 70);
        painter->setPen(Qt::NoPen);
        painter->setBrush(halo);
        painter->drawEllipse(screen, kPointRadius + kPointHighlightGrow,
                             kPointRadius + kPointHighlightGrow);
    }

    const QColor fill = isDerived ? palette().color(QPalette::Link)
                                  : palette().color(QPalette::WindowText);
    painter->setPen(QPen(palette().color(QPalette::Base), 1.0));
    painter->setBrush(fill);
    painter->drawEllipse(screen, kPointRadius, kPointRadius);
    painter->setBrush(Qt::NoBrush);
}

void CanvasWidget::drawLineData(QPainter *painter, const LineData &line, const QPen &pen) const
{
    if (!line.isDefined) {
        return;
    }

    const double denominator = line.direction.lengthSquared();
    if (denominator <= GeoMath::kPositionTolerance) {
        return;
    }

    // 把参数区间换算成屏幕坐标下的两个端点：有界维度用真实端点，
    // 无界维度延伸出可见区域后再交给裁剪算法
    const QPointF originScreen = toScreenPoint(m_camera, line.origin);
    const QPointF endScreen = toScreenPoint(m_camera, line.origin + line.direction);

    QPointF startPoint = originScreen;
    QPointF endPoint = endScreen;
    const double extension = kClipExtensionFactor
        * std::max(static_cast<double>(width()), static_cast<double>(height()));

    if (!line.hasStartBound) {
        const double dx = endScreen.x() - originScreen.x();
        const double dy = endScreen.y() - originScreen.y();
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1e-9) {
            return;
        }
        startPoint = QPointF(originScreen.x() - dx / length * extension,
                             originScreen.y() - dy / length * extension);
    }
    if (!line.hasEndBound) {
        const double dx = endScreen.x() - originScreen.x();
        const double dy = endScreen.y() - originScreen.y();
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1e-9) {
            return;
        }
        endPoint = QPointF(endScreen.x() + dx / length * extension,
                           endScreen.y() + dy / length * extension);
    }

    QPointF clippedStart;
    QPointF clippedEnd;
    const QRectF visibleRect(0.0, 0.0, width(), height());
    if (!clipSegmentToRect(startPoint, endPoint, visibleRect, &clippedStart, &clippedEnd)) {
        return;
    }

    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(clippedStart, clippedEnd);
}

void CanvasWidget::drawArcData(QPainter *painter, const ArcData &arc, const QPen &pen) const
{
    if (!arc.isDefined || arc.radius <= GeoMath::kPositionTolerance) {
        return;
    }

    // 采样数与屏幕上的弧长成正比：放大后弧仍然是圆的，不会变成折线
    double sweep = GeoMath::normalizedAngle(arc.endAngle - arc.startAngle);
    if (sweep <= GeoMath::kParameterTolerance) {
        sweep = 2.0 * GeoMath::kPi;
    }
    const double screenArcLength = sweep * arc.radius * m_camera.zoom();
    int segments = static_cast<int>(std::ceil(screenArcLength / kArcSegmentPixels));
    segments = std::max(8, std::min(kArcMaxSegments, segments));

    QPainterPath path;
    for (int i = 0; i <= segments; ++i) {
        const double angle = arc.startAngle + sweep * (static_cast<double>(i) / segments);
        const Vec2 point = arc.center + Vec2(std::cos(angle), std::sin(angle)) * arc.radius;
        const QPointF screen = toScreenPoint(m_camera, point);
        if (i == 0) {
            path.moveTo(screen);
        } else {
            path.lineTo(screen);
        }
    }

    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
}

void CanvasWidget::drawLabel(QPainter *painter, const QPointF &screenPos, const QString &text) const
{
    if (text.isEmpty()) {
        return;
    }

    const QFontMetrics metrics = painter->fontMetrics();
    const QRect textRect = metrics.boundingRect(text);
    QRectF background(screenPos.x() - 2.0, screenPos.y() - textRect.height() + 2.0,
                      textRect.width() + 6.0, textRect.height() + 2.0);

    // 底衬半透明：标注经常压在图形上，没有底衬会看不清
    QColor background2 = palette().color(QPalette::Base);
    background2.setAlpha(200);

    painter->setPen(Qt::NoPen);
    painter->setBrush(background2);
    painter->drawRect(background);

    painter->setPen(palette().color(QPalette::WindowText));
    painter->setBrush(Qt::NoBrush);
    painter->drawText(QPointF(screenPos.x() + 1.0, screenPos.y()), text);
}

void CanvasWidget::drawPreview(QPainter *painter)
{
    // 圆弧预览优先：它表达的是"弧的最终形状"，与折线橡皮筋不能混在一起画
    if (m_hasArcPreview) {
        const QColor previewColor = palette().color(QPalette::Highlight);
        const Vec2 center(m_arcPreviewCenter);
        const Vec2 rim(m_arcPreviewRim);
        const double radius = center.distanceTo(rim);
        if (radius > GeoMath::kPositionTolerance) {
            // 半径辅助线（虚线）让用户看清半径点在哪里
            painter->setPen(makePen(previewColor, 1.0, true));
            painter->setBrush(Qt::NoBrush);
            painter->drawLine(toScreenPoint(m_camera, center), toScreenPoint(m_camera, rim));

            // 用与正式绘制同一套代码预览弧的形状
            ArcData arc;
            arc.center = center;
            arc.radius = radius;
            arc.startAngle = GeoMath::angleOf(rim - center);
            const Vec2 arcPoint(m_arcPreviewArcPoint);
            arc.endAngle = arcPoint.isCloseTo(rim, GeoMath::kPositionTolerance)
                               ? arc.startAngle
                               : GeoMath::angleOf(arcPoint - center);
            arc.isDefined = true;
            drawArcData(painter, arc, makePen(previewColor, 1.4));
        }
        return;
    }

    if (m_previewPoints.isEmpty() && !m_hasPreviewHint) {
        return;
    }

    const QColor previewColor = palette().color(QPalette::Highlight);
    const QPen previewPen = makePen(previewColor, 1.4, true);

    painter->setPen(previewPen);
    painter->setBrush(Qt::NoBrush);

    if (m_previewAsCircle && !m_previewPoints.isEmpty() && m_hasPreviewHint) {
        const Vec2 center(m_previewPoints.first());
        const Vec2 rim(m_previewHint);
        const double radiusPixels = center.distanceTo(rim) * m_camera.zoom();
        if (radiusPixels > 0.5) {
            painter->drawEllipse(toScreenPoint(m_camera, center), radiusPixels, radiusPixels);
        }
        return;
    }

    // 已确定的顶点之间连实线，最后一个顶点到鼠标之间连橡皮筋
    painter->setPen(makePen(previewColor, 1.4, false));
    for (int i = 0; i + 1 < m_previewPoints.size(); ++i) {
        painter->drawLine(m_previewPoints.at(i), m_previewPoints.at(i + 1));
    }

    if (m_hasPreviewHint && !m_previewPoints.isEmpty()) {
        painter->setPen(previewPen);
        painter->drawLine(m_previewPoints.last(), m_previewHint);
        if (m_previewPoints.size() > 1) {
            // 多边形预览：把最后一点与起点也连起来，直观显示闭合形状
            painter->drawLine(m_previewHint, m_previewPoints.first());
        }
    }

    painter->setPen(Qt::NoPen);
    painter->setBrush(previewColor);
    for (int i = 0; i < m_previewPoints.size(); ++i) {
        painter->drawEllipse(m_previewPoints.at(i), 3.0, 3.0);
    }
    painter->setBrush(Qt::NoBrush);
}

QPen CanvasWidget::makePen(const QColor &color, double widthF, bool dash)
{
    QPen pen(color);
    pen.setWidthF(widthF);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    if (dash) {
        pen.setStyle(Qt::DashLine);
    }
    return pen;
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------

double CanvasWidget::hitTolerance() const
{
    return m_camera.pixelsToScene(kHitTolerancePixels);
}

ShapeId CanvasWidget::draggablePointAt(const Vec2 &scenePos) const
{
    if (m_document == nullptr) {
        return kInvalidShapeId;
    }
    return m_document->hitTestPoint(scenePos, hitTolerance());
}

void CanvasWidget::updateHover(const Vec2 &scenePos)
{
    if (m_document == nullptr) {
        return;
    }
    const ShapeId hit = m_document->hitTestAny(scenePos, hitTolerance());
    if (hit == m_hoveredId) {
        return;
    }
    m_hoveredId = hit;
    update();
}

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);

    if (m_document == nullptr) {
        return;
    }

    // 作图输入先过一遍吸附：这样"点在栅格上 / 点在已有对象上"是既成事实，
    // 而不是靠用户手稳
    const Vec2 rawPressScenePos = m_camera.toScene(event->pos());
    const Vec2 scenePos(snapScenePos(rawPressScenePos.toPointF()).position);

    // 中键或右键按住都可以平移视图。右键同时还要负责弹出快捷菜单，
    // 因此这里只记录按下位置，等松开时用位移阈值区分"右键点击"与"右键拖动"。
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_isPanning = true;
        m_panButton = event->button();
        m_lastPanScreenPos = event->pos();
        if (event->button() == Qt::RightButton) {
            m_rightPressScreenPos = event->pos();
            m_rightDragMoved = false;
        }
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() != Qt::LeftButton) {
        return;
    }

    const ShapeId hitPoint = draggablePointAt(scenePos);

    // 按下时统一刷新"拖动起点参考位置"：鼠标移动事件里是拿它与当前位置比较来判断
    // 位移的。若这里不刷新，比较对象会是上一次拖动留下的旧位置，位移判据随即失真
    // （实测表现：单击的松键被误判成"拖到终点"，在起点原地收尾出零长度图形）。
    m_lastDragScreenPos = event->pos();

    if (m_activeTool == ToolId::Select) {
        const bool additive = event->modifiers().testFlag(Qt::ControlModifier)
            || event->modifiers().testFlag(Qt::ShiftModifier);
        const ShapeId hit = (hitPoint != kInvalidShapeId)
                                ? hitPoint
                                : m_document->hitTestAny(scenePos, hitTolerance());

        if (hit == kInvalidShapeId) {
            m_document->clearSelection();
            emit selectionChanged(QList<ShapeId>());
            return;
        }

        // Ctrl（或 Shift）点击为多选追加/移除；普通点击为单选。
        // 注意：QSet 的"范围构造"是 Qt 5.14 才有的，本项目基准是 5.12，
        // 因此这里显式遍历填充，不对 Qt 小版本做假设。
        QSet<ShapeId> selection;
        if (additive) {
            const QList<ShapeId> current = m_document->selection();
            for (int i = 0; i < current.size(); ++i) {
                selection.insert(current.at(i));
            }
        }
        if (additive && selection.contains(hit)) {
            selection.remove(hit);
        } else {
            selection.insert(hit);
        }
        m_document->setSelection(selection);
        emit selectionChanged(m_document->selection());

        if (hitPoint != kInvalidShapeId) {
            m_dragCandidateId = hitPoint;
            m_isDragging = false; // 真正移动超过阈值后才算拖动，避免误触产生空的撤销步骤
            m_lastDragScreenPos = event->pos();
            emit pointDragStarted(hitPoint);
        }
        return;
    }

    // 作图工具：按下即进入"可拖动"状态，随后"拖动并松手"或"再点一下"都能完成作图。
    // 关键：**即使按在空白处也要进入这个状态**——那时起点是一个新建的点（由控制器
    // 在 clicked 里创建），命中 id 为空。早先版本用"是否命中已有点"当作进入条件，
    // 结果在空白处起笔画圆/画线时拖动分支被整段跳过，表现为"点了没反应"（实测回归）。
    //
    // 同时必须清掉上一次"选择工具拖动点"留下的候选：否则接下来的鼠标移动会走进
    // 选择拖动分支并直接 return，作图拖动永远收不到事件（这也是实测出来的，
    // 表现为"按住拖动毫无反应"）。
    m_dragCandidateId = kInvalidShapeId;
    m_isDragging = false;
    m_constructionStartId = hitPoint; // 命中已有点则复用；未命中时由控制器新建
    m_constructionArmed = true;
    m_constructionStartScenePos = scenePos.toPointF();
    m_constructPressScenePos = scenePos; // 松键时用它判断是否拖开了
    m_isConstructionDragging = false;
    m_lastDragScreenPos = event->pos();
    emit constructionDragStarted(hitPoint);

    // 发出单击信号：控制器据此"确定起点"（复用已有点或新建一个）。
    // 若用户接着拖动并松手，由 constructionDragFinished 以终点完成图形。
    emit canvasClicked(scenePos.toPointF());
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    const Vec2 rawScenePos = m_camera.toScene(event->pos());
    const Vec2 scenePos = Vec2(snapScenePos(rawScenePos.toPointF()).position);

    if (m_isPanning) {
        const QPointF screenDelta = event->pos() - m_lastPanScreenPos;
        m_lastPanScreenPos = event->pos();
        if (m_panButton == Qt::RightButton
            && static_cast<int>((event->pos() - m_rightPressScreenPos).manhattanLength()) > 4) {
            m_rightDragMoved = true; // 右键已经拖出阈值：松手时不再弹快捷菜单
        }
        // 屏幕位移转场景位移：内容跟随鼠标，因此相机中心向相反方向移动
        const Vec2 sceneDelta(-screenDelta.x() / m_camera.zoom(),
                              screenDelta.y() / m_camera.zoom());
        emit panRequested(sceneDelta.toPointF());
        return;
    }

    if (m_activeTool == ToolId::Select && m_dragCandidateId != kInvalidShapeId) {
        const int movedPixels = static_cast<int>(
            (event->pos() - m_lastDragScreenPos).manhattanLength());
        if (!m_isDragging && movedPixels < 3) {
            return; // 小于阈值视为点击，不产生移动
        }
        m_isDragging = true;
        emit pointDragged(m_dragCandidateId, scenePos.toPointF());
        return;
    }

    // 作图工具的"按下-拖动"：仅对支持一次拖动完成的工具给出预览，
    // 否则（垂线、交点等）会画出误导性的橡皮筋
    if (isDragConstructionTool(m_activeTool) && m_constructionArmed) {
        const int movedPixels = static_cast<int>(
            (event->pos() - m_lastDragScreenPos).manhattanLength());
        if (movedPixels >= 3) {
            m_isConstructionDragging = true;
        }
        if (m_isConstructionDragging) {
            if (m_activeTool == ToolId::ArcCenter) {
                // 圆心式圆弧：三次点击之间必须让用户看见"已定的圆心与半径在哪"、
                // "弧将扫到哪里"。只拉一条从圆心出发的橡皮筋是不够的——
                // 那与线段工具的画面完全一样，用户根本认不出自己在画弧（实测症状）。
                emit arcCenterPreviewNeeded(m_constructionStartScenePos, scenePos.toPointF());
            } else {
                // 其余工具：从起点拉一条橡皮筋到当前鼠标位置（画圆时显示为预览圆）
                QList<QPointF> previewPoints;
                previewPoints.append(m_constructionStartScenePos);
                setPreview(previewPoints, scenePos.toPointF(), m_activeTool == ToolId::Circle);
            }
        }
        return;
    }

    // 状态栏坐标显示**原始**位置而不是吸附后的位置：吸附是"作图落点"的规则，
    // 显示真实鼠标坐标才便于用户判断自己在哪、以及吸附把它挪了多远。
    // 同时做节流：拖动时不刷新（坐标此刻意义不大），静止时仅在跨过整像素时更新。
    if (!rawScenePos.isCloseTo(m_lastHoverScenePos, m_camera.pixelsToScene(1.0))) {
        m_lastHoverScenePos = rawScenePos;
        emit mouseScenePosChanged(rawScenePos.toPointF());
    }

    updateHover(scenePos);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    // 平移结束：中键与右键都走这里。右键若没有拖出阈值，则视为"右键点击"，
    // 由随后的 contextMenuEvent 弹出快捷菜单（这里不处理）。
    if ((event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
        && m_isPanning) {
        m_isPanning = false;
        m_panButton = Qt::NoButton;
        unsetCursor();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        return;
    }

    if (m_dragCandidateId != kInvalidShapeId) {
        if (m_isDragging) {
            emit pointDragFinished();
        }
        m_dragCandidateId = kInvalidShapeId;
        m_isDragging = false;
    }

    // 作图工具的"按下-拖动"收尾，判据只有一条：**按住期间鼠标真的移动过**
    // （m_isConstructionDragging 只在移动事件超过像素阈值时才置位）。
    //   单击（按下与松开之间没有移动事件）→ 不收尾，交给"再点一下"完成；
    //   拖动 → 以当前位置完成图形（一次拖出线段/圆/弧上点）。
    //
    // 这里**不能**改用"松开位置离按下位置远不远"来判断：那个判据在绝对坐标上成立，
    // 于是"按下时鼠标在 A、松开时鼠标已经在 B"（快速点两下、或按下瞬间手抖）
    // 就会被误判成拖动，把图形在错误的终点上直接建出来——实测症状是
    // "圆心两点画圆弧不能正常绘制"：第一下点击就把弧完成在半径点了。
    if (isDragConstructionTool(m_activeTool) && m_constructionArmed) {
        if (m_isConstructionDragging) {
            const Vec2 scenePos = m_camera.toScene(event->pos());
            emit constructionDragFinished(scenePos.toPointF());
            m_constructionArmed = false;
        }
    }
    m_isConstructionDragging = false;
}

void CanvasWidget::contextMenuEvent(QContextMenuEvent *event)
{
    // 右键拖动过视图就不弹菜单：那样的右键操作意图是平移，不是菜单
    if (m_rightDragMoved) {
        m_rightDragMoved = false;
        return;
    }

    // QContextMenuEvent::pos() 是 QPoint（屏幕/窗口坐标），Camera 接受 QPointF
    const Vec2 rawScenePosVec = m_camera.toScene(QPointF(event->pos()));
    const Vec2 scenePosVec = Vec2(snapScenePos(rawScenePosVec.toPointF()).position);
    const QPointF scenePos = scenePosVec.toPointF();

    QMenu menu(this);
    const ShapeId hit = (m_document != nullptr)
                            ? m_document->hitTestAny(scenePosVec, hitTolerance())
                            : kInvalidShapeId;

    // 与右键位置相关的快捷添加：把"先选工具、再点位置"压缩成一步
    QAction *pointAction = menu.addAction(tr("在此处添加点"));
    QAction *segmentAction = menu.addAction(tr("从此处画线段"));
    QAction *circleAction = menu.addAction(tr("以此处为圆心画圆"));
    QAction *distanceAction = menu.addAction(tr("从此处量距离"));

    menu.addSeparator();
    QAction *fitAction = menu.addAction(tr("缩放到全部内容"));
    QAction *deleteAction = menu.addAction(tr("删除选中图元 (Del)"));
    deleteAction->setEnabled(m_document != nullptr && !m_document->selection().isEmpty());
    if (hit != kInvalidShapeId) {
        deleteAction->setText(tr("删除选中图元 (Del)"));
    }

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == nullptr) {
        return;
    }

    if (chosen == pointAction) {
        emit quickPointRequested(scenePos);
    } else if (chosen == segmentAction) {
        emit quickSegmentRequested(scenePos);
    } else if (chosen == circleAction) {
        emit quickCircleRequested(scenePos);
    } else if (chosen == distanceAction) {
        emit quickDistanceRequested(scenePos);
    } else if (chosen == fitAction) {
        fitContent();
    } else if (chosen == deleteAction) {
        emit quickDeleteRequested();
    }
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }

    // 双击用于：结束多边形绘制；两点类工具则以双击位置"确定起点"（控制器决定语义）。
    // 画布这边只需要把拖动起点定位到双击位置，让随后的橡皮筋从正确的地方出发。
    const Vec2 scenePos = m_camera.toScene(event->pos());
    if (isDragConstructionTool(m_activeTool)) {
        m_constructionStartScenePos = scenePos.toPointF();
        m_constructionArmed = true;
        m_constructPressScenePos = scenePos;
        m_isConstructionDragging = false;
        m_lastDragScreenPos = event->pos();
    }

    // 双击用于：结束多边形绘制，以及两点类工具的"起笔"（控制器决定语义）
    emit canvasDoubleClicked(scenePos.toPointF());
}

void CanvasWidget::wheelEvent(QWheelEvent *event)
{
    const int notches = event->angleDelta().y();
    if (notches == 0) {
        return;
    }
    const double factor = (notches > 0) ? kWheelZoomFactor : (1.0 / kWheelZoomFactor);
    emit zoomRequested(factor, m_camera.toScene(event->pos()).toPointF());
}

void CanvasWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        emit deleteRequested();
        return;
    case Qt::Key_Escape:
        emit escapeRequested();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::resizeEvent(QResizeEvent *event)
{
    m_camera.setViewportSize(event->size().width(), event->size().height());
    QWidget::resizeEvent(event);
}

void CanvasWidget::leaveEvent(QEvent *event)
{
    if (m_hoveredId != kInvalidShapeId) {
        m_hoveredId = kInvalidShapeId;
        update();
    }
    QWidget::leaveEvent(event);
}
