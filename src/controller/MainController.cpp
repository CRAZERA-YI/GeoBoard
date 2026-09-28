/**
 * @file    controller/MainController.cpp
 * @brief   编排层的实现：工具状态机与撤销栈。
 * @details 关于"拖动"这一操作的实现要点：拖动过程中**直接改动文档**（这样依赖图形
 *          才会实时跟随），在**松开鼠标时**才把整段位移压成一条命令入栈。
 *          压栈时 QUndoCommand::redo() 会再次把点设到终点坐标——这是一次幂等的
 *          重复赋值（重算结果完全相同），换来的是"拖动中图形实时跟随"这一必须的体验。
 */

#include "MainController.h"

#include "view/GeoBoardMainWindow.h"
#include "view/CanvasWidget.h"
#include "view/SceneExporter.h"
#include "view/ShapeListPanel.h"

#include <QAction>
#include <QFileInfo>
#include <QPointF>
#include <QTimer>
#include <QUndoStack>

#include <cmath>
#include <cstdio>

namespace {

/** @brief 保留工具（不自动切回选择工具）的集合。 */
bool isStickyTool(ToolId tool)
{
    return tool == ToolId::Select || tool == ToolId::Point || tool == ToolId::PointOnObject;
}

/** @brief 判断某工具是否属于"两点确定"的一族。 */
bool isTwoPointTool(ToolId tool)
{
    return tool == ToolId::Segment || tool == ToolId::Ray || tool == ToolId::InfiniteLine
        || tool == ToolId::Circle || tool == ToolId::MidPoint;
}

/**
 * @brief 本文件内部对"能否拖动作图"这一判断的转发。
 * @param tool 工具。
 * @return 支持"按住起点拖到终点"返回 true。
 * @note  必须转发到 controller/ToolId.h 里的**唯一定义**，不能在这里另写一份。
 *        以前画布与控制器各写了一份、只靠注释保证一致，结果新增"圆心式圆弧"时
 *        只改了一处，画布便永远走不到圆弧预览分支（真实缺陷：
 *        "圆心两点画圆弧不能正常绘制"）。
 */
bool supportsDrag(ToolId tool)
{
    return isDragConstructionTool(tool);
}

/** @brief 视图缩放的固定倍数。 */
const double kViewZoomStep = 1.25;

} // namespace

// ---------------------------------------------------------------------------
// 构造与启动
// ---------------------------------------------------------------------------

