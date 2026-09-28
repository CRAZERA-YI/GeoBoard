/**
 * @file    tests/InteractionSmoke.cpp
 * @brief   交互自测：用合成鼠标事件驱动真实窗口，验证作图动作确实产生了图元。
 * @details 存在的理由：上一轮把"点两下"改成"可以按住拖动"之后，用户报告**画圆失灵**。
 *          这类"点了没反应"的缺陷靠读代码很难定位，而手工点击又无法自动化；
 *          于是这里直接往 CanvasWidget 投递 QMouseEvent（与真实鼠标走同一条
 *          mousePressEvent → mouseMoveEvent → mouseReleaseEvent 路径），
 *          再用文档的图元数作为判据。
 *
 *          它覆盖三种操作方式：
 *          1. 点两下（旧方式）；
 *          2. 按住起点拖到终点（上一轮新增）；
 *          3. 双击进入"拉线"状态，再在终点点击一次确认（本轮新增）。
 *
 *          用法：interaction_smoke.exe   全部通过时退出码 0。
 */

#include "controller/MainController.h"
#include "model/GeoDocument.h"
#include "model/Shape.h"
#include "view/CanvasWidget.h"
#include "view/GeoBoardMainWindow.h"
#include "view/ShapeListPanel.h"

#include <QApplication>
#include <QDockWidget>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QThread>
#include <QTreeWidget>
#include <QUndoStack>

#include <cmath>
#include <cstdio>

namespace {

/** @brief 输出一行文本。 */
void print(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    std::fputs(utf8.constData(), stdout);
    std::fputs("\n", stdout);
}

/** @brief 极简断言计数器。 */
class Checker
{
public:
    Checker() : m_passed(0), m_failed(0) {}

    /**
     * @brief 记录一条断言。
     * @param condition 是否成立。
     * @param description 描述。
     * @param detail 失败补充信息。
     */
    void check(bool condition, const QString &description, const QString &detail = QString())
    {
        if (condition) {
            ++m_passed;
            return;
        }
        ++m_failed;
        print(QStringLiteral("[FAIL] %1").arg(description));
        if (!detail.isEmpty()) {
            print(QStringLiteral("       %1").arg(detail));
        }
    }

    /** @brief 返回通过数。 */
    int passes() const { return m_passed; }

    /** @brief 返回失败数。 */
    int failures() const { return m_failed; }

private:
    int m_passed;
    int m_failed;
};

/**
 * @brief 让测试等待若干毫秒，模拟"按住"的持续时间。
 * @param ms 毫秒数。
 * @note  画布把"按下后 120ms 内松键"判为单击而不是拖动（这条判据用来把
 *        双击起笔时紧跟按下的松键与真正的拖动区分开）。合成事件是瞬间发生的，
 *        因此这里必须显式等待，否则测出来的不是真实鼠标的行为。
 */
void holdFor(int ms)
{
    QThread::msleep(static_cast<unsigned long>(ms));
}

/**
 * @brief 向窗口投递一个合成鼠标事件。
 * @param canvas 目标画布。
 * @param type 事件类型。
 * @param pos 画布内坐标（屏幕/窗口坐标，不是场景坐标）。
 * @param button 按键。
 */
void sendMouse(CanvasWidget *canvas, QEvent::Type type, const QPoint &pos, Qt::MouseButton button)
{
    const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease) ? Qt::NoButton : button;
    QMouseEvent event(type, QPointF(pos), QPointF(canvas->mapToGlobal(pos)),
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(canvas, &event);
}

/**
 * @brief 模拟"点两下"：按下并松开两次（同一个位置各一次）。
 * @param canvas 目标画布。
 * @param first 第一次点击的位置（画布坐标）。
 * @param second 第二次点击的位置（画布坐标）。
 */
void clickTwice(CanvasWidget *canvas, const QPoint &first, const QPoint &second)
{
    sendMouse(canvas, QEvent::MouseButtonPress, first, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, first, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonPress, second, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, second, Qt::LeftButton);
}

/**
 * @brief 模拟"按住拖动"：按下 → 多次移动 → 松开。
 * @param canvas 目标画布。
 * @param from 起点（画布坐标）。
 * @param to 终点（画布坐标）。
 */
void dragFromTo(CanvasWidget *canvas, const QPoint &from, const QPoint &to)
{
    sendMouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton);
    holdFor(150); // 真实拖动必然持续一段时间
    // 中间多走几步，模拟真实拖动（阈值判断需要位移）
    for (int step = 1; step <= 5; ++step) {
        const QPoint mid(from.x() + (to.x() - from.x()) * step / 5,
                         from.y() + (to.y() - from.y()) * step / 5);
        sendMouse(canvas, QEvent::MouseMove, mid, Qt::NoButton);
        holdFor(20);
    }
    sendMouse(canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton);
}

/**
 * @brief 模拟"双击起笔 → 拖动塑形 → 松键定点"（用户要求的划线方式）。
 * @param canvas 目标画布。
 * @param start 起笔位置（画布坐标）。
 * @param end 终点位置（画布坐标）。
 */
