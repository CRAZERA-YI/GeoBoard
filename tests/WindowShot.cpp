/**
 * @file    WindowShot.cpp
 * @brief   把主窗口（含停靠面板与工具栏）离屏渲染成一张 PNG，用于人工核对布局。
 * @details 为什么需要它：自动化断言能验证"面板存在、能列出图元、选中能同步"，
 *          但**验证不了"它看起来是不是对的"**——面板会不会挤掉画布、工具栏会不会换行、
 *          两边宽度是否协调，这些只能靠眼睛。离屏渲染不需要可见窗口，
 *          因此在远程会话里也能跑，而且不受屏幕 DPI 影响（截屏方式踩过这个坑：
 *          逻辑分辨率与物理分辨率不一致，导致截出来的图缺一块）。
 *
 *          用法：window_shot.exe [输出路径]
 */

#include "controller/MainController.h"
#include "controller/ToolId.h"
#include "model/GeoDocument.h"
#include "model/Shape.h"
#include "view/CanvasWidget.h"
#include "view/GeoBoardMainWindow.h"
#include "view/ShapeListPanel.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

/** @brief 输出一行文本（Windows 控制台用 UTF-8，避免中文乱码）。 */
void print(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

} // namespace

/**
 * @brief 程序入口。
 * @param argc 参数个数。
 * @param argv 参数数组。
 * @return 成功返回 0。
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QString outputPath = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                          : QStringLiteral("window-shot.png");

    GeoBoardMainWindow window;
    MainController controller(&window);
    controller.start();

    // 造一点有代表性的内容：点、线段、圆、圆心式圆弧、测量标注，
    // 这样对象列表里能同时看到多种类型与它们的说明列
    GeoDocument *document = window.canvas()->document();
    const ShapeId pointA = document->allocateShapeId();
    const ShapeId pointB = document->allocateShapeId();
    const ShapeId pointC = document->allocateShapeId();
    const ShapeId pointD = document->allocateShapeId();
    document->addShape(new PointShape(pointA, PointKind::Free, Vec2(-4.0, -2.0)));
    document->addShape(new PointShape(pointB, PointKind::Free, Vec2(2.0, -2.0)));
    document->addShape(new PointShape(pointC, PointKind::Free, Vec2(1.0, 2.5)));
    document->addShape(new PointShape(pointD, PointKind::Free, Vec2(-1.0, 2.5)));
    const ShapeId segmentId = document->allocateShapeId();
    document->addShape(new LineShape(segmentId, LineKind::Segment, pointA, pointB));
    const ShapeId circleId = document->allocateShapeId();
    document->addShape(new CircleShape(circleId, pointC, pointD));
    const ShapeId arcCenterId = document->allocateShapeId();
    const ShapeId arcRadiusId = document->allocateShapeId();
    const ShapeId arcOnId = document->allocateShapeId();
    document->addShape(new PointShape(arcCenterId, PointKind::Free, Vec2(4.5, 1.5)));
    document->addShape(new PointShape(arcRadiusId, PointKind::Free, Vec2(6.5, 1.5)));
    document->addShape(new PointShape(arcOnId, PointKind::Free, Vec2(4.5, 3.5)));
    const ShapeId arcId = document->allocateShapeId();
    document->addShape(new ArcShape(arcId, arcCenterId, arcRadiusId, arcOnId,
                                    ArcKind::CenterTwoPoint));
    const ShapeId measureId = document->allocateShapeId();
    QList<ShapeId> measureInputs;
    measureInputs << pointA << pointB;
    document->addShape(new MeasurementShape(measureId, MeasurementKind::Distance, measureInputs,
                                            kInvalidShapeId, Vec2(-1.0, -2.6)));

    // 面板绑定文档并刷新（真实程序里由 MainController::start() 完成绑定，
    // 但那时文档还是空的，所以这里补一次刷新让内容出现在截图里）
    window.shapeListPanel()->refresh();
    window.canvas()->fitContent();

    // 给窗口一个确定的尺寸再渲染：默认尺寸下停靠面板可能被挤成很窄，
    // 不利于核对布局
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();

    QImage image(window.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        window.render(&painter);
    }

    if (!image.save(outputPath)) {
        print(QStringLiteral("[FAIL] 无法写出图片：%1").arg(outputPath));
        return 1;
    }

    print(QStringLiteral("已写出：%1").arg(outputPath));
    print(QStringLiteral("窗口尺寸：%1x%2，图元数：%3，对象列表可见：%4")
              .arg(window.width())
              .arg(window.height())
              .arg(document->shapeCount())
              .arg(window.shapeListPanel()->isVisible() ? QStringLiteral("是")
                                                        : QStringLiteral("否")));
    return 0;
}