MainController::MainController(GeoBoardMainWindow *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_document(new GeoDocument(this))
    , m_store(new JsonStore(this))
    , m_undoStack(new QUndoStack(this))
    , m_exporter(new SceneExporter())
    , m_filePath()
    , m_currentTool(ToolId::Select)
    , m_baseCurveId(kInvalidShapeId)
    , m_firstCurveId(kInvalidShapeId)
    , m_curveFilterShapeId(kInvalidShapeId)
    , m_toolPointAId(kInvalidShapeId)
    , m_toolPointBId(kInvalidShapeId)
    , m_arcCenterPointId(kInvalidShapeId)
    , m_arcRadiusPointId(kInvalidShapeId)
    , m_polygonPointIds()
    , m_previewScenePoints()
    , m_dragPointId(kInvalidShapeId)
    , m_dragStartPosition()
    , m_isDraggingPoint(false)
{
    CanvasWidget *canvas = m_view->canvas();

    // 画布 → 控制器
    connect(canvas, &CanvasWidget::canvasClicked, this, &MainController::onCanvasClicked);
    connect(canvas, &CanvasWidget::canvasDoubleClicked, this, &MainController::onCanvasDoubleClicked);
    connect(canvas, &CanvasWidget::arcCenterPreviewNeeded,
            this, &MainController::onArcCenterPreviewNeeded);
    connect(canvas, &CanvasWidget::mouseScenePosChanged, this, &MainController::updateMouseStatus);
    connect(canvas, &CanvasWidget::pointDragStarted, this, &MainController::onPointDragStarted);
    connect(canvas, &CanvasWidget::pointDragged, this, &MainController::onPointDragged);
    connect(canvas, &CanvasWidget::pointDragFinished, this, &MainController::onPointDragFinished);
    connect(canvas, &CanvasWidget::selectionChanged, this, &MainController::onSelectionChanged);
    connect(canvas, &CanvasWidget::deleteRequested, this, &MainController::onDeleteRequested);
    connect(canvas, &CanvasWidget::escapeRequested, this, &MainController::onEscapeRequested);
    connect(canvas, &CanvasWidget::zoomRequested, this, &MainController::onZoomRequested);
    connect(canvas, &CanvasWidget::panRequested, this, &MainController::onPanRequested);
    connect(canvas, &CanvasWidget::constructionDragStarted, this,
            &MainController::onConstructionDragStarted);
    connect(canvas, &CanvasWidget::constructionDragFinished, this,
            &MainController::onConstructionDragFinished);
    connect(canvas, &CanvasWidget::quickPointRequested, this, &MainController::onQuickPointRequested);
    connect(canvas, &CanvasWidget::quickSegmentRequested, this,
            &MainController::onQuickSegmentRequested);
    connect(canvas, &CanvasWidget::quickCircleRequested, this,
            &MainController::onQuickCircleRequested);
    connect(canvas, &CanvasWidget::quickDistanceRequested, this,
            &MainController::onQuickDistanceRequested);
    connect(canvas, &CanvasWidget::quickDeleteRequested, this,
            &MainController::onQuickDeleteRequested);

    // 主窗口 → 控制器
    connect(m_view, &GeoBoardMainWindow::toolChangeRequested, this, &MainController::onToolChangeRequested);
    connect(m_view, &GeoBoardMainWindow::gridVisibilityToggled, this, &MainController::onGridVisibilityToggled);
    connect(m_view, &GeoBoardMainWindow::axesVisibilityToggled, this, &MainController::onAxesVisibilityToggled);
    connect(m_view, &GeoBoardMainWindow::labelsVisibilityToggled, this, &MainController::onLabelsVisibilityToggled);
    connect(m_view, &GeoBoardMainWindow::newRequested, this, &MainController::onNewRequested);
    connect(m_view, &GeoBoardMainWindow::openRequested, this, &MainController::onOpenRequested);
    connect(m_view, &GeoBoardMainWindow::saveRequested, this, &MainController::onSaveRequested);
    connect(m_view, &GeoBoardMainWindow::undoRequested, this, &MainController::onUndoRequested);
    connect(m_view, &GeoBoardMainWindow::redoRequested, this, &MainController::onRedoRequested);
    connect(m_view, &GeoBoardMainWindow::zoomInRequested, this, &MainController::onZoomInRequested);
    connect(m_view, &GeoBoardMainWindow::zoomOutRequested, this, &MainController::onZoomOutRequested);
    connect(m_view, &GeoBoardMainWindow::zoomResetRequested, this, &MainController::onZoomResetRequested);
    connect(m_view, &GeoBoardMainWindow::fitContentRequested, this, &MainController::onFitContentRequested);
    connect(m_view, &GeoBoardMainWindow::exportRequested, this, &MainController::onExportRequested);
    connect(m_view, &GeoBoardMainWindow::snapToggled, this, &MainController::onSnapToggled);
    connect(m_view, &GeoBoardMainWindow::gridPrecisionRequested,
            this, &MainController::onGridPrecisionRequested);
    connect(m_view->shapeListPanel(), &ShapeListPanel::selectionChanged,
            this, &MainController::onShapeListSelectionChanged);
    connect(m_view->shapeListPanel(), &ShapeListPanel::focusRequested,
            this, &MainController::onShapeListFocusRequested);
    connect(m_view->shapeListPanel(), &ShapeListPanel::deleteRequested,
            this, &MainController::onShapeListDeleteRequested);
    connect(m_view, &GeoBoardMainWindow::aboutRequested, this, &MainController::onAboutRequested);

    // 撤销栈 → 界面（可用性与未保存标记）
    connect(m_undoStack, &QUndoStack::canUndoChanged, this, [this](bool canUndo) {
        m_view->setUndoRedoEnabled(canUndo, m_undoStack->canRedo());
    });
    connect(m_undoStack, &QUndoStack::canRedoChanged, this, [this](bool canRedo) {
        m_view->setUndoRedoEnabled(m_undoStack->canUndo(), canRedo);
    });
    connect(m_undoStack, &QUndoStack::cleanChanged, this, [this](bool clean) {
        m_view->setModified(!clean);
    });
}

MainController::~MainController() = default;

void MainController::createDemoConstruction()
{
    // 演示内容：以原点为圆心、半径 3 的圆；一条自左向右穿过圆的线段；
    // 它们的两个交点；线段中点；以及三个标注。拖动圆上点或线段的端点，
    // 交点会随两条曲线实时重算——这正是"动态几何"要展示的核心行为。
    m_document->beginUpdate();

    const ShapeId centerId = m_document->allocateShapeId();
    m_document->addShape(new PointShape(centerId, PointKind::Free, Vec2(0.0, 0.0)));

    const ShapeId rimId = m_document->allocateShapeId();
    m_document->addShape(new PointShape(rimId, PointKind::Free, Vec2(3.0, 0.0)));

    const ShapeId circleId = m_document->allocateShapeId();
    m_document->addShape(new CircleShape(circleId, centerId, rimId));

    const ShapeId lineStartId = m_document->allocateShapeId();
    m_document->addShape(new PointShape(lineStartId, PointKind::Free, Vec2(-4.5, 1.8)));

    const ShapeId lineEndId = m_document->allocateShapeId();
    m_document->addShape(new PointShape(lineEndId, PointKind::Free, Vec2(4.5, 1.8)));

    const ShapeId segmentId = m_document->allocateShapeId();
    m_document->addShape(new LineShape(segmentId, LineKind::Segment, lineStartId, lineEndId));

    // 线段的两个端点落在圆上：把它们做成"附着点"，从而演示受约束的点
    m_document->setPointKind(lineStartId, PointKind::OnObject, circleId);
    m_document->setPointKind(lineEndId, PointKind::OnObject, circleId);

    for (int i = 0; i < 2; ++i) {
        const ShapeId intersectionId = m_document->allocateShapeId();
        m_document->addShape(new IntersectionPointShape(intersectionId, circleId, segmentId, i));
    }

    const ShapeId midId = m_document->allocateShapeId();
    m_document->addShape(new MidPointShape(midId, lineStartId, lineEndId));

    const ShapeId radiusMeasureId = m_document->allocateShapeId();
    m_document->addShape(new MeasurementShape(radiusMeasureId, MeasurementKind::Radius,
                                              QList<ShapeId>(), circleId, Vec2(0.0, 3.6)));

    const ShapeId lengthMeasureId = m_document->allocateShapeId();
    m_document->addShape(new MeasurementShape(lengthMeasureId, MeasurementKind::SegmentLength,
                                              QList<ShapeId>(), segmentId, Vec2(-4.4, 2.4)));

    QList<ShapeId> angleInputs;
    angleInputs << lineStartId << midId << lineEndId;
    const ShapeId angleMeasureId = m_document->allocateShapeId();
    m_document->addShape(new MeasurementShape(angleMeasureId, MeasurementKind::Angle,
                                              angleInputs, kInvalidShapeId, Vec2(0.0, -3.6)));

    m_document->endUpdate();

    // 适配视图延后到事件循环启动之后再做：此刻窗口刚 show()，布局尚未最终结算，
    // 直接 fitContent() 会用到过时的视口尺寸，导致内容没有真正居中。
    QTimer::singleShot(0, this, [this]() {
        m_view->canvas()->fitContent();
        m_view->setZoomPercent(m_view->canvas()->camera()->zoomPercent());
    });
    m_view->setStatusMessage(
        tr("演示图形：用选择工具拖动圆上的点或圆心，观察交点、中点与标注如何实时跟随"), 0);
}

void MainController::start()
{
    CanvasWidget *canvas = m_view->canvas();
    canvas->setDocument(m_document);
    // 对象列表面板也绑定同一个文档：它自己监听 documentChanged/selectionChanged 刷新，
    // 因此这里只需要绑一次，之后无需控制器再推数据
    m_view->shapeListPanel()->setDocument(m_document);

    m_undoStack->clear();
    m_view->setModified(false);
    m_view->setUndoRedoEnabled(false, false);
    m_view->setActiveTool(m_currentTool);
    m_view->setZoomPercent(canvas->camera()->zoomPercent());
    m_view->setSelectionCount(0);
    m_view->setStatusMessage(toolHint(m_currentTool), 0);
}

// ---------------------------------------------------------------------------
// 工具切换与状态机复位
// ---------------------------------------------------------------------------

void MainController::onToolChangeRequested(ToolId tool)
{
    if (m_currentTool != tool) {
        cancelPendingDrawing();
    }
    m_currentTool = tool;
    m_view->setActiveTool(tool);
    m_view->canvas()->setActiveTool(tool);
    m_view->setStatusMessage(toolHint(tool), 0);
}

void MainController::cancelPendingDrawing()
{
    m_baseCurveId = kInvalidShapeId;
    m_firstCurveId = kInvalidShapeId;
    m_curveFilterShapeId = kInvalidShapeId;
    m_toolPointAId = kInvalidShapeId;
    m_toolPointBId = kInvalidShapeId;
    m_arcCenterPointId = kInvalidShapeId;
    m_arcRadiusPointId = kInvalidShapeId;
    m_polygonPointIds.clear();
    m_previewScenePoints.clear();

    if (m_view != nullptr) {
        m_view->canvas()->clearPreview();
        // 圆弧预览与折线预览是两条独立的绘制路径，取消时都要清掉，
        // 否则按 Esc 之后画布上会残留一条"幽灵弧"
        m_view->canvas()->clearArcPreview();
        m_view->canvas()->setHighlight(kInvalidShapeId);
    }
}

void MainController::onEscapeRequested()
{
    cancelPendingDrawing();
    m_view->setStatusMessage(toolHint(m_currentTool), 0);
}

// ---------------------------------------------------------------------------
// 取点与命中
// ---------------------------------------------------------------------------

ShapeId MainController::shapeAt(const QPointF &scenePos) const
{
    const Camera *camera = m_view->canvas()->camera();
    const double tolerance = camera->pixelsToScene(8.0);
    return m_document->hitTestAny(Vec2(scenePos), tolerance);
}

ShapeId MainController::curveAt(const QPointF &scenePos) const
{
    const Camera *camera = m_view->canvas()->camera();
    const double tolerance = camera->pixelsToScene(8.0);
    return m_document->hitTestCurve(Vec2(scenePos), tolerance);
}

ShapeId MainController::acquirePoint(const QPointF &scenePos, QList<Shape *> *shapes,
                                     const QList<ShapeId> &excludedIds)
{
    // 先过吸附：吸附可能给出"已有点"（复用）、"栅格点"（新建自由点），
    // 或"某条曲线上的点"（新建**附着点**，从而让这个点一直贴在那条曲线上）。
    Camera *camera = m_view->canvas()->camera();
    const CanvasWidget::SnapResult snap = m_view->canvas()->snapScenePos(scenePos);
    Vec2 position(snap.position);
    ShapeId hostCurveId = snap.hostCurveId;

    // 排除规则：被排除的点不能作为复用目标，也不能作为"落在曲线上"的依据。
    // 角度测量中若第三下点在顶点附近，吸附会把顶点当成第三个点，角退化成 0°、
    // 界面上看不到任何反馈（用户报过"角度不能用"）。
    const double tolerance = camera->pixelsToScene(8.0);
    const ShapeId atSnap = m_document->hitTestPoint(position, tolerance);
    if (atSnap != kInvalidShapeId && excludedIds.contains(atSnap)) {
        position = Vec2(scenePos);          // 退回鼠标真实位置
        hostCurveId = kInvalidShapeId;      // 并放弃吸附给出的曲线
    }

    // 吸附到已有点 → 复用，避免在同一个位置叠出第二个点
    const ShapeId reusableId = m_document->hitTestPoint(position, camera->pixelsToScene(4.0));
    if (reusableId != kInvalidShapeId && !excludedIds.contains(reusableId)) {
        return reusableId;
    }

    const ShapeId newId = m_document->allocateShapeId();
    if (hostCurveId != kInvalidShapeId && m_document->curve(hostCurveId) != nullptr) {
        // 落在曲线上：建成**附着点**，拖动时它只会沿这条曲线滑动
        PointShape *attached = new PointShape(newId, PointKind::OnObject, position);
        attached->setHostId(hostCurveId);
        shapes->append(attached);
    } else {
        shapes->append(new PointShape(newId, PointKind::Free, position));
    }
    return newId;
}

ShapeId MainController::beginConstructionAt(const QPointF &startPos, ToolId tool)
{
    if (!supportsDrag(tool)) {
        return kInvalidShapeId;
    }

    QList<Shape *> created;
    const ShapeId startId = acquirePoint(startPos, &created);
    if (startId == kInvalidShapeId) {
        return kInvalidShapeId;
    }

    // 起点若是新建的点，先单独入栈：用户可能只想放下一个点就 Esc 取消，
    // 也可能继续拖出图形；分成两条命令后撤销粒度更符合直觉。
    if (!created.isEmpty()) {
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
    }

    if (m_currentTool != tool) {
        // 走 onToolChangeRequested 而不是直接赋值：它会复位未完成的作图状态并刷新界面
        onToolChangeRequested(tool);
    }

    m_toolPointAId = startId;
    m_previewScenePoints.clear();
    m_previewScenePoints.append(startPos);

    if (tool == ToolId::MeasureAngle) {
        m_view->setStatusMessage(tr("已确定一条边上的点，请再确定角的顶点与另一条边"), 0);
    } else {
        m_view->setStatusMessage(tr("已确定起点：拖到终点松手，或者再点击一次"), 0);
    }
    return startId;
}

void MainController::onConstructionDragStarted(ShapeId id)
{
    // 只有能在一次拖动内完成的工具才参与拖动。其余工具（垂线/平行线/交点/多边形等）
    // 需要先选图元或多次点击，此处给出明确提示，避免"按住拖动却毫无反应"。
    if (!supportsDrag(m_currentTool)) {
        if (m_currentTool != ToolId::Select && id != kInvalidShapeId) {
            m_view->setStatusMessage(
                tr("%1需要逐次点击确定，请按状态栏提示操作").arg(toolName(m_currentTool)), 4000);
        }
        return;
    }
    Q_UNUSED(id); // 起点由随后的 canvasClicked 统一创建或复用
}

void MainController::onConstructionDragFinished(const QPointF &scenePos)
{
    if (!supportsDrag(m_currentTool) || m_toolPointAId == kInvalidShapeId) {
        return;
    }

    // 拖到终点松手：终点即"第二次点击"，复用同一套完成逻辑
    onCanvasClicked(scenePos);
    m_view->canvas()->clearPreview();
    m_view->canvas()->clearArcPreview();}

void MainController::onQuickPointRequested(const QPointF &scenePos)
{
    // 该位置已经有图元就不再叠加一个点（否则用户会得到两个几乎重合的点）
    if (shapeAt(scenePos) != kInvalidShapeId) {
        m_view->setStatusMessage(tr("该位置已有图元，未重复添加点"), 3000);
        return;
    }

    const ShapeId pointId = m_document->allocateShapeId();
    QList<Shape *> created;
    created.append(new PointShape(pointId, PointKind::Free, Vec2(scenePos)));
    m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
    m_view->setStatusMessage(tr("已创建点 P%1").arg(pointId), 3000);
}

void MainController::onQuickSegmentRequested(const QPointF &scenePos)
{
    if (beginConstructionAt(scenePos, ToolId::Segment) != kInvalidShapeId) {
        m_view->setStatusMessage(tr("已确定线段起点：拖到终点松手，或者再点击一次"), 0);
    }
}

void MainController::onQuickCircleRequested(const QPointF &scenePos)
{
    if (beginConstructionAt(scenePos, ToolId::Circle) != kInvalidShapeId) {
        m_view->setStatusMessage(tr("已确定圆心：拖到圆上一点松手，或者再点击一次"), 0);
    }
}

void MainController::onQuickDistanceRequested(const QPointF &scenePos)
{
    if (beginConstructionAt(scenePos, ToolId::MeasureDistance) != kInvalidShapeId) {
        m_view->setStatusMessage(tr("已确定第一个点：拖到第二个点松手，或者再点击一次"), 0);
    }
}

void MainController::onQuickDeleteRequested()
{
    onDeleteRequested();
}

QString MainController::shapeDisplayName(ShapeId id) const
{
    const Shape *shape = m_document->shape(id);
    if (shape == nullptr) {
        return QString();
    }

    switch (shape->type()) {
    case ShapeType::Point:
        return tr("点 P%1").arg(id);
    case ShapeType::Line: {
        switch (static_cast<const LineShape *>(shape)->lineKind()) {
        case LineKind::Segment:
            return tr("线段");
        case LineKind::Ray:
            return tr("射线");
        case LineKind::InfiniteLine:
            return tr("直线");
        }
        return tr("线");
    }
    case ShapeType::Circle:
        return tr("圆");
    case ShapeType::Arc:
        return tr("圆弧");
    case ShapeType::Polygon:
        return tr("多边形");
    case ShapeType::PerpendicularLine:
        return tr("垂线");
    case ShapeType::ParallelLine:
        return tr("平行线");
    case ShapeType::AngleBisector:
        return tr("角平分线");
    case ShapeType::MidPoint:
        return tr("中点");
    case ShapeType::IntersectionPoint:
        return tr("交点");
    case ShapeType::Measurement:
        return tr("标注");
    }
    return QString();
}

// ---------------------------------------------------------------------------
// 点击分发
// ---------------------------------------------------------------------------

void MainController::onCanvasClicked(const QPointF &scenePos)
{
    switch (m_currentTool) {
    case ToolId::Select:
        return; // 选择工具的行为由画布直接处理选中集与拖动
    case ToolId::Point: {
        QList<Shape *> created;
        const ShapeId pointId = acquirePoint(scenePos, &created);
        if (pointId == kInvalidShapeId) {
            return;
        }
        if (!created.isEmpty()) {
            m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
        }
        m_view->setStatusMessage(tr("已创建点 P%1").arg(pointId), 3000);
        return;
    }
    case ToolId::PointOnObject: {
        const ShapeId hostId = curveAt(scenePos);
        if (hostId == kInvalidShapeId) {
            m_view->setStatusMessage(tr("请点在一条曲线（线段、直线、射线、圆、弧或多边形）上"), 4000);
            return;
        }
        const ShapeId newId = m_document->allocateShapeId();
        PointShape *attached = new PointShape(newId, PointKind::OnObject, Vec2(scenePos));
        attached->setHostId(hostId);
        QList<Shape *> created;
        created.append(attached);
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建附着点")));
        m_view->setStatusMessage(tr("已在%1上创建点 P%2").arg(shapeDisplayName(hostId)).arg(newId), 3000);
        return;
    }
    case ToolId::Segment:
    case ToolId::Ray:
    case ToolId::InfiniteLine:
    case ToolId::Circle:
    case ToolId::MidPoint:
        handleTwoPointTool(scenePos, m_currentTool);
        return;
    case ToolId::Arc:
        handleArc(scenePos);
        return;
    case ToolId::ArcCenter:
        handleArcCenter(scenePos);
        return;
    case ToolId::Polygon:
        handlePolygon(scenePos);
        return;
    case ToolId::Perpendicular:
    case ToolId::Parallel:
        handlePerpendicularFamily(scenePos);
        return;
    case ToolId::AngleBisector:
        handleAngleBisector(scenePos);
        return;
    case ToolId::Intersection:
        handleIntersection(scenePos);
        return;
    case ToolId::MeasureDistance:
    case ToolId::MeasureLength:
    case ToolId::MeasureAngle:
    case ToolId::MeasureRadius:
    case ToolId::MeasureCircumference:
    case ToolId::MeasureArea:
    case ToolId::MeasureCircleArea:
    case ToolId::MeasureCoordinates:
        handleMeasurement(scenePos);
        return;
    }
}

void MainController::handleTwoPointTool(const QPointF &scenePos, ToolId tool)
{
    if (m_toolPointAId == kInvalidShapeId) {
        // 起点统一由 beginConstructionAt 处理：它负责复用/新建起点、单独入栈、
        // 复位状态并给出"可拖动也可再点一下"的提示。
        beginConstructionAt(scenePos, tool);
        return;
    }

    finishTwoPointConstruction(scenePos, tool, m_toolPointAId);
}

void MainController::finishTwoPointConstruction(const QPointF &scenePos, ToolId tool,
                                                ShapeId startId)
{
    QList<Shape *> created;

    if (tool == ToolId::MeasureDistance) {
        // 距离标注：两个输入点 + 一个标注，标注文字挂在中点
        QList<ShapeId> excluded;
        excluded << startId;
        const ShapeId secondId = acquirePoint(scenePos, &created, excluded);
        const ShapeId measurementId = m_document->allocateShapeId();
        QList<ShapeId> inputs;
        inputs << startId << secondId;
        const PointShape *startPoint = m_document->point(startId);
        const Vec2 startPosition = (startPoint != nullptr) ? startPoint->position() : Vec2(scenePos);
        created.append(new MeasurementShape(measurementId, MeasurementKind::Distance, inputs,
                                            kInvalidShapeId,
                                            (startPosition + Vec2(scenePos)) * 0.5));
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("测量距离")));
        m_view->setStatusMessage(tr("已创建距离标注"), 3000);

        m_toolPointAId = kInvalidShapeId;
        m_previewScenePoints.clear();
        m_view->canvas()->clearPreview();
        onToolChangeRequested(ToolId::Select);
        return;
    }

    // 排除已经用作起点的那个点：否则在同一处点两下会得到零长度的线段
    QList<ShapeId> excluded;
    excluded << startId;
    const ShapeId secondId = acquirePoint(scenePos, &created, excluded);
    const ShapeId shapeId = m_document->allocateShapeId();

    switch (tool) {
    case ToolId::Segment:
        created.append(new LineShape(shapeId, LineKind::Segment, startId, secondId));
        break;
    case ToolId::Ray:
        created.append(new LineShape(shapeId, LineKind::Ray, startId, secondId));
        break;
    case ToolId::InfiniteLine:
        created.append(new LineShape(shapeId, LineKind::InfiniteLine, startId, secondId));
        break;
    case ToolId::Circle:
        created.append(new CircleShape(shapeId, startId, secondId));
        break;
    case ToolId::MidPoint:
        created.append(new MidPointShape(shapeId, startId, secondId));
        break;
    default:
        return;
    }

    m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建%1").arg(toolName(tool))));

    // created 里除图形本身外，还可能包含本次新建的第二个点
    if (created.size() == 1) {
        m_view->setStatusMessage(tr("已创建%1").arg(toolName(tool)), 3000);
    } else {
        m_view->setStatusMessage(tr("已创建%1，并新建了终点").arg(toolName(tool)), 3000);
    }

    // 两点已用完，复位以便连续作图
    m_toolPointAId = kInvalidShapeId;
    m_previewScenePoints.clear();
    m_view->canvas()->clearPreview();
    if (!isStickyTool(tool)) {
        onToolChangeRequested(ToolId::Select);
    } else {
        m_view->setStatusMessage(toolHint(tool), 0);
    }
}