void doubleClickThenDrag(CanvasWidget *canvas, const QPoint &start, const QPoint &end)
{
    // 必须发送**完整的**双击序列：真实 Qt 依次投递
    //   按下① → 松开① → 按下② → 双击 → 松开②
    // 只发一个双击事件时，画布里"识别双击前半段"的逻辑拿不到与实机一致的状态，
    // 测试就会与真实行为分叉（本用例最初正是因此失败）。
    sendMouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, start, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonDblClick, start, Qt::LeftButton);
    sendMouse(canvas, QEvent::MouseButtonRelease, start, Qt::LeftButton);
    // 起笔之后拖动塑形，松键即确定终点
    sendMouse(canvas, QEvent::MouseButtonPress, start, Qt::LeftButton);
    holdFor(150);
    for (int step = 1; step <= 5; ++step) {
        const QPoint mid(start.x() + (end.x() - start.x()) * step / 5,
                         start.y() + (end.y() - start.y()) * step / 5);
        sendMouse(canvas, QEvent::MouseMove, mid, Qt::NoButton);
        holdFor(20);
    }
    sendMouse(canvas, QEvent::MouseButtonRelease, end, Qt::LeftButton);
}

/**
 * @brief 判断一个数是否落在整数格点上（容差内）。
 * @param value 待判断的数。
 * @param tolerance 容差。
 * @return 落在整数格点上返回 true。
 * @note  刻意不用 `std::fabs` / `std::floor`：MSVC 的 `<cmath>` 会把它们带成宏，
 *        写在表达式里会被展开成 `std::(x)` 之类的非法形式，报错还指向"项不会计算为
 *        接受 0 个参数的函数"，非常难懂。这里用最朴素的写法把它挡在测试代码之外。
 */
bool isOnIntegerGrid(double value, double tolerance = 1e-6)
{
    const double nearest = static_cast<double>(static_cast<long long>(value >= 0.0
                                                                          ? value + 0.5
                                                                          : value - 0.5));
    double diff = value - nearest;
    if (diff < 0.0) {
        diff = -diff;
    }
    return diff <= tolerance;
}

/** @brief 判断两个浮点数是否足够接近。 */
bool isCloseToDouble(double a, double b, double tolerance = 1e-9)
{
    double diff = a - b;
    if (diff < 0.0) {
        diff = -diff;
    }
    return diff <= tolerance;
}

/** @brief 判断两个点是否足够接近（用于断言"位置未被改动"）。 */
bool isCloseToPoint(const QPointF &a, const QPointF &b, double tolerance = 1e-9)
{
    double dx = a.x() - b.x();
    double dy = a.y() - b.y();
    if (dx < 0.0) {
        dx = -dx;
    }
    if (dy < 0.0) {
        dy = -dy;
    }
    return dx <= tolerance && dy <= tolerance;
}

/** @brief 统计文档中某种类型的图元个数。 */
int countShapesOfType(const GeoDocument *document, ShapeType type)
{
    int count = 0;
    const QList<Shape *> shapes = document->shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes.at(i)->type() == type) {
            ++count;
        }
    }
    return count;
}

/**
 * @brief 统计文档中"有效"的某种类型图元个数。
 * @note  只看类型不看有效性会把"创建了但退化的图元"也算进去，
 *        而用户看到的恰恰是"无效＝没画出来"，所以两种都要查。
 */
int countValidShapesOfType(const GeoDocument *document, ShapeType type)
{
    int count = 0;
    const QList<Shape *> shapes = document->shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes.at(i)->type() == type && shapes.at(i)->isValid()) {
            ++count;
        }
    }
    return count;
}

/// 便捷：把动作名与"新画出的图元数"一起打印，便于定位是哪一步没生效
void report(const QString &label, int before, int after)
{
    print(QStringLiteral("   %1：图元数 %2 → %3").arg(label).arg(before).arg(after));
}

/** @brief 类型名（用于诊断输出）。 */
QString typeName(ShapeType type)
{
    switch (type) {
    case ShapeType::Point: return QStringLiteral("Point");
    case ShapeType::Line: return QStringLiteral("Line");
    case ShapeType::Circle: return QStringLiteral("Circle");
    case ShapeType::Arc: return QStringLiteral("Arc");
    case ShapeType::Polygon: return QStringLiteral("Polygon");
    case ShapeType::PerpendicularLine: return QStringLiteral("Perp");
    case ShapeType::ParallelLine: return QStringLiteral("Para");
    case ShapeType::AngleBisector: return QStringLiteral("Bisect");
    case ShapeType::MidPoint: return QStringLiteral("Mid");
    case ShapeType::IntersectionPoint: return QStringLiteral("Inter");
    case ShapeType::Measurement: return QStringLiteral("Measure");
    }
    return QStringLiteral("?");
}

/** @brief 打印文档里每个图元的类型与有效性，用于定位"创建了但无效"的情形。 */
void dumpShapes(const GeoDocument *document, const QString &label)
{
    QStringList parts;
    const QList<Shape *> shapes = document->shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        Shape *shape = shapes.at(i);
        QString extra;
        if (shape->type() == ShapeType::Line) {
            const LineData &line = static_cast<const LineShape *>(shape)->lineData();
            extra = QStringLiteral("[origin=(%1,%2) dir=(%3,%4) defined=%5]")
                        .arg(line.origin.x).arg(line.origin.y)
                        .arg(line.direction.x).arg(line.direction.y)
                        .arg(line.isDefined);
        } else if (shape->type() == ShapeType::Point) {
            const PointShape *point = static_cast<const PointShape *>(shape);
            extra = QStringLiteral("[(%1,%2) kind=%3 host=%4]")
                        .arg(point->position().x).arg(point->position().y)
                        .arg(point->kind() == PointKind::Free ? 1 : 2)
                        .arg(point->hostId());
        } else if (shape->type() == ShapeType::Circle) {
            const CircleData &circle = static_cast<const CircleShape *>(shape)->circleData();
            extra = QStringLiteral("[r=%1 defined=%2]").arg(circle.radius).arg(circle.isDefined);
        }
        parts << QStringLiteral("#%1 %2%3%4").arg(shape->id())
                     .arg(typeName(shape->type()))
                     .arg(shape->isValid() ? QString() : QStringLiteral("(无效)"))
                     .arg(extra);
    }
    print(QStringLiteral("   [dump] %1: %2").arg(label, parts.join(QStringLiteral(", "))));
}

} // namespace

