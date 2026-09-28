/**
 * @file    tests/RenderDump.cpp
 * @brief   离屏渲染出图：把画布真实绘制到一个 PNG 文件。
 * @details 存在的理由：抗锯齿、圆的绘制质量这类改动用"看代码"无法验证，
 *          而在 200% 缩放的机器上截屏又抓不全窗口。让 CanvasWidget 把同一个
 *          paintEvent 画到 QImage 上，就得到了一份**可复核、可对比、可归档**的证据。
 *
 *          它顺带证明了一件事：CanvasWidget 的绘制逻辑不依赖窗口的可见性，
 *          可以脱离真实窗口渲染（对将来做导出图片功能也有用）。
 *
 *          用法：render_dump.exe <输出.png> [--demo]
 *          默认输出 render-dump.png，并打印画布尺寸与图元数量。
 */

#include "controller/ToolId.h"
#include "model/GeoDocument.h"
#include "model/Shape.h"
#include "view/CanvasWidget.h"
#include "view/SceneExporter.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

/** @brief 输出一行文本。 */
void print(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    std::fputs(utf8.constData(), stdout);
    std::fputs("\n", stdout);
}

/** @brief 加一个自由点。 */
ShapeId addPoint(GeoDocument *document, double x, double y)
{
    const ShapeId id = document->allocateShapeId();
    document->addShape(new PointShape(id, PointKind::Free, Vec2(x, y)));
    return id;
}

/**
 * @brief 构造用于出图的演示图形。
 * @param document 目标文档。
 * @details 刻意包含圆与圆弧：这两者是最能看出锯齿的图元，也是本次抗锯齿改动的对象。
 */
void buildScene(GeoDocument *document)
{
    // 圆（半径 2.2）与它的圆心、圆上点
    const ShapeId centerId = addPoint(document, -3.0, 0.0);
    const ShapeId rimId = addPoint(document, -0.8, 0.0);
    const ShapeId circleId = document->allocateShapeId();
    document->addShape(new CircleShape(circleId, centerId, rimId));

    // 一条与圆相交的线段 + 两个交点
    const ShapeId lineAId = addPoint(document, -5.0, 1.1);
    const ShapeId lineBId = addPoint(document, 0.0, 1.1);
    const ShapeId segmentId = document->allocateShapeId();
    document->addShape(new LineShape(segmentId, LineKind::Segment, lineAId, lineBId));
    for (int i = 0; i < 2; ++i) {
        const ShapeId intersectionId = document->allocateShapeId();
        document->addShape(new IntersectionPointShape(intersectionId, circleId, segmentId, i));
    }

    // 三点圆弧：小半径圆弧最能暴露锯齿
    const ShapeId arcAId = addPoint(document, 1.5, -1.5);
    const ShapeId arcBId = addPoint(document, 3.0, 0.0);
    const ShapeId arcCId = addPoint(document, 4.5, -1.5);
    const ShapeId arcId = document->allocateShapeId();
    document->addShape(new ArcShape(arcId, arcAId, arcBId, arcCId));

    // 多边形 + 面积标注
    const ShapeId polyAId = addPoint(document, 1.0, 1.5);
    const ShapeId polyBId = addPoint(document, 5.0, 1.5);
    const ShapeId polyCId = addPoint(document, 3.0, 3.6);
    QList<ShapeId> vertices;
    vertices << polyAId << polyBId << polyCId;
    const ShapeId polygonId = document->allocateShapeId();
    document->addShape(new PolygonShape(polygonId, vertices));
    const ShapeId areaId = document->allocateShapeId();
    document->addShape(new MeasurementShape(areaId, MeasurementKind::Area, QList<ShapeId>(),
                                            polygonId, Vec2(2.4, 2.2)));

    // 半径标注（顺带验证文字底衬与抗锯齿共存）
    const ShapeId radiusId = document->allocateShapeId();
    document->addShape(new MeasurementShape(radiusId, MeasurementKind::Radius, QList<ShapeId>(),
                                            circleId, Vec2(-3.0, 2.6)));
}

} // namespace