void MainController::handleArc(const QPointF &scenePos)
{
    QList<Shape *> created;

    if (m_toolPointAId == kInvalidShapeId) {
        m_toolPointAId = acquirePoint(scenePos, &created);
        m_previewScenePoints.clear();
        m_previewScenePoints.append(scenePos);
        m_view->setStatusMessage(tr("已确定起点，再点击两次确定弧上一点与终点"), 0);
    } else if (m_toolPointBId == kInvalidShapeId) {
        m_toolPointBId = acquirePoint(scenePos, &created);
        m_previewScenePoints.append(scenePos);
        m_view->setStatusMessage(tr("已确定弧上一点，再点击一次确定终点"), 0);
    } else {
        const ShapeId endId = acquirePoint(scenePos, &created);
        const ShapeId arcId = m_document->allocateShapeId();
        created.append(new ArcShape(arcId, m_toolPointAId, m_toolPointBId, endId));
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建圆弧")));
        m_view->setStatusMessage(tr("已创建圆弧"), 3000);

        m_toolPointAId = kInvalidShapeId;
        m_toolPointBId = kInvalidShapeId;
        m_previewScenePoints.clear();
        m_view->canvas()->clearPreview();
        onToolChangeRequested(ToolId::Select);
        return;
    }

    if (!created.isEmpty()) {
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
    }
    refreshPreview(scenePos);
}