/**
 * @brief 入口。
 * @param argc 参数个数。
 * @param argv 参数数组。
 * @return 全部通过返回 0，否则返回 1。
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    print(QStringLiteral("==== GeoBoard 交互自测（合成鼠标事件）===="));
    Checker checker;

    GeoBoardMainWindow window;
    MainController controller(&window);
    window.resize(900, 640);
    window.show();
    controller.start();

    CanvasWidget *canvas = window.canvas();
    GeoDocument *document = canvas->document();
    checker.check(document != nullptr, QStringLiteral("画布应已绑定文档"));
    if (document == nullptr) {
        return 1;
    }

    // 诊断：先把画布几何与坐标换算打出来。合成事件用的是画布内坐标，
    // 如果画布尺寸与预期不符，点击位置就会落到窗口之外而完全没有反应。
    print(QStringLiteral("   画布尺寸=%1x%2 缩放=%3% 中心场景=(%4,%5)")
              .arg(canvas->width()).arg(canvas->height())
              .arg(canvas->camera()->zoomPercent())
              .arg(canvas->camera()->sceneCenter().x)
              .arg(canvas->camera()->sceneCenter().y));

    // 画布中心附近的若干位置（屏幕坐标），彼此拉开足够距离避免吸附
    const QPoint center = canvas->rect().center();
    const QPoint right = center + QPoint(160, 0);
    const QPoint up = center + QPoint(0, -140);
    const QPoint down = center + QPoint(0, 140);
    const QPoint leftUp = center + QPoint(-170, -120);

    // ---- 1. 圆：按住拖动 ----
    {
        const int before = document->shapeCount();
        controller.onToolChangeRequested(ToolId::Circle);
        dragFromTo(canvas, center, right);
        const int after = document->shapeCount();
        report(QStringLiteral("圆（按住拖动）"), before, after);
        dumpShapes(document, QStringLiteral("圆（按住拖动）后"));
        checker.check(countValidShapesOfType(document, ShapeType::Circle) == 1,
                     QStringLiteral("按住拖动应能画出一个有效的圆"),
                     QStringLiteral("有效圆个数 = %1")
                         .arg(countValidShapesOfType(document, ShapeType::Circle)));
    }

    // ---- 2. 圆：点两下（旧方式必须仍然可用）----
    {
        const int before = countValidShapesOfType(document, ShapeType::Circle);
        controller.onToolChangeRequested(ToolId::Circle);
        clickTwice(canvas, up, up + QPoint(120, 60));
        const int after = countValidShapesOfType(document, ShapeType::Circle);
        checker.check(after == before + 1,
                     QStringLiteral("点两下也应能画出圆（旧方式不能被破坏）"),
                     QStringLiteral("有效圆个数 %1 → %2").arg(before).arg(after));
    }

    // ---- 3. 线段：按住拖动 ----
    {
        const int before = countValidShapesOfType(document, ShapeType::Line);
        controller.onToolChangeRequested(ToolId::Segment);
        dragFromTo(canvas, down, down + QPoint(150, -40));
        const int after = countValidShapesOfType(document, ShapeType::Line);
        checker.check(after == before + 1,
                     QStringLiteral("按住拖动应能画出一条有效的线段"),
                     QStringLiteral("有效线个数 %1 → %2").arg(before).arg(after));
    }

    // ---- 4. 线段：点两下 ----
    {
        const int before = countValidShapesOfType(document, ShapeType::Line);
        controller.onToolChangeRequested(ToolId::Segment);
        clickTwice(canvas, leftUp, leftUp + QPoint(130, 40));
        const int after = countValidShapesOfType(document, ShapeType::Line);
        checker.check(after == before + 1,
                     QStringLiteral("点两下也应能画出线段"),
                     QStringLiteral("有效线个数 %1 → %2").arg(before).arg(after));
    }

    // ---- 5. 线段：双击起笔 → 拖动塑形 → 松键定点（用户要求的划线方式）----
    {
        const int before = countValidShapesOfType(document, ShapeType::Line);
        controller.onToolChangeRequested(ToolId::Segment);
        doubleClickThenDrag(canvas, up + QPoint(0, 90), up + QPoint(140, 90));
        const int after = countValidShapesOfType(document, ShapeType::Line);
        // 先断言再 dump：dump 自身会读文档，若放在断言之前会改变计数口径
        checker.check(after == before + 1,
                     QStringLiteral("双击起笔后拖动应能完成线段"),
                     QStringLiteral("有效线个数 %1 → %2").arg(before).arg(after));
        dumpShapes(document, QStringLiteral("双击起笔画线后"));
    }

    // ---- 6. 圆：双击起笔 → 拖动塑形 ----
    {
        const int before = countValidShapesOfType(document, ShapeType::Circle);
        controller.onToolChangeRequested(ToolId::Circle);
        doubleClickThenDrag(canvas, leftUp + QPoint(-60, 0), leftUp + QPoint(-10, 40));
        const int after = countValidShapesOfType(document, ShapeType::Circle);
        checker.check(after == before + 1,
                     QStringLiteral("双击起笔后拖动也应能完成圆"),
                     QStringLiteral("有效圆个数 %1 → %2").arg(before).arg(after));
        dumpShapes(document, QStringLiteral("双击起笔画圆后"));
    }

    // ---- 7. 圆弧（圆心 + 两点）：三次点击 ----
    {
        const int before = countValidShapesOfType(document, ShapeType::Arc);
        controller.onToolChangeRequested(ToolId::ArcCenter);
        const QPoint arcCenter = center + QPoint(-40, 200);
        const QPoint arcRadius = arcCenter + QPoint(120, 0);
        const QPoint arcOnCurve = arcCenter + QPoint(0, -120);

        sendMouse(canvas, QEvent::MouseButtonPress, arcCenter, Qt::LeftButton);
        sendMouse(canvas, QEvent::MouseButtonRelease, arcCenter, Qt::LeftButton);
        sendMouse(canvas, QEvent::MouseButtonPress, arcRadius, Qt::LeftButton);
        sendMouse(canvas, QEvent::MouseButtonRelease, arcRadius, Qt::LeftButton);
        sendMouse(canvas, QEvent::MouseButtonPress, arcOnCurve, Qt::LeftButton);
        sendMouse(canvas, QEvent::MouseButtonRelease, arcOnCurve, Qt::LeftButton);

        const int after = countValidShapesOfType(document, ShapeType::Arc);
        checker.check(after == before + 1,
                      QStringLiteral("圆心 + 两点应能画出圆弧"),
                      QStringLiteral("有效弧个数 %1 → %2").arg(before).arg(after));

        bool foundCenterKind = false;
        const QList<Shape *> shapes = document->shapesInOrder();
        for (int i = 0; i < shapes.size(); ++i) {
            if (shapes.at(i)->type() == ShapeType::Arc
                && static_cast<const ArcShape *>(shapes.at(i))->kind() == ArcKind::CenterTwoPoint) {
                foundCenterKind = true;
            }
        }
        checker.check(foundCenterKind, QStringLiteral("新画的弧应标记为圆心式构造"));
    }

    // ---- 8. 吸附对齐：关闭时跟随鼠标，开启时落到栅格 ----
    {
        Camera *camera = canvas->camera();
        camera->reset(); // 默认视图：原点居中、每场景单位 40 像素、栅格步长 1

        const QPoint offGrid = center + QPoint(37, 23); // 故意偏离栅格
        auto lastPointPosition = [document]() -> Vec2 {
            ShapeId lastId = kInvalidShapeId;
            const QList<Shape *> all = document->shapesInOrder();
            for (int i = 0; i < all.size(); ++i) {
                if (all.at(i)->type() == ShapeType::Point) {
                    lastId = all.at(i)->id();
                }
            }
            if (lastId == kInvalidShapeId) {
                return Vec2(0.0, 0.0);
            }
            return static_cast<const PointShape *>(document->shape(lastId))->position();
        };

        canvas->setSnapEnabled(false);
        controller.onToolChangeRequested(ToolId::Point);
        sendMouse(canvas, QEvent::MouseButtonPress, offGrid, Qt::LeftButton);
        sendMouse(canvas, QEvent::MouseButtonRelease, offGrid, Qt::LeftButton);
        const Vec2 noSnap = lastPointPosition();
        const bool looksFree = !isOnIntegerGrid(noSnap.x) || !isOnIntegerGrid(noSnap.y);
        checker.check(looksFree,
                      QStringLiteral("关闭吸附时作图点应落在鼠标的真实位置"),
                      QStringLiteral("实际 (%1,%2)").arg(noSnap.x).arg(noSnap.y));

        // 吸附行为**直接对 API 断言**，而不是"点一下再去猜哪个点是新建的"。
        // 后者要假设"最后一个点就是刚建的点"，而吸附可能命中已有点或曲线上的附着点，
        // 断言会随场景内容漂移——我在这上面白费过两轮。
        canvas->setSnapEnabled(true);
        const double gridStep = camera->gridStep();

        // 不再用"假设的倍率"推算吸附分辨率（我在这上面算错过两次）：改成**扫描**出
        // 一个确实会吸附的位置与一个确实不会吸附的位置，再分别断言它们的行为。
        // 这样测试不依赖阈值常量，只检查"两类位置确实被区别对待"。
        QPointF snappingScene;
        QPointF freeScene;
        bool foundSnapping = false;
        bool foundFree = false;
        for (int i = 1; i <= 60 && !(foundSnapping && foundFree); ++i) {
            for (int j = 1; j <= 60 && !(foundSnapping && foundFree); ++j) {
                const QPointF probe(i * 0.05, j * 0.05); // 0.05 为步长细扫第一象限
                const CanvasWidget::SnapResult candidate = canvas->snapScenePos(probe);
                if (candidate.hostCurveId != kInvalidShapeId) {
                    continue; // 只要"纯栅格吸附"的位置，曲线吸附另有用例
                }
                if (candidate.snapped && !foundSnapping) {
                    snappingScene = probe;
                    foundSnapping = true;
                } else if (!candidate.snapped && !foundFree) {
                    freeScene = probe;
                    foundFree = true;
                }
            }
        }
        checker.check(foundSnapping,
                      QStringLiteral("应存在会被吸附到栅格的输入位置（栅格吸附必须生效）"));
        checker.check(foundFree,
                      QStringLiteral("应存在不会被吸附的输入位置（必须有自由作图的空间）"));
        checker.check(foundSnapping && foundFree && !isCloseToPoint(snappingScene, freeScene),
                      QStringLiteral("两类位置必须真的不同，否则上面的断言没有意义"));

        if (foundSnapping && foundFree) {
            const CanvasWidget::SnapResult againSnap = canvas->snapScenePos(snappingScene);
            const CanvasWidget::SnapResult againFree = canvas->snapScenePos(freeScene);
            const Vec2 snappedPosition(againSnap.position);
            checker.check(againSnap.snapped && againSnap.hostCurveId == kInvalidShapeId
                              && isOnIntegerGrid(snappedPosition.x, gridStep * 0.01),
                          QStringLiteral("会吸附的位置应稳定地吸到格点上"),
                          QStringLiteral("输入 (%1,%2) → (%3,%4)，栅格步长 %5")
                              .arg(snappingScene.x()).arg(snappingScene.y())
                              .arg(snappedPosition.x).arg(snappedPosition.y)
                              .arg(gridStep, 0, 'f', 3));
            checker.check(!againFree.snapped && isCloseToPoint(againFree.position, freeScene),
                          QStringLiteral("自由位置应原样保留，不被悄悄挪动"),
                          QStringLiteral("输入 (%1,%2) → (%3,%4)")
                              .arg(freeScene.x()).arg(freeScene.y())
                              .arg(againFree.position.x()).arg(againFree.position.y()));
        }

        // 关闭吸附后 API 必须原样返回，不得私自吸附
        canvas->setSnapEnabled(false);
        const CanvasWidget::SnapResult disabled =
            canvas->snapScenePos(foundSnapping ? snappingScene : QPointF(1.0, 1.0));
        checker.check(!disabled.snapped, QStringLiteral("关闭吸附时 API 应原样返回输入位置"));
        canvas->setSnapEnabled(true);

        // 吸附到曲线上时要报告宿主曲线 id —— 控制器据此创建**附着点**（结构性约束）。
        //
        // 用一个**专门造的线段**来验证，而不是在已有场景里碰运气：
        //   ① 探测点必须避开两端（那里会先命中"已有点"，拿到的是点而非曲线宿主，
        //      这是优先级设计使然，不是缺陷）；
        //   ② 探测点必须落在命中容差内——默认命中容差是 8 像素，在当前缩放下
        //      只有 0.2 场景单位，所以偏移量要很小（先前取 0.3 步长向外扫，全部落空）。
        canvas->setActiveTool(ToolId::PointOnObject);
        const ShapeId snapSegmentFirstId = document->allocateShapeId();
        const ShapeId snapSegmentSecondId = document->allocateShapeId();
        document->addShape(new PointShape(snapSegmentFirstId, PointKind::Free, Vec2(-6.0, -4.0)));
        document->addShape(new PointShape(snapSegmentSecondId, PointKind::Free, Vec2(-2.0, -4.0)));
        const ShapeId snapSegmentId = document->allocateShapeId();
        document->addShape(new LineShape(snapSegmentId, LineKind::Segment, snapSegmentFirstId,
                                         snapSegmentSecondId));

        bool reportedHost = false;
        bool hostStillValid = false;
        int hostProbeCount = 0;
        for (int step = 1; step <= 6 && !reportedHost; ++step) {
            const double t = static_cast<double>(step) / 7.0; // 取在线段内部
            const double x = -6.0 + 4.0 * t;
            for (int k = 0; k < 4 && !reportedHost; ++k) {
                const double offset = 0.02 * k; // 逐步贴近线段（0 / 0.02 / 0.04 / 0.06）
                ++hostProbeCount;
                const CanvasWidget::SnapResult onCurve =
                    canvas->snapScenePos(QPointF(x, -4.0 + offset));
                if (onCurve.hostCurveId != kInvalidShapeId) {
                    reportedHost = true;
                    hostStillValid = (document->curve(onCurve.hostCurveId) != nullptr);
                }
            }
        }
        checker.check(reportedHost,
                      QStringLiteral("在线段附近吸附时应报告宿主曲线 id（用于创建附着点）"),
                      QStringLiteral("探测 %1 个位置").arg(hostProbeCount));
        checker.check(hostStillValid,
                      QStringLiteral("报告出来的宿主曲线 id 必须能在文档里查到"));

        canvas->setSnapEnabled(true);
    }

    // ---- 10. 对象列表面板：停靠窗口装配是否正确 ----
    {
        GeoBoardMainWindow window;
        ShapeListPanel *panel = window.shapeListPanel();
        checker.check(panel != nullptr, QStringLiteral("主窗口应提供对象列表面板"));

        QDockWidget *dock =
            (panel != nullptr) ? qobject_cast<QDockWidget *>(panel->parentWidget()) : nullptr;
        checker.check(dock != nullptr,
                      QStringLiteral("面板应被放进 QDockWidget（可停靠、可移动、可关闭）"));
        if (dock != nullptr) {
            checker.check(dock->widget() == panel,
                          QStringLiteral("停靠窗口里装的应当就是这个面板"));
            checker.check(dock->allowedAreas().testFlag(Qt::LeftDockWidgetArea)
                              && dock->allowedAreas().testFlag(Qt::RightDockWidgetArea),
                          QStringLiteral("对象列表应能停靠在左侧或右侧"));
            checker.check(dock->features().testFlag(QDockWidget::DockWidgetClosable),
                          QStringLiteral("对象列表应可关闭（关闭后从视图菜单再打开）"));
            checker.check(dock->features().testFlag(QDockWidget::DockWidgetMovable)
                              && dock->features().testFlag(QDockWidget::DockWidgetFloatable),
                          QStringLiteral("对象列表应可移动、可浮动"));

            // 断言"隐藏状态"，而不是 isVisible()：测试里的窗口从未 show()，
            // 此时 isVisible() 恒为 false，用它断言等于什么都没测。
            // setShapeListVisible 的真实效果在实机由 isShapeListVisible() 体现。
            window.setShapeListVisible(false);
            checker.check(dock->isHidden(), QStringLiteral("可以隐藏对象列表"));


            window.setShapeListVisible(true);
            checker.check(!dock->isHidden(), QStringLiteral("可以重新显示对象列表"));
        }

        // 绑定文档后应能列出图元：用独立文档验证，避免受前面场景影响
        GeoDocument listDocument;
        const ShapeId listA = listDocument.allocateShapeId();
        const ShapeId listB = listDocument.allocateShapeId();
        listDocument.addShape(new PointShape(listA, PointKind::Free, Vec2(0.0, 0.0)));
        listDocument.addShape(new PointShape(listB, PointKind::Free, Vec2(3.0, 0.0)));
        const ShapeId listSegment = listDocument.allocateShapeId();
        listDocument.addShape(new LineShape(listSegment, LineKind::Segment, listA, listB));

        if (panel != nullptr) {
            panel->setDocument(&listDocument);
            panel->refresh();
            QTreeWidget *tree = nullptr;
            const QList<QWidget *> children = panel->findChildren<QWidget *>();
            for (int i = 0; i < children.size(); ++i) {
                if (tree == nullptr) {
                    tree = qobject_cast<QTreeWidget *>(children.at(i));
                }
            }
            checker.check(tree != nullptr, QStringLiteral("对象列表面板内部应有清单控件"));
            if (tree != nullptr) {
                checker.check(tree->topLevelItemCount() == 3,
                              QStringLiteral("面板应列出文档里的全部 3 个图元"),
                              QStringLiteral("实际列出 %1 行").arg(tree->topLevelItemCount()));

                // 契约（见 ShapeListPanel 头文件）：文档选中集变化 → **清单高亮对应行**。
                // 注意面板在"文档 → 列表"这个方向上**刻意不发** selectionChanged
                // （否则会与"列表 → 文档"绕成环），所以这里断言的是清单的选中状态，
                // 不是信号——我最初按信号断言，是误解了自己定的契约。
                QSet<ShapeId> selection;
                selection.insert(listSegment);
                listDocument.setSelection(selection);
                const QList<QTreeWidgetItem *> selectedItems = tree->selectedItems();
                bool listsSegment = false;
                for (int i = 0; i < selectedItems.size(); ++i) {
                    if (selectedItems.at(i)->data(0, Qt::UserRole).toInt()
                        == static_cast<int>(listSegment)) {
                        listsSegment = true;
                    }
                }
                checker.check(selectedItems.size() == 1 && listsSegment,
                              QStringLiteral("文档选中集变化应让清单高亮对应行"),
                              QStringLiteral("清单选中 %1 行").arg(selectedItems.size()));

                // 文档里删掉图元后，清单行也要跟着消失（级联删除场景）
                QSet<ShapeId> toRemove;
                toRemove.insert(listSegment);
                listDocument.removeShapes(toRemove);
                panel->refresh();
                checker.check(tree->topLevelItemCount() == 2,
                              QStringLiteral("图元被删除后清单行应同步移除"),
                              QStringLiteral("实际剩下 %1 行").arg(tree->topLevelItemCount()));
            }
        }
    }

    // ---- 11. 圆心式圆弧：中途换工具必须能干净地重新开始 ----
    // 这是"圆心两点画圆弧不正常"最可能的复发点：若第三步借用别的工具的成员
    // （或换工具时不复位），用户点到一半切走再切回来，就会从错误的一步接着走。
    {
        GeoBoardMainWindow arcWindow;
        MainController arcController(&arcWindow);
        arcController.start();
        CanvasWidget *arcCanvas = arcWindow.canvas();
        arcCanvas->resize(900, 620);
        GeoDocument *arcDocument = arcCanvas->document();
        const QPoint origin(arcCanvas->width() / 2, arcCanvas->height() / 2);

        auto arcCount = [arcDocument]() -> int {
            int count = 0;
            const QList<Shape *> all = arcDocument->shapesInOrder();
            for (int i = 0; i < all.size(); ++i) {
                if (all.at(i)->type() == ShapeType::Arc && all.at(i)->isValid()) {
                    ++count;
                }
            }
            return count;
        };

        arcController.onToolChangeRequested(ToolId::ArcCenter);
        checker.check(arcController.currentTool() == ToolId::ArcCenter,
                      QStringLiteral("应能切换到圆心式圆弧工具"));

        // 只点第一下（定圆心），随后切到点工具再切回来
        sendMouse(arcCanvas, QEvent::MouseButtonPress, origin + QPoint(-100, 0), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonRelease, origin + QPoint(-100, 0), Qt::LeftButton);
        arcController.onToolChangeRequested(ToolId::Point);
        arcController.onToolChangeRequested(ToolId::ArcCenter);

        // 重新走完整三步：必须能画出弧（说明状态已干净复位）
        sendMouse(arcCanvas, QEvent::MouseButtonPress, origin + QPoint(-100, 0), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonRelease, origin + QPoint(-100, 0), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonPress, origin + QPoint(20, 0), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonRelease, origin + QPoint(20, 0), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonPress, origin + QPoint(-100, -120), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonRelease, origin + QPoint(-100, -120), Qt::LeftButton);
        checker.check(arcCount() == 1,
                      QStringLiteral("中途换工具后重新画，圆心式圆弧仍应正常完成"),
                      QStringLiteral("有效弧个数 %1").arg(arcCount()));

        // 再画一条，并多按一下：第四下不应又生成一条弧
        arcController.onToolChangeRequested(ToolId::ArcCenter);
        sendMouse(arcCanvas, QEvent::MouseButtonPress, origin + QPoint(100, 100), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonRelease, origin + QPoint(100, 100), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonPress, origin + QPoint(170, 100), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonRelease, origin + QPoint(170, 100), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonPress, origin + QPoint(100, 30), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonRelease, origin + QPoint(100, 30), Qt::LeftButton);
        checker.check(arcCount() == 2,
                      QStringLiteral("第二次作圆弧也应正常完成"),
                      QStringLiteral("有效弧个数 %1").arg(arcCount()));
        sendMouse(arcCanvas, QEvent::MouseButtonPress, origin + QPoint(130, 65), Qt::LeftButton);
        sendMouse(arcCanvas, QEvent::MouseButtonRelease, origin + QPoint(130, 65), Qt::LeftButton);
        checker.check(arcCount() == 2,
                      QStringLiteral("完成之后多按一下不应再生成一条弧"),
                      QStringLiteral("有效弧个数 %1").arg(arcCount()));
    }

    // ---- 12b. 圆心式圆弧的分步预览（这正是"不能正常绘制"的两个根因所在）----
    // 两个根因，各一条断言：
    //   ① 画布里的"能否拖动作图"判断漏了 ArcCenter，于是鼠标移动永远走不到
    //      圆弧预览分支 —— 用户点了圆心之后画布上毫无反应；
    //   ② 松键收尾曾用"松开位置离按下位置远不远"判断，导致按下与松开之间
    //      鼠标位置发生过变化时（快速点两下、手抖）就把图形建在错误的终点上。
    {
        GeoBoardMainWindow previewWindow;
        MainController previewController(&previewWindow);
        previewController.start();
        CanvasWidget *previewCanvas = previewWindow.canvas();
        previewCanvas->resize(900, 620);
        const QPoint origin(previewCanvas->width() / 2, previewCanvas->height() / 2);

        previewController.onToolChangeRequested(ToolId::ArcCenter);
        checker.check(isDragConstructionTool(ToolId::ArcCenter),
                      QStringLiteral("圆心式圆弧必须被判定为支持拖动作图（画布据此显示预览）"));

        // 第一步：点圆心，然后把鼠标移开 → 必须出现弧预览
        sendMouse(previewCanvas, QEvent::MouseButtonPress, origin + QPoint(-150, 0), Qt::LeftButton);
        sendMouse(previewCanvas, QEvent::MouseButtonRelease, origin + QPoint(-150, 0), Qt::LeftButton);
        checker.check(!previewCanvas->hasArcPreview(),
                      QStringLiteral("刚点完圆心、鼠标还没动时不应有预览（避免误导）"));
        sendMouse(previewCanvas, QEvent::MouseMove, origin + QPoint(50, 0), Qt::NoButton);
        checker.check(previewCanvas->hasArcPreview(),
                      QStringLiteral("点了圆心之后移动鼠标，画布必须显示半径预览（否则用户以为没反应）"));

        // 第二步：点半径点，把鼠标移到"弧上点"候选位置 → 仍要有预览
        sendMouse(previewCanvas, QEvent::MouseButtonPress, origin + QPoint(50, 0), Qt::LeftButton);
        sendMouse(previewCanvas, QEvent::MouseButtonRelease, origin + QPoint(50, 0), Qt::LeftButton);
        sendMouse(previewCanvas, QEvent::MouseMove, origin + QPoint(-150, -150), Qt::NoButton);
        checker.check(previewCanvas->hasArcPreview(),
                      QStringLiteral("半径确定后移动鼠标仍应显示弧预览"));

        // 关键回归：完成第三次点击（第三次点击的"按下-松开"之间夹了一次拖动）
        // 之后，工作流必须结束（工具切回选择），而不是继续等着第四次点。
        sendMouse(previewCanvas, QEvent::MouseButtonPress, origin + QPoint(-150, -150), Qt::LeftButton);
        sendMouse(previewCanvas, QEvent::MouseMove, origin + QPoint(-150, -150), Qt::NoButton);
        sendMouse(previewCanvas, QEvent::MouseButtonRelease, origin + QPoint(-150, -150), Qt::LeftButton);
        checker.check(previewController.currentTool() == ToolId::Select,
                      QStringLiteral("完成第三次点击后应结束本次圆弧（回到选择工具）"),
                      QStringLiteral("实际工具 %1").arg(static_cast<int>(previewController.currentTool())));

        // 单击（按下与松开之间没有移动事件）绝不能被当成拖动收尾。
        // 这条守住"第一下点击就把弧完成在半径点"那个回归。
        previewController.onToolChangeRequested(ToolId::ArcCenter);
        GeoDocument *previewDocument = previewCanvas->document();
        int arcsBefore = 0;
        {
            const QList<Shape *> all = previewDocument->shapesInOrder();
            for (int i = 0; i < all.size(); ++i) {
                if (all.at(i)->type() == ShapeType::Arc) {
                    ++arcsBefore;
                }
            }
        }
        // 模拟"按下在前一个位置、松开在当前位置"的错位点击（快速点两下的常见现象）
        sendMouse(previewCanvas, QEvent::MouseButtonPress, origin + QPoint(0, 200), Qt::LeftButton);
        sendMouse(previewCanvas, QEvent::MouseButtonRelease, origin + QPoint(40, 220), Qt::LeftButton);
        int arcsAfter = 0;
        {
            const QList<Shape *> all = previewDocument->shapesInOrder();
            for (int i = 0; i < all.size(); ++i) {
                if (all.at(i)->type() == ShapeType::Arc) {
                    ++arcsAfter;
                }
            }
        }
        checker.check(arcsAfter == arcsBefore,
                      QStringLiteral("只有一次点击（中间无移动）时不得生成圆弧——"
                                     "否则会以错误的终点直接建出图形"),
                      QStringLiteral("弧个数 %1 → %2").arg(arcsBefore).arg(arcsAfter));
        checker.check(previewController.currentTool() == ToolId::ArcCenter,
                      QStringLiteral("单击之后作图应仍在进行中（等待后续点击）"),
                      QStringLiteral("实际工具 %1").arg(static_cast<int>(previewController.currentTool())));

        // 善后：清掉预览，避免影响后续用例
        previewController.onEscapeRequested();
    }

    // ---- 12. 坐标轴最小精度选项与缩放上限 ----
    {
        GeoBoardMainWindow precisionWindow;
        MainController precisionController(&precisionWindow);
        precisionController.start();
        CanvasWidget *precisionCanvas = precisionWindow.canvas();
        precisionCanvas->resize(900, 620);
        Camera *precisionCamera = precisionCanvas->camera();

        checker.check(isCloseToDouble(precisionCamera->minimumGridStep(), 1.0),
                      QStringLiteral("默认最小精度应为每格 1"),
                      QStringLiteral("实际 %1").arg(precisionCamera->minimumGridStep()));

        // 菜单里必须有这个选项（否则用户根本找不到）。
        // 按对象名查找而不是按标题文案：标题以后可能改，对象名是稳定的契约。
        QMenu *precisionMenu =
            precisionWindow.findChild<QMenu *>(QStringLiteral("gridPrecisionMenu"));
        checker.check(precisionMenu != nullptr,
                      QStringLiteral("菜单里应有「坐标轴最小精度」子菜单"));
        if (precisionMenu != nullptr) {
            checker.check(precisionMenu->actions().size() >= 4,
                          QStringLiteral("最小精度子菜单应提供若干可选值"),
                          QStringLiteral("实际 %1 项").arg(precisionMenu->actions().size()));
            if (!precisionMenu->actions().isEmpty()) {
                const QAction *first = precisionMenu->actions().first();
                checker.check(isCloseToDouble(first->data().toDouble(), 1.0),
                              QStringLiteral("第一个选项应为「1 每格」"),
                              QStringLiteral("data=%1").arg(first->data().toDouble()));
            }
        }

        // 走与界面相同的路径改精度：网格步长必须跟着变大
        precisionController.onGridPrecisionRequested(5.0);
        checker.check(isCloseToDouble(precisionCamera->minimumGridStep(), 5.0)
                          && precisionCamera->gridStep() >= 5.0,
                      QStringLiteral("把最小精度设为 5 后，网格步长应不小于 5"),
                      QStringLiteral("最小精度 %1，网格步长 %2")
                          .arg(precisionCamera->minimumGridStep())
                          .arg(precisionCamera->gridStep()));

        // 放大到上限后网格不得细于最小精度（这条直接对应需求）
        precisionCamera->setZoom(100000.0);
        checker.check(precisionCamera->zoomPercent() == 300,
                      QStringLiteral("缩放上限应为 300%"),
                      QStringLiteral("实际 %1%").arg(precisionCamera->zoomPercent()));
        checker.check(precisionCamera->gridStep() >= precisionCamera->minimumGridStep(),
                      QStringLiteral("放大到上限后网格步长仍不得低于最小精度"),
                      QStringLiteral("网格步长 %1，最小精度 %2")
                          .arg(precisionCamera->gridStep())
                          .arg(precisionCamera->minimumGridStep()));

        // 调完要有可见反馈，否则用户不知道生效没有
        checker.check(!precisionWindow.statusMessage().isEmpty(),
                      QStringLiteral("调整最小精度后应在状态栏给出说明"),
                      precisionWindow.statusMessage());

        // 复位为默认值后再验证一次（默认 1 每格时网格不得细于 1）
        precisionController.onGridPrecisionRequested(1.0);
        checker.check(precisionCamera->gridStep() >= 1.0,
                      QStringLiteral("恢复默认（1 每格）后网格步长仍不得低于 1"),
                      QStringLiteral("网格步长 %1").arg(precisionCamera->gridStep()));
    }

    // ---- 13. 撤销：连续撤销应当能把图元删掉，且不崩溃 ----
    {
        QUndoStack *stack = controller.undoStack();
        checker.check(stack != nullptr, QStringLiteral("控制器应持有撤销栈"));
        if (stack != nullptr) {
            const int before = document->shapeCount();
            print(QStringLiteral("   撤销栈：count=%1 栈顶=%2")
                      .arg(stack->count())
                      .arg(stack->undoText()));

            // 合成事件里夹带了点拖动，栈顶未必是"新建图形"。
            // 因此连续撤销若干次，只要求"至少有一次让图元数下降"，并全程不崩溃。
            int undoSteps = 0;
            int minimumCount = before;
            while (stack->canUndo() && undoSteps < 20) {
                controller.onUndoRequested();
                ++undoSteps;
                minimumCount = qMin(minimumCount, document->shapeCount());
            }
            checker.check(undoSteps > 0, QStringLiteral("应当有可撤销的步骤"));
            checker.check(minimumCount < before,
                          QStringLiteral("连续撤销后图元数应当下降"),
                          QStringLiteral("撤销 %1 步，最少时 %2 个（起始 %3 个）")
                              .arg(undoSteps).arg(minimumCount).arg(before));

            // 再重做几步，确认正向恢复也正常
            if (stack->canRedo()) {
                const int beforeRedo = document->shapeCount();
                controller.onRedoRequested();
                checker.check(document->shapeCount() >= beforeRedo,
                              QStringLiteral("重做不应导致图元数量异常减少"));
            }
        }
    }

    print(QStringLiteral("---- 交互自测：通过 %1 / 失败 %2 ----")
              .arg(checker.passes())
              .arg(checker.failures()));
    print(QStringLiteral("=================================="));
    if (checker.failures() == 0) {
        print(QStringLiteral("结论：全部用例通过"));
        return 0;
    }
    print(QStringLiteral("结论：存在失败项"));
    return 1;
}
