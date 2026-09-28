/**
 * @file    ArcCenterRepro.cpp
 * @brief   复现"圆心 + 两点画圆弧不能正常绘制"的现场诊断工具（临时用）。
 * @details 与 InteractionSmoke 的差别：这里刻意**逐事件回放真人操作**
 *          （按下 + 松开成对出现、三次点击之间夹着鼠标移动、吸附保持默认开启），
 *          并把每一步之后的状态机与文档内容打印出来，
 *          以便判断到底是哪一步没生效。
 */

#include "controller/MainController.h"
#include "controller/ToolId.h"
#include "model/GeoDocument.h"
#include "model/Shape.h"
#include "view/CanvasWidget.h"
#include "view/GeoBoardMainWindow.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QString>

#include <cstdio>

namespace {

/** @brief 输出一行文本。 */
void print(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

/** @brief 投递一个合成鼠标事件。 */
void send(CanvasWidget *canvas, QEvent::Type type, const QPoint &pos, Qt::MouseButton button)
{
    const Qt::MouseButtons buttons = (type == QEvent::MouseButtonRelease) ? Qt::NoButton : button;
    QMouseEvent event(type, QPointF(pos), QPointF(canvas->mapToGlobal(pos)), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(canvas, &event);
}

/** @brief 用"按下 + 松开"模拟一次完整点击。 */
void click(CanvasWidget *canvas, const QPoint &pos)
{
    send(canvas, QEvent::MouseButtonPress, pos, Qt::LeftButton);
    send(canvas, QEvent::MouseButtonRelease, pos, Qt::LeftButton);
}

/** @brief 描述文档里所有图元。 */
QString describe(const GeoDocument *document)
{
    QStringList parts;
    const QList<Shape *> shapes = document->shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        Shape *shape = shapes.at(i);
        QString extra;
        if (shape->type() == ShapeType::Arc) {
            const ArcShape *arc = static_cast<const ArcShape *>(shape);
            extra = QStringLiteral("[kind=%1 r=%2 sweep=%3]")
                        .arg(arc->kind() == ArcKind::CenterTwoPoint ? QStringLiteral("center")
                                                                    : QStringLiteral("three"))
                        .arg(arc->arcData().radius)
                        .arg(arc->arcData().sweep());
        }
        parts << QStringLiteral("#%1 %2%3%4")
                     .arg(shape->id())
                     .arg(static_cast<int>(shape->type()))
                     .arg(shape->isValid() ? QString() : QStringLiteral("(无效)"))
                     .arg(extra);
    }
    return parts.join(QStringLiteral(", "));
}

/** @brief 把画布当前画面存成 PNG，用于人工核对"分步预览到底长什么样"。 */
void saveCanvas(CanvasWidget *canvas, const QString &fileName)
{
    QImage shot(canvas->size(), QImage::Format_ARGB32_Premultiplied);
    shot.fill(Qt::white);
    {
        QPainter painter(&shot);
        canvas->render(&painter);
    }
    shot.save(fileName);
}

} // namespace

/**
 * @brief 程序入口。
 * @param argc 参数个数。
 * @param argv 参数数组。
 * @return 始终返回 0（这是诊断工具，不是测试）。
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    GeoBoardMainWindow window;
    MainController controller(&window);
    controller.start();
    window.resize(900, 620);

    CanvasWidget *canvas = window.canvas();
    GeoDocument *document = canvas->document();
    const QPoint center(canvas->width() / 2, canvas->height() / 2);

    print(QStringLiteral("画布 %1x%2，吸附默认=%3")
              .arg(canvas->width())
              .arg(canvas->height())
              .arg(canvas->isSnapEnabled() ? QStringLiteral("开") : QStringLiteral("关")));

    // 通过工具栏动作切换工具，走与真人点击菜单完全相同的路径
    controller.onToolChangeRequested(ToolId::ArcCenter);
    print(QStringLiteral("当前工具=%1（期望 圆弧(圆心)）")
              .arg(static_cast<int>(controller.currentTool())));

    const QPoint p1 = center + QPoint(-120, 60);   // 圆心
    const QPoint p2 = center + QPoint(120, 60);    // 半径点
    const QPoint p3 = center + QPoint(-120, -120); // 弧上点

    print(QStringLiteral("--- 第 1 次点击（圆心）---"));
    click(canvas, p1);
    print(QStringLiteral("  图元：%1").arg(describe(document)));
    print(QStringLiteral("  状态栏：%1").arg(window.statusMessage()));

    print(QStringLiteral("--- 第 2 次点击（半径点）---"));
    click(canvas, p2);
    print(QStringLiteral("  图元：%1").arg(describe(document)));
    print(QStringLiteral("  状态栏：%1").arg(window.statusMessage()));

    print(QStringLiteral("--- 第 3 次点击（弧上点）---"));
    click(canvas, p3);
    print(QStringLiteral("  图元：%1").arg(describe(document)));
    print(QStringLiteral("  状态栏：%1").arg(window.statusMessage()));

    // 再试一次"两次点击之间夹鼠标移动"的真人节奏
    print(QStringLiteral("--- 换一种节奏：点击 → 移动 → 点击 → 移动 → 点击 ---"));
    controller.onToolChangeRequested(ToolId::ArcCenter);
    const QPoint q1 = center + QPoint(-200, 150);
    const QPoint q2 = center + QPoint(-80, 150);
    const QPoint q3 = center + QPoint(-200, 30);
    click(canvas, q1);
    send(canvas, QEvent::MouseMove, center, Qt::NoButton);
    click(canvas, q2);
    send(canvas, QEvent::MouseMove, center, Qt::NoButton);
    click(canvas, q3);
    print(QStringLiteral("  图元：%1").arg(describe(document)));

    // 第三次：按住拖一小段再松开（真人手抖时一定会发生）
    print(QStringLiteral("--- 每次点击都带 2 像素抖动（模拟手抖）---"));
    controller.onToolChangeRequested(ToolId::ArcCenter);
    const QPoint r1 = center + QPoint(60, 180);
    const QPoint r2 = center + QPoint(180, 180);
    const QPoint r3 = center + QPoint(60, 60);
    click(canvas, r1);
    send(canvas, QEvent::MouseButtonPress, r2, Qt::LeftButton);
    send(canvas, QEvent::MouseMove, r2 + QPoint(2, 2), Qt::NoButton);
    send(canvas, QEvent::MouseButtonRelease, r2 + QPoint(2, 2), Qt::LeftButton);
    send(canvas, QEvent::MouseButtonPress, r3, Qt::LeftButton);
    send(canvas, QEvent::MouseMove, r3 + QPoint(1, 1), Qt::NoButton);
    send(canvas, QEvent::MouseButtonRelease, r3 + QPoint(1, 1), Qt::LeftButton);
    print(QStringLiteral("  图元：%1").arg(describe(document)));
    print(QStringLiteral("  状态栏：%1").arg(window.statusMessage()));

    // 关键一步：把画布渲染出来看看到底画成了什么。
    // "状态机是对的、文档里有图元"并不等于"用户看得见"——半径太小、
    // 位置在可视区之外、或者绘制路径有分支漏掉，都会让用户觉得"没画出来"。
    {
        window.resize(900, 620);
        window.show();
        QApplication::processEvents();
        canvas->fitContent();
        QApplication::processEvents();

        QImage shot(canvas->size(), QImage::Format_ARGB32_Premultiplied);
        shot.fill(Qt::white);
        {
            QPainter painter(&shot);
            canvas->render(&painter);
        }
        const QString shotPath = QStringLiteral("arc-repro-canvas.png");
        shot.save(shotPath);

        print(QStringLiteral("画布 %1x%2，缩放 %3%，网格步长 %4")
                  .arg(canvas->width())
                  .arg(canvas->height())
                  .arg(canvas->camera()->zoomPercent())
                  .arg(canvas->camera()->gridStep()));
        print(QStringLiteral("已导出画布截图：%1").arg(shotPath));

        // 逐个报告弧的屏幕位置，判断它是否落在可视区内
        const QList<Shape *> shapes = document->shapesInOrder();
        for (int i = 0; i < shapes.size(); ++i) {
            if (shapes.at(i)->type() != ShapeType::Arc) {
                continue;
            }
            const ArcShape *arc = static_cast<const ArcShape *>(shapes.at(i));
            const QPointF centerScreen = canvas->camera()->toScreen(arc->arcData().center);
            const double radiusPixels = arc->arcData().radius * canvas->camera()->zoom();
            print(QStringLiteral("  弧 #%1：圆心屏幕位置 (%2,%3)，半径 %4 像素，张角 %5°，有效=%6")
                      .arg(arc->id())
                      .arg(centerScreen.x(), 0, 'f', 1)
                      .arg(centerScreen.y(), 0, 'f', 1)
                      .arg(radiusPixels, 0, 'f', 1)
                      .arg(arc->arcData().sweep() * 180.0 / GeoMath::kPi, 0, 'f', 1)
                      .arg(arc->isValid()));
        }
    }

    // 分步预览的三种状态各截一张图。断言能证明"状态机对了"，
    // 但证明不了"用户看得见自己在做什么"——那只能看图。
    {
        window.resize(1000, 700);
        window.show();
        QApplication::processEvents();

        controller.onToolChangeRequested(ToolId::ArcCenter);
        const QPoint previewCenter(300, 300);
        const QPoint previewRadius(500, 300);
        const QPoint previewOnArc(300, 120);

        // ① 只定了圆心：应看到"圆心 → 鼠标"的半径示意与整圆预览
        click(canvas, previewCenter);
        send(canvas, QEvent::MouseMove, previewRadius, Qt::NoButton);
        QApplication::processEvents();
        print(QStringLiteral("预览诊断：控制器工具=%1 画布工具=%2 armed=%3 有弧预览=%4 拖动中=%5")
                  .arg(static_cast<int>(controller.currentTool()))
                  .arg(static_cast<int>(canvas->activeTool()))
                  .arg(canvas->isConstructionArmed() ? 1 : 0)
                  .arg(canvas->hasArcPreview() ? 1 : 0)
                  .arg(canvas->isConstructionDragging() ? 1 : 0));
        saveCanvas(canvas, QStringLiteral("arc-preview-1-center.png"));

        // ② 半径也定了：应看到虚线半径 + 整圆
        click(canvas, previewRadius);
        send(canvas, QEvent::MouseMove, previewOnArc, Qt::NoButton);
        QApplication::processEvents();
        saveCanvas(canvas, QStringLiteral("arc-preview-2-radius.png"));

        print(QStringLiteral("已导出三张分步预览图：arc-preview-1-center.png / "
                             "arc-preview-2-radius.png（第三步完成后的弧见整窗截图）"));
    }

    return 0;
}