void MainController::handlePolygon(const QPointF &scenePos)
{
    QList<Shape *> created;
    const ShapeId pointId = acquirePoint(scenePos, &created);
    if (pointId == kInvalidShapeId) {
        return;
    }

    // 双击结束时会先收到一次单击：若与上一个顶点相同则忽略，避免产生重复顶点
    if (!m_polygonPointIds.isEmpty() && m_polygonPointIds.last() == pointId) {
        return;
    }

    if (!created.isEmpty()) {
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
    }
    m_polygonPointIds.append(pointId);
    m_previewScenePoints.append(scenePos);

    if (m_polygonPointIds.size() < 3) {
        m_view->setStatusMessage(
            tr("多边形已有 %1 个顶点，至少需要 3 个；双击结束绘制").arg(m_polygonPointIds.size()), 0);
    } else {
        m_view->setStatusMessage(
            tr("多边形已有 %1 个顶点，双击结束绘制，或继续点击添加顶点").arg(m_polygonPointIds.size()), 0);
    }
    refreshPreview(scenePos);
}

void MainController::handleArcCenter(const QPointF &scenePos)
{
    QList<Shape *> created;

    // 三步各自用自己的成员记录：**不能**借用 m_toolPointAId/m_toolPointBId。
    // 那对成员是"三点弧"与两点类工具的输入槽，借用的后果是选择工具来一下、
    // 或者中间换过工具，弧的进度就被当成别的工具的进度（这是我最初埋下的隐患，
    // 也是"圆心两点画圆弧不正常"这类问题的温床）。
    if (m_arcCenterPointId == kInvalidShapeId) {
        // 第一次点击：圆心
        m_arcCenterPointId = acquirePoint(scenePos, &created);
        if (!created.isEmpty()) {
            m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
        }
        m_view->setStatusMessage(
            tr("已确定圆心（弧第一步）：再点一下确定半径；弧从半径点起逆时针扫向第三次点的位置"), 0);
        return;
    }

    if (m_arcRadiusPointId == kInvalidShapeId) {
        // 第二次点击：半径点。半径由圆心与它的距离决定。
        QList<ShapeId> excluded;
        excluded << m_arcCenterPointId;
        m_arcRadiusPointId = acquirePoint(scenePos, &created, excluded);
        if (!created.isEmpty()) {
            m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
        }
        m_view->setStatusMessage(
            tr("半径已确定（弧第二步）：再点一下确定弧扫到哪里；按 Esc 可重新开始"), 0);
        return;
    }

    // 第三次点击：弧上一点。弧从半径点起逆时针扫到它。
    QList<ShapeId> excluded;
    excluded << m_arcCenterPointId << m_arcRadiusPointId;
    const ShapeId arcPointId = acquirePoint(scenePos, &created, excluded);

    const ShapeId arcId = m_document->allocateShapeId();
    created.append(new ArcShape(arcId, m_arcCenterPointId, m_arcRadiusPointId, arcPointId,
                                ArcKind::CenterTwoPoint));
    m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建圆弧(圆心)")));

    const Shape *arc = m_document->shape(arcId);
    if (arc != nullptr && arc->isValid()) {
        const ArcData &data = static_cast<const ArcShape *>(arc)->arcData();
        m_view->setStatusMessage(tr("已创建圆弧：半径 %1，张角 %2°")
                                     .arg(data.radius, 0, 'f', 2)
                                     .arg(data.sweep() * 180.0 / GeoMath::kPi, 0, 'f', 1),
                                 4000);
    } else {
        m_view->setStatusMessage(tr("圆弧已创建，但当前退化（半径为零），拖开半径点即可恢复"), 6000);
    }

    cancelPendingDrawing();
    onToolChangeRequested(ToolId::Select);
}