/**
 * @brief 入口。
 * @param argc 参数个数。
 * @param argv 参数数组；argv[1] 可指定输出路径。
 * @return 成功写出图片返回 0，否则返回 1。
 */
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    const QStringList arguments = QCoreApplication::arguments();
    QString outputPath = QStringLiteral("render-dump.png");
    for (int i = 1; i < arguments.size(); ++i) {
        if (!arguments.at(i).startsWith(QLatin1Char('-'))) {
            outputPath = arguments.at(i);
        }
    }

    GeoDocument document;
    buildScene(&document);

    CanvasWidget canvas;
    canvas.setDocument(&document);
    canvas.setActiveTool(ToolId::Select);
    canvas.resize(900, 620);

    // 先把视图适配到全部内容，保证图形完整落在画面内
    canvas.fitContent();

    QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    // QWidget::render 会把 CanvasWidget 的 paintEvent 重放到这个 QPainter 上，
    // 因此用的是与屏幕完全相同的绘制代码路径（含抗锯齿设置）
    canvas.render(&painter);
    painter.end();

    if (!image.save(outputPath)) {
        print(QStringLiteral("[FAIL] 无法写出图片：%1").arg(outputPath));
        return 1;
    }

    print(QStringLiteral("已写出：%1").arg(outputPath));
    print(QStringLiteral("画布尺寸：%1x%2，图元数：%3，缩放：%4%")
              .arg(canvas.width())
              .arg(canvas.height())
              .arg(document.shapeCount())
              .arg(canvas.camera()->zoomPercent()));

    // ---- 顺带验证"导出图片"功能真的能产出可用文件 ----
    // 导出与屏幕显示共用 CanvasWidget::renderToDevice，因此这里能覆盖到导出的核心路径。
    // 若这一步失败，说明导出功能坏了——比"界面上点了没反应"要早得多地暴露出来。
    int failures = 0;

    const QString pngPath = QFileInfo(outputPath).absolutePath()
        + QStringLiteral("/export-check.png");
    SceneExporter exporter;
    if (exporter.exportPng(&canvas, pngPath, QSize(1200, 900))) {
        const QImage check(pngPath);
        if (check.isNull()) {
            print(QStringLiteral("[FAIL] PNG 导出后无法读回：%1").arg(pngPath));
            ++failures;
        } else {
            print(QStringLiteral("PNG 导出成功：%1x%2（%3 字节）")
                      .arg(check.width())
                      .arg(check.height())
                      .arg(QFileInfo(pngPath).size()));
        }
    } else {
        print(QStringLiteral("[FAIL] PNG 导出失败：%1").arg(exporter.lastError()));
        ++failures;
    }

    const QString svgPath = QFileInfo(outputPath).absolutePath()
        + QStringLiteral("/export-check.svg");
    if (exporter.exportSvg(&canvas, svgPath, QSize(1200, 900))) {
        QFile svgFile(svgPath);
        bool looksLikeSvg = false;
        qint64 svgSize = 0;
        if (svgFile.open(QIODevice::ReadOnly)) {
            const QByteArray head = svgFile.read(512);
            const QByteArray all = svgFile.readAll();
            svgSize = head.size() + all.size();
            // 只检查"是 SVG 且含矢量元素"，不做全文比对（不同 Qt 版本写法略有差别）
            looksLikeSvg = head.contains("<svg") && all.contains("<path");
        }
        if (!looksLikeSvg) {
            print(QStringLiteral("[FAIL] SVG 导出结果不像有效的矢量图：%1").arg(svgPath));
            ++failures;
        } else {
            print(QStringLiteral("SVG 导出成功：%1 字节，含矢量路径元素").arg(svgSize));
        }
    } else {
        print(QStringLiteral("[FAIL] SVG 导出失败：%1").arg(exporter.lastError()));
        ++failures;
    }

    if (failures > 0) {
        print(QStringLiteral("结论：出图成功，但导出验证有 %1 项失败").arg(failures));
        return 1;
    }
    print(QStringLiteral("结论：出图与导出验证全部通过"));
    return 0;
}
