/**
 * @file    view/SceneExporter.cpp
 * @brief   PNG / SVG 导出的实现。
 * @details 两个导出路径的差别只有"画到哪种设备上"：位图是 QImage，矢量是 QSvgGenerator，
 *          而绘制本身都交给 `CanvasWidget::renderToDevice()`。因此新增导出格式时
 *          只需要再加一个设备类型，不必再写一套绘制代码。
 */

#include "SceneExporter.h"

#include "CanvasWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSaveFile>

// SVG 模块是可选依赖：未在 .pro 里加 QT += svg 时，这里退化为"不支持"而不是编译失败。
// 本项目已在 GeoBoard.pro 中启用（windeployqt 也会带上 Qt5Svg.dll）。
#include <QSvgGenerator>

#include <algorithm>
#include <cmath>

namespace {

/** @brief 默认导出尺寸：够用于课件与文档插图，又不会大到难以处理。 */
const int kDefaultWidth = 1600;
const int kDefaultHeight = 1200;

/** @brief 导出尺寸的上限，防止误输入导致内存耗尽。 */
const int kMaxDimension = 20000;

} // namespace

SceneExporter::SceneExporter()
    : m_lastError()
{
}

QSize SceneExporter::defaultSize()
{
    return QSize(kDefaultWidth, kDefaultHeight);
}

QSize SceneExporter::normalizedSize(const QSize &requested)
{
    int width = requested.width();
    int height = requested.height();
    if (width <= 0) {
        width = kDefaultWidth;
    }
    if (height <= 0) {
        height = kDefaultHeight;
    }
    width = std::min(width, kMaxDimension);
    height = std::min(height, kMaxDimension);
    return QSize(width, height);
}

bool SceneExporter::exportPng(CanvasWidget *canvas,
                              const QString &path,
                              const QSize &requestedSize,
                              double scale)
{
    m_lastError.clear();
    if (canvas == nullptr) {
        m_lastError = QObject::tr("导出失败：画布不可用。");
        return false;
    }
    if (path.isEmpty()) {
        m_lastError = QObject::tr("导出失败：没有指定文件路径。");
        return false;
    }

    const QSize base = normalizedSize(requestedSize);
    const double safeScale = (scale > 0.0 && scale <= 8.0) ? scale : 1.0;
    const QSize size(std::max(1, static_cast<int>(std::lround(base.width() * safeScale))),
                     std::max(1, static_cast<int>(std::lround(base.height() * safeScale))));

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        m_lastError = QObject::tr("导出失败：无法创建 %1×%2 的图片（尺寸过大或内存不足）。")
                          .arg(size.width())
                          .arg(size.height());
        return false;
    }
    // 先铺白底：PNG 带透明通道，贴进 Word/PPT 时透明区会显示为黑色
    image.fill(Qt::white);

    if (!canvas->renderToDevice(&image, size, false)) {
        m_lastError = QObject::tr("导出失败：绘制过程未完成。");
        return false;
    }

    // 用 QSaveFile 保证原子写入：中途失败不会留下半张损坏的图片
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QObject::tr("导出失败：无法写入文件 %1。\n\n可能原因：\n"
                                  "· 文件被其他程序打开（请关闭后重试）\n"
                                  "· 目标目录没有写入权限（请另存到桌面）\n"
                                  "· 磁盘空间不足")
                          .arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (!image.save(&file, "PNG")) {
        file.cancelWriting();
        m_lastError = QObject::tr("导出失败：PNG 编码未成功（磁盘空间不足或路径不可写）。");
        return false;
    }
    if (!file.commit()) {
        m_lastError = QObject::tr("导出失败：写入 %1 时未完成提交，文件可能未更新。")
                          .arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}

bool SceneExporter::exportSvg(CanvasWidget *canvas,
                              const QString &path,
                              const QSize &requestedSize)
{
    m_lastError.clear();
    if (canvas == nullptr) {
        m_lastError = QObject::tr("导出失败：画布不可用。");
        return false;
    }
    if (path.isEmpty()) {
        m_lastError = QObject::tr("导出失败：没有指定文件路径。");
        return false;
    }

    const QSize size = normalizedSize(requestedSize);

    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(size);
    // viewBox 与 size 设为一致：这样 SVG 在矢量缩放时坐标系与导出一致，
    // 不会出现"某些阅读器里图形偏移"的问题
    generator.setViewBox(QRect(QPoint(0, 0), size));
    generator.setTitle(QObject::tr("GeoBoard 作图"));
    generator.setDescription(QObject::tr("由 GeoBoard 导出的矢量作图"));

    if (!canvas->renderToDevice(&generator, size, false)) {
        m_lastError = QObject::tr("导出失败：绘制过程未完成。");
        return false;
    }

    if (!QFileInfo::exists(path)) {
        m_lastError = QObject::tr("导出失败：未能生成 %1。\n\n本程序需要 Qt 的 SVG 模块"
                                  "（Qt5Svg）。若目标机缺少该 DLL，请改用 PNG 导出。")
                          .arg(QDir::toNativeSeparators(path));
        return false;
    }
    return true;
}