void MainController::onArcCenterPreviewNeeded(const QPointF &center, const QPointF &cursor)
{
    CanvasWidget *canvas = m_view->canvas();

    const PointShape *centerPoint = m_document->point(m_arcCenterPointId);
    if (centerPoint == nullptr) {
        // 圆心还没定：此时画"圆心 → 鼠标"的半径示意，让用户知道这一下点的是圆心
        canvas->setArcPreview(center, cursor, cursor);
        return;
    }

    const PointShape *radiusPoint = m_document->point(m_arcRadiusPointId);
    if (radiusPoint == nullptr) {
        // 只定了圆心：预览"以圆心到鼠标的距离为半径"的整圆。
        // 这一步是用户判断"半径是不是我想要的"的唯一依据，必须画出来。
        canvas->setArcPreview(centerPoint->position().toPointF(), cursor, cursor);
        return;
    }

    // 圆心与半径都已定：预览从半径点逆时针扫到鼠标位置的弧。
    // 画的就是最终那条弧，因此"松手/点击得到的结果"与预览完全一致。
    canvas->setArcPreview(centerPoint->position().toPointF(),
                          radiusPoint->position().toPointF(), cursor);
}

void MainController::finishPolygon()
{
    if (m_polygonPointIds.size() < 3) {
        m_view->setStatusMessage(tr("多边形至少需要 3 个顶点，当前只有 %1 个").arg(m_polygonPointIds.size()),
                                 4000);
        return;
    }

    const ShapeId polygonId = m_document->allocateShapeId();
    QList<Shape *> created;
    created.append(new PolygonShape(polygonId, m_polygonPointIds));
    m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建多边形")));

    m_view->setStatusMessage(tr("已创建多边形（%1 个顶点）").arg(m_polygonPointIds.size()), 3000);
    cancelPendingDrawing();
    onToolChangeRequested(ToolId::Select);
}

void MainController::handlePerpendicularFamily(const QPointF &scenePos)
{
    // 第一步：选定基准曲线
    if (m_baseCurveId == kInvalidShapeId) {
        const ShapeId baseId = curveAt(scenePos);
        if (baseId == kInvalidShapeId) {
            m_view->setStatusMessage(tr("请先点击作为基准的曲线"), 4000);
            return;
        }
        const ShapeType baseType = m_document->shape(baseId)->type();
        if (baseType != ShapeType::Line && baseType != ShapeType::PerpendicularLine
            && baseType != ShapeType::ParallelLine && baseType != ShapeType::AngleBisector) {
            m_view->setStatusMessage(
                tr("平行与垂直只对线段、射线、直线、垂线、平行线、角平分线有定义；"
                   "圆与圆弧不能作为基准"),
                5000);
            return;
        }
        m_baseCurveId = baseId;
        m_view->canvas()->setHighlight(baseId);
        m_view->setStatusMessage(tr("基准为%1，请点击要经过的点").arg(shapeDisplayName(baseId)), 0);
        return;
    }

    // 第二步：通过点 + 生成派生线
    QList<Shape *> created;
    const ShapeId throughId = acquirePoint(scenePos, &created);
    const ShapeId lineId = m_document->allocateShapeId();
    const ShapeType derivedType = (m_currentTool == ToolId::Perpendicular)
                                      ? ShapeType::PerpendicularLine
                                      : ShapeType::ParallelLine;
    created.append(new LineShape(lineId, derivedType, m_baseCurveId, throughId));
    m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建%1").arg(toolName(m_currentTool))));
    m_view->setStatusMessage(tr("已创建%1").arg(toolName(m_currentTool)), 3000);

    cancelPendingDrawing();
    onToolChangeRequested(ToolId::Select);
}

void MainController::handleAngleBisector(const QPointF &scenePos)
{
    QList<Shape *> created;

    if (m_toolPointAId == kInvalidShapeId) {
        m_toolPointAId = acquirePoint(scenePos, &created);
        m_previewScenePoints.clear();
        m_previewScenePoints.append(scenePos);
        m_view->setStatusMessage(tr("已确定顶点，请点击第一条边上的一点"), 0);
    } else if (m_toolPointBId == kInvalidShapeId) {
        m_toolPointBId = acquirePoint(scenePos, &created);
        m_previewScenePoints.append(scenePos);
        m_view->setStatusMessage(tr("已确定第一条边，请点击第二条边上的一点"), 0);
    } else {
        const ShapeId secondRayId = acquirePoint(scenePos, &created);
        if (secondRayId == m_toolPointAId || secondRayId == m_toolPointBId) {
            m_view->setStatusMessage(tr("第二条边上的点不能与已有点重合"), 4000);
            return;
        }
        const ShapeId lineId = m_document->allocateShapeId();
        created.append(new LineShape(lineId, m_toolPointAId, m_toolPointBId, secondRayId));
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建角平分线")));
        m_view->setStatusMessage(tr("已创建角平分线"), 3000);

        cancelPendingDrawing();
        onToolChangeRequested(ToolId::Select);
        return;
    }

    if (!created.isEmpty()) {
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
    }
    refreshPreview(scenePos);
}

void MainController::handleIntersection(const QPointF &scenePos)
{
    const ShapeId curveId = curveAt(scenePos);
    if (curveId == kInvalidShapeId) {
        m_view->setStatusMessage(tr("请点击一条曲线"), 4000);
        return;
    }

    if (m_firstCurveId == kInvalidShapeId) {
        m_firstCurveId = curveId;
        m_view->canvas()->setHighlight(curveId);
        m_view->setStatusMessage(
            tr("第一条曲线为%1，请点击第二条曲线").arg(shapeDisplayName(curveId)), 0);
        return;
    }

    if (curveId == m_firstCurveId) {
        m_view->setStatusMessage(tr("请点击另一条曲线（同一条曲线的交点没有意义）"), 4000);
        return;
    }

    QList<Vec2> intersections;
    const int count = m_document->curveIntersections(m_firstCurveId, curveId, &intersections);
    if (count <= 0) {
        m_view->setStatusMessage(tr("这两条曲线当前没有交点（可能平行、相离或暂时退化）"), 5000);
        cancelPendingDrawing();
        return;
    }

    QList<Shape *> created;
    for (int i = 0; i < count; ++i) {
        const ShapeId pointId = m_document->allocateShapeId();
        created.append(new IntersectionPointShape(pointId, m_firstCurveId, curveId, i));
    }
    m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建交点")));
    m_view->setStatusMessage(tr("已创建 %1 个交点").arg(count), 3000);
    Q_UNUSED(intersections);

    cancelPendingDrawing();
    onToolChangeRequested(ToolId::Select);
}

