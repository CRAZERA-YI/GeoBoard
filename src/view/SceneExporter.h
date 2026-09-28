/**
 * @file    view/SceneExporter.h
 * @brief   把当前作图导出为 PNG 或 SVG 文件。
 * @details 本类只做三件事：决定导出尺寸、驱动画布绘制到目标设备、把结果写成文件。
 *          **绘制逻辑一行都不重复**——全部委托给 `CanvasWidget::renderToDevice()`，
 *          它使用的正是屏幕上那套绘制代码，因此导出结果与所见一致。
 *
 *          放在 view 层而不是 service 层，原因就是它必须认识 `CanvasWidget`；
 *          service 层的 `JsonStore` 只处理数据、不认识任何控件。
 *
 *          不负责：弹文件对话框（controller/view 的职责）、决定导出哪一部分内容
 *          （当前导出全部有效图元，按内容自动适配画幅）。
 */

#ifndef GEOBOARDSCENEEXPORTER_H
#define GEOBOARDSCENEEXPORTER_H

#include <QSize>
#include <QString>

class CanvasWidget;

/**
 * @brief 作图导出器（PNG / SVG）。
 */
class SceneExporter
{
public:
    /** @brief 构造导出器。 */
    SceneExporter();

    /**
     * @brief 把当前作图导出为 PNG 位图。
     * @param canvas 提供绘制入口的画布，不得为 nullptr。
     * @param path 目标文件路径（建议以 .png 结尾）。
     * @param requestedSize 期望尺寸；宽或高为 0 或负数时按 1600×1200 处理。
     * @param scale 额外缩放倍数（1.0 为原始尺寸）；用于导出高清图。
     * @return 成功返回 true；失败时 `lastError()` 给出原因。
     * @note  导出使用**白色背景**而不是窗口背景色：图片通常要贴进文档或幻灯片，
     *        透明背景在那里会变成黑色。
     */
    bool exportPng(CanvasWidget *canvas,
                   const QString &path,
                   const QSize &requestedSize = QSize(1600, 1200),
                   double scale = 1.0);

    /**
     * @brief 把当前作图导出为 SVG 矢量图。
     * @param canvas 提供绘制入口的画布，不得为 nullptr。
     * @param path 目标文件路径（建议以 .svg 结尾）。
     * @param requestedSize 画幅尺寸；宽或高为 0 或负数时按 1600×1200 处理。
     * @return 成功返回 true；失败时 `lastError()` 给出原因。
     * @note  SVG 是矢量的，放大不失真，适合插入论文与课件。
     * @warning 需要 Qt 的 SVG 模块（`QT += svg`，运行期依赖 `Qt5Svg.dll`）；
     *          未启用该模块时本函数返回 false 并说明原因，而不是崩溃。
     */
    bool exportSvg(CanvasWidget *canvas,
                   const QString &path,
                   const QSize &requestedSize = QSize(1600, 1200));

    /**
     * @brief 返回最近一次失败的原因（面向用户，说明发生了什么、可怎么办）。
     * @return 错误文本；没有失败时为空串。
     */
    QString lastError() const { return m_lastError; }

    /**
     * @brief 返回 PNG 导出的默认建议尺寸。
     * @return 默认尺寸。
     */
    static QSize defaultSize();

private:
    /**
     * @brief 把用户给的尺寸规范化（把非法尺寸换成默认值）。
     * @param requested 请求的尺寸。
     * @return 可用的尺寸。
     */
    static QSize normalizedSize(const QSize &requested);

    QString m_lastError; ///< 最近一次失败原因。
};

#endif // GEOBOARDSCENEEXPORTER_H