void MainController::handleMeasurement(const QPointF &scenePos)
{
    switch (m_currentTool) {
    case ToolId::MeasureDistance:
    case ToolId::MeasureAngle: {
        // 距离需要 2 个点，角度需要 3 个点（第二次点击为顶点）
        QList<Shape *> created;
        if (m_toolPointAId == kInvalidShapeId) {
            m_toolPointAId = acquirePoint(scenePos, &created);
            m_previewScenePoints.clear();
            m_previewScenePoints.append(scenePos);
            m_view->setStatusMessage(
                m_currentTool == ToolId::MeasureAngle
                    ? tr("已确定第一个点，请点击角的顶点")
                    : tr("已确定第一个点，请点击第二个点"),
                0);
        } else if (m_toolPointBId == kInvalidShapeId) {
            // 排除已确定的第一个点：否则第二下点在它附近会被吸附成同一个点，
            // 距离恒为 0、角度恒为 0°，用户看到的就是"功能不能用"
            QList<ShapeId> excluded;
            excluded << m_toolPointAId;
            m_toolPointBId = acquirePoint(scenePos, &created, excluded);
            m_previewScenePoints.append(scenePos);
            if (m_currentTool == ToolId::MeasureDistance) {
                const ShapeId measurementId = m_document->allocateShapeId();
                QList<ShapeId> inputs;
                inputs << m_toolPointAId << m_toolPointBId;
                created.append(new MeasurementShape(measurementId, MeasurementKind::Distance,
                                                    inputs, kInvalidShapeId,
                                                    Vec2((scenePos.x() + m_previewScenePoints.first().x()) * 0.5,
                                                         (scenePos.y() + m_previewScenePoints.first().y()) * 0.5)));
                m_undoStack->push(new AddShapesCommand(m_document, created, tr("测量距离")));
                m_view->setStatusMessage(tr("已创建距离标注"), 3000);
                cancelPendingDrawing();
                onToolChangeRequested(ToolId::Select);
                return;
            }
            m_view->setStatusMessage(tr("已确定顶点，请点击角的第二条边上一点"), 0);
        } else {
            // 角度：第三个点不能与顶点或第一条边上的点重合，否则角不存在。
            // 这里把前两个点都排除掉，吸附就不会把它们当成第三个点。
            QList<ShapeId> excluded;
            excluded << m_toolPointAId << m_toolPointBId;
            const ShapeId thirdId = acquirePoint(scenePos, &created, excluded);
            const ShapeId measurementId = m_document->allocateShapeId();
            QList<ShapeId> inputs;
            inputs << m_toolPointAId << m_toolPointBId << thirdId;
            created.append(new MeasurementShape(measurementId, MeasurementKind::Angle,
                                                inputs, kInvalidShapeId, Vec2(scenePos)));
            m_undoStack->push(new AddShapesCommand(m_document, created, tr("测量角度")));

            // 三个点若共线，角会是 0° 或 180°；这不是错误（数学上成立），
            // 但要主动说明，否则用户会以为"角度又坏了"
            const MeasurementShape *measurement =
                static_cast<const MeasurementShape *>(m_document->shape(measurementId));
            if (measurement != nullptr && measurement->isValid()) {
                const double degrees = measurement->value() * 180.0 / GeoMath::kPi;
                if (degrees < 0.5 || degrees > 179.5) {
                    m_view->setStatusMessage(
                        tr("已创建角度标注：当前三点几乎共线，角度为 %1°；拖动端点即可看到变化")
                            .arg(degrees, 0, 'f', 1),
                        6000);
                } else {
                    m_view->setStatusMessage(tr("已创建角度标注：%1°").arg(degrees, 0, 'f', 1), 4000);
                }
            } else {
                m_view->setStatusMessage(tr("已创建角度标注"), 3000);
            }

            cancelPendingDrawing();
            onToolChangeRequested(ToolId::Select);
            return;
        }

        if (!created.isEmpty()) {
            m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点")));
        }
        refreshPreview(scenePos);
        return;
    }
    case ToolId::MeasureLength:
    case ToolId::MeasureRadius:
    case ToolId::MeasureCircumference:
    case ToolId::MeasureArea:
    case ToolId::MeasureCircleArea: {
        const ShapeId targetId = curveAt(scenePos);
        if (targetId == kInvalidShapeId) {
            m_view->setStatusMessage(tr("请点击要测量的图元"), 4000);
            return;
        }

        const Shape *target = m_document->shape(targetId);
        MeasurementKind kind = MeasurementKind::SegmentLength;
        QString commandText = tr("测量长度");

        if (m_currentTool == ToolId::MeasureLength) {
            if (target->type() != ShapeType::Line
                || static_cast<const LineShape *>(target)->lineKind() != LineKind::Segment) {
                m_view->setStatusMessage(tr("长度测量请点选一条线段"), 4000);
                return;
            }
            kind = MeasurementKind::SegmentLength;
        } else if (m_currentTool == ToolId::MeasureRadius
                   || m_currentTool == ToolId::MeasureCircumference) {
            if (target->type() != ShapeType::Circle) {
                m_view->setStatusMessage(tr("该测量请点选一个圆"), 4000);
                return;
            }
            kind = (m_currentTool == ToolId::MeasureRadius) ? MeasurementKind::Radius
                                                            : MeasurementKind::Circumference;
            commandText = (m_currentTool == ToolId::MeasureRadius) ? tr("测量半径") : tr("测量周长");
        } else if (m_currentTool == ToolId::MeasureCircleArea) {
            if (target->type() != ShapeType::Circle) {
                m_view->setStatusMessage(tr("圆面积测量请点选一个圆"), 4000);
                return;
            }
            kind = MeasurementKind::Area;
            commandText = tr("测量圆面积");
        } else {
            if (target->type() != ShapeType::Polygon) {
                m_view->setStatusMessage(tr("面积测量请点选一个多边形"), 4000);
                return;
            }
            kind = MeasurementKind::Area;
            commandText = tr("测量面积");
        }

        QList<Shape *> created;
        const ShapeId measurementId = m_document->allocateShapeId();
        created.append(new MeasurementShape(measurementId, kind, QList<ShapeId>(),
                                            targetId, Vec2(scenePos)));
        m_undoStack->push(new AddShapesCommand(m_document, created, commandText));
        m_view->setStatusMessage(tr("已创建%1标注").arg(toolName(m_currentTool)), 3000);
        onToolChangeRequested(ToolId::Select);
        return;
    }
    case ToolId::MeasureCoordinates: {
        // 坐标标注是"点选一个点"，不属于曲线，因此单独处理
        const ShapeId pointId = m_document->hitTestPoint(Vec2(scenePos),
                                                         m_view->canvas()->camera()->pixelsToScene(8.0));
        if (pointId == kInvalidShapeId) {
            m_view->setStatusMessage(tr("坐标标注请点选一个点"), 4000);
            return;
        }

        QList<Shape *> created;
        const ShapeId measurementId = m_document->allocateShapeId();
        QList<ShapeId> inputs;
        inputs << pointId;
        created.append(new MeasurementShape(measurementId, MeasurementKind::PointCoordinates,
                                            inputs, kInvalidShapeId, Vec2(scenePos)));
        m_undoStack->push(new AddShapesCommand(m_document, created, tr("标注点坐标")));
        m_view->setStatusMessage(tr("已创建坐标标注"), 3000);
        onToolChangeRequested(ToolId::Select);
        return;
    }
    default:
        return;
    }
}

void MainController::onCanvasDoubleClicked(const QPointF &scenePos)
{
    if (m_currentTool == ToolId::Polygon) {
        finishPolygon();
        return;
    }

    // 两点类工具（线段/射线/直线/圆/中点/距离）的双击语义：把双击当作**第一次点击**
    // （确定起点），随后的点击再确定终点。于是"双击 + 点击"与原来的"点两下"等价，
    // 用户不必关心自己用的是哪一种。
    // 先清掉未完成的作图状态，避免双击被理解成"结束上一次作图"。
    if (isTwoPointTool(m_currentTool) || m_currentTool == ToolId::MeasureDistance) {
        cancelPendingDrawing();
        if (m_currentTool == ToolId::MeasureDistance) {
            handleMeasurement(scenePos);
        } else {
            handleTwoPointTool(scenePos, m_currentTool);
        }
        return;
    }

    // 角度与其余工具的双击不改变语义：双击的第一次点击已经由 canvasClicked 处理过了，
    // 这里若再处理一次会导致"一次双击建两个图形"这类难以解释的行为。
}

// ---------------------------------------------------------------------------
// 预览与提示
// ---------------------------------------------------------------------------

void MainController::refreshPreview(const QPointF &scenePos)
{
    m_view->canvas()->setPreview(m_previewScenePoints, scenePos,
                                 m_currentTool == ToolId::Circle && m_previewScenePoints.size() == 1);
}

void MainController::refreshInputHighlight(const QPointF &scenePos)
{
    CanvasWidget *canvas = m_view->canvas();
    if (m_baseCurveId != kInvalidShapeId || m_firstCurveId != kInvalidShapeId) {
        return; // 已经锁定输入，保持高亮不变
    }

    const bool needsCurve = (m_currentTool == ToolId::Perpendicular)
        || (m_currentTool == ToolId::Parallel) || (m_currentTool == ToolId::Intersection)
        || (m_currentTool == ToolId::MeasureLength) || (m_currentTool == ToolId::MeasureRadius)
        || (m_currentTool == ToolId::MeasureCircumference)
        || (m_currentTool == ToolId::MeasureArea)
        || (m_currentTool == ToolId::MeasureCircleArea)
        || (m_currentTool == ToolId::PointOnObject);
    if (!needsCurve) {
        // "标注点坐标"期待的是点而不是曲线，用点命中来给反馈
        if (m_currentTool == ToolId::MeasureCoordinates) {
            canvas->setHighlight(m_document->hitTestPoint(
                Vec2(scenePos), canvas->camera()->pixelsToScene(8.0)));
        } else {
            canvas->setHighlight(kInvalidShapeId);
        }
        return;
    }

    canvas->setHighlight(curveAt(scenePos));
}

void MainController::updateMouseStatus(const QPointF &scenePos)
{
    m_view->setMouseStatus(tr("x = %1, y = %2").arg(scenePos.x(), 0, 'f', 2).arg(scenePos.y(), 0, 'f', 2));
}

// ---------------------------------------------------------------------------
// 拖动
// ---------------------------------------------------------------------------

void MainController::onPointDragStarted(ShapeId id)
{
    const PointShape *point = m_document->point(id);
    if (point == nullptr) {
        // 中点是两点坐标的均值，拖动它无法单独改变任何一个父点；
        // 交点的位置同样完全由两条曲线决定。这里明确不拖动，而不是静默无反应。
        m_dragPointId = kInvalidShapeId;
        const Shape *shape = m_document->shape(id);
        if (shape != nullptr && shape->type() == ShapeType::MidPoint) {
            m_view->setStatusMessage(tr("中点的位置由两个端点决定，请拖动端点"), 4000);
        } else if (shape != nullptr && shape->type() == ShapeType::IntersectionPoint) {
            m_view->setStatusMessage(tr("交点的位置由两条曲线决定，请拖动相关曲线的定义点"), 4000);
        }
        return;
    }

    m_dragPointId = id;
    m_dragStartPosition = point->position();
    m_isDraggingPoint = false;
}

void MainController::onPointDragged(ShapeId id, const QPointF &scenePos)
{
    if (m_dragPointId == kInvalidShapeId || id != m_dragPointId) {
        return;
    }
    m_isDraggingPoint = true;

    // 拖动过程中直接改文档：这样依赖它的图形才会实时跟随。
    // 批量更新成对出现，避免每个中间状态都触发一次全画布重绘。
    m_document->beginUpdate();
    m_document->movePoint(id, Vec2(scenePos));
    m_document->endUpdate();
}

void MainController::onPointDragFinished()
{
    if (m_dragPointId == kInvalidShapeId) {
        return;
    }

    const PointShape *point = m_document->point(m_dragPointId);
    if (m_isDraggingPoint && point != nullptr) {
        const Vec2 finalPosition = point->position();
        if (!finalPosition.isCloseTo(m_dragStartPosition, 1e-12)) {
            // 整段拖动只产生一条撤销命令（redo 会幂等地再设一次终点坐标）
            m_undoStack->push(new MovePointCommand(m_document, m_dragPointId,
                                                   m_dragStartPosition, finalPosition,
                                                   tr("移动点")));
        }
    }

    m_dragPointId = kInvalidShapeId;
    m_isDraggingPoint = false;
}

// ---------------------------------------------------------------------------
// 选中、删除、撤销
// ---------------------------------------------------------------------------

void MainController::onSelectionChanged(const QList<ShapeId> &ids)
{
    m_view->setUndoRedoEnabled(m_undoStack->canUndo(), m_undoStack->canRedo());
    // 状态栏显示选中数量（需求要求界面能反映当前选中了几个对象）。
    // 注意这里是"文档当前的选中集"而不是信号参数：两者在删除后被裁剪的情形下
    // 可能瞬时不一致，以文档为准才不会显示出已经消失的图元数。
    m_view->setSelectionCount(m_document->selection().size());
    Q_UNUSED(ids);
}

void MainController::onDeleteRequested()
{
    const QList<ShapeId> selected = m_document->selection();
    if (selected.isEmpty()) {
        m_view->setStatusMessage(tr("请先选中要删除的图元（使用选择工具点击图元）"), 4000);
        return;
    }

    QSet<ShapeId> ids;
    for (int i = 0; i < selected.size(); ++i) {
        ids.insert(selected.at(i));
    }

    // 删除会被级联扩展到依赖它的图元，先算出总数以便如实告知用户
    const QSet<ShapeId> cascaded = m_document->descendantIds(ids);
    const int extraCount = cascaded.size() - ids.size();

    RemoveShapesCommand *command = new RemoveShapesCommand(m_document, ids, tr("删除图元"));
    m_undoStack->push(command);

    if (extraCount > 0) {
        m_view->setStatusMessage(
            tr("已删除 %1 个图元，其中 %2 个是依赖被删图元而一并删除的（可用撤销恢复）")
                .arg(command->removedIds().size())
                .arg(extraCount),
            6000);
    } else {
        m_view->setStatusMessage(tr("已删除 %1 个图元").arg(command->removedIds().size()), 4000);
    }
}

void MainController::onShapeListSelectionChanged(const QList<ShapeId> &ids)
{
    // 选中集已经由面板写进文档，这里只负责把结果反映到状态栏与画布。
    // **不回写文档**：面板刚刚写过，回写只会绕成环（面板侧另有防递归，但两边都绕没有意义）。
    m_view->setSelectionCount(ids.size());
    m_view->canvas()->update();
}

void MainController::onShapeListFocusRequested(ShapeId id)
{
    m_view->focusOnShape(id);
    m_view->setStatusMessage(tr("已把视图聚焦到 #%1").arg(id), 3000);
}

void MainController::onShapeListDeleteRequested()
{
    // 完全复用画布的删除路径：同样的级联提示、同一条撤销命令。
    // 复制一份删除逻辑会让两条路径慢慢分叉（例如以后加了"保护某些图元"就只改一处）。
    onDeleteRequested();
}

void MainController::onUndoRequested()
{
    if (!m_undoStack->canUndo()) {
        m_view->setStatusMessage(tr("没有可撤销的操作"), 3000);
        return;
    }
    const QString text = m_undoStack->undoText();
    m_undoStack->undo();
    m_view->setStatusMessage(tr("已撤销：%1").arg(text), 3000);
}

void MainController::onRedoRequested()
{
    if (!m_undoStack->canRedo()) {
        m_view->setStatusMessage(tr("没有可重做的操作"), 3000);
        return;
    }
    const QString text = m_undoStack->redoText();
    m_undoStack->redo();
    m_view->setStatusMessage(tr("已重做：%1").arg(text), 3000);
}

// ---------------------------------------------------------------------------
// 视图
// ---------------------------------------------------------------------------

void MainController::onZoomRequested(double factor, const QPointF &scenePos)
{
    Camera *camera = m_view->canvas()->camera();
    camera->zoomAround(factor, Vec2(scenePos));
    m_view->canvas()->update();
    m_view->setZoomPercent(camera->zoomPercent());
}

void MainController::onPanRequested(const QPointF &sceneDelta)
{
    Camera *camera = m_view->canvas()->camera();
    camera->panBySceneDelta(Vec2(sceneDelta));
    m_view->canvas()->update();
}

void MainController::onZoomInRequested()
{
    CanvasWidget *canvas = m_view->canvas();
    canvas->camera()->zoomAround(kViewZoomStep, canvas->camera()->sceneCenter());
    canvas->update();
    m_view->setZoomPercent(canvas->camera()->zoomPercent());
}

void MainController::onZoomOutRequested()
{
    CanvasWidget *canvas = m_view->canvas();
    canvas->camera()->zoomAround(1.0 / kViewZoomStep, canvas->camera()->sceneCenter());
    canvas->update();
    m_view->setZoomPercent(canvas->camera()->zoomPercent());
}

void MainController::onZoomResetRequested()
{
    CanvasWidget *canvas = m_view->canvas();
    canvas->camera()->reset();
    canvas->update();
    m_view->setZoomPercent(canvas->camera()->zoomPercent());
    m_view->setStatusMessage(tr("视图已恢复默认"), 2000);
}

void MainController::onFitContentRequested()
{
    if (!m_view->canvas()->fitContent()) {
        m_view->setStatusMessage(tr("当前没有可显示的内容"), 3000);
        return;
    }
    m_view->setZoomPercent(m_view->canvas()->camera()->zoomPercent());
    m_view->setStatusMessage(tr("已缩放到全部内容"), 2000);
}

void MainController::onGridVisibilityToggled(bool visible)
{
    m_view->canvas()->setGridVisible(visible);
    m_view->setStatusMessage(visible ? tr("已显示网格") : tr("已隐藏网格"), 2000);
}

void MainController::onAxesVisibilityToggled(bool visible)
{
    m_view->canvas()->setAxesVisible(visible);
    m_view->setStatusMessage(visible ? tr("已显示坐标轴") : tr("已隐藏坐标轴"), 2000);
}

void MainController::onLabelsVisibilityToggled(bool visible)
{
    m_view->canvas()->setLabelsVisible(visible);
    m_view->setStatusMessage(visible ? tr("已显示点名与标注") : tr("已隐藏点名与标注"), 2000);
}

void MainController::onSnapToggled(bool enabled)
{
    m_view->canvas()->setSnapEnabled(enabled);
    m_view->setStatusMessage(enabled
                                 ? tr("已启用吸附对齐：作图输入会吸附到栅格、已有点或曲线上")
                                 : tr("已关闭吸附对齐：作图位置完全跟随鼠标"),
                             4000);
}

void MainController::onGridPrecisionRequested(double minimumStep)
{
    Camera *camera = m_view->canvas()->camera();
    camera->setMinimumGridStep(minimumStep);
    m_view->canvas()->update();
    m_view->setStatusMessage(
        tr("坐标轴最小精度已设为每格 %1 个场景单位（缩放上限 300%）；"
           "这只改变网格与吸附的粗细，不会改动任何已有图形")
            .arg(camera->minimumGridStep(), 0, 'g', 3),
        5000);
}

void MainController::onExportRequested(const QString &path)
{
    if (m_document->shapeCount() == 0) {
        m_view->setStatusMessage(tr("当前没有可导出的内容，请先作图"), 4000);
        return;
    }

    QString targetPath = path;
    if (targetPath.isEmpty()) {
        QString baseName = QFileInfo(m_filePath).completeBaseName();
        if (baseName.isEmpty()) {
            baseName = tr("作图");
        }
        QString selectedFilter;
        targetPath = m_view->promptExportPath(baseName + QStringLiteral(".png"), &selectedFilter);
        if (targetPath.isEmpty()) {
            return;
        }
    }

    // 格式由扩展名决定，因此"另存为 .svg"与"另存为 .png"用同一个入口
    const bool asSvg = targetPath.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive);
    const bool ok = asSvg ? m_exporter->exportSvg(m_view->canvas(), targetPath)
                          : m_exporter->exportPng(m_view->canvas(), targetPath);

    if (!ok) {
        m_view->showError(tr("导出失败"), m_exporter->lastError());
        return;
    }
    m_view->setStatusMessage(
        tr("已导出为 %1（%2）")
            .arg(QFileInfo(targetPath).fileName(), asSvg ? tr("SVG 矢量图") : tr("PNG 位图")),
        5000);
}

void MainController::onAboutRequested()
{
    m_view->showAbout();
}

// ---------------------------------------------------------------------------
// 文件
// ---------------------------------------------------------------------------

bool MainController::confirmDiscardChanges()
{
    if (m_undoStack->isClean()) {
        return true;
    }

    const int answer = m_view->askSaveChanges();
    if (answer == GeoBoardMainWindow::SaveChanges) {
        onSaveRequested(QString());
        // 保存可能被用户取消，因此保存后要再看一次是否仍然"脏"
        return m_undoStack->isClean();
    }
    return answer == GeoBoardMainWindow::DiscardChanges;
}

void MainController::resetForNewDocument(const QString &path)
{
    m_filePath = path;
    m_undoStack->clear();
    cancelPendingDrawing();
    m_view->canvas()->update();
    m_view->setFileName(QFileInfo(path).fileName());
    m_view->setModified(false);
    m_view->setZoomPercent(m_view->canvas()->camera()->zoomPercent());
    // 新建/打开后选中集必然为空，界面也要跟着复位，否则会残留上一次的"已选中 N 个"
    m_view->setSelectionCount(0);
}

void MainController::onNewRequested()
{
    if (!confirmDiscardChanges()) {
        return;
    }

    m_document->beginUpdate();
    m_document->clear();
    m_document->endUpdate();
    m_view->canvas()->camera()->reset();
    resetForNewDocument(QString());
    m_view->setStatusMessage(tr("已新建空白文档"), 3000);
}

void MainController::onOpenRequested(const QString &path)
{
    if (!confirmDiscardChanges()) {
        return;
    }

    QString targetPath = path;
    if (targetPath.isEmpty()) {
        targetPath = m_view->promptOpenPath();
        if (targetPath.isEmpty()) {
            return;
        }
    }

    // JsonStore::load 保证"要么整体成功，要么文档原样保留"，因此不会出现半加载状态
    if (!m_store->load(targetPath, m_document)) {
        m_view->showError(tr("打开失败"), m_store->lastError());
        return;
    }

    m_view->canvas()->camera()->reset();
    resetForNewDocument(targetPath);
    m_view->setStatusMessage(tr("已打开 %1").arg(QFileInfo(targetPath).fileName()), 4000);
}

void MainController::onSaveRequested(const QString &path)
{
    QString targetPath = path;
    if (targetPath.isEmpty()) {
        targetPath = m_filePath;
    }
    if (targetPath.isEmpty()) {
        targetPath = m_view->promptSavePath(QString());
        if (targetPath.isEmpty()) {
            return;
        }
    }

    if (!m_store->save(*m_document, targetPath)) {
        m_view->showError(tr("保存失败"), m_store->lastError());
        return;
    }

    m_filePath = targetPath;
    m_undoStack->setClean();
    m_view->setFileName(QFileInfo(targetPath).fileName());
    m_view->setModified(false);
    m_view->setStatusMessage(tr("已保存到 %1").arg(QFileInfo(targetPath).fileName()), 4000);
}
