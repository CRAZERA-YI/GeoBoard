/**
 * @file    model/Camera.h
 * @brief   场景坐标与屏幕坐标之间的双向变换。
 * @details 场景坐标系 y 轴**向上**（数学惯例），屏幕坐标系 y 轴向下。
 *          两者的换算只允许经由本类完成 —— 自绘代码里手工翻转 y 轴是
 *          符号错误的常见来源。
 *
 *          不负责：绘制坐标轴与网格（见 view/CanvasWidget）、
 *          决定缩放交互（见 controller/MainController）。
 */

#ifndef GEOBOARDCAMERA_H
#define GEOBOARDCAMERA_H

#include "GeometryMath.h"

#include <QPointF>
#include <QTransform>

/**
 * @brief 场景 ↔ 屏幕的视图变换。
 */
class Camera
{
public:
    /** @brief 构造默认相机：原点居中、每场景单位 40 像素。 */
    Camera();

    /** @brief 最小缩放（像素/场景单位）；再小会让网格与点失去意义。 */
    static double minZoom();

    /** @brief 最大缩放（像素/场景单位）。 */
    static double maxZoom();

    /**
     * @brief 设置视口尺寸（像素）。
     * @param width 视口宽度，负值按 0 处理。
     * @param height 视口高度，负值按 0 处理。
     */
    void setViewportSize(int width, int height);

    /** @brief 返回视口宽度（像素）。 */
    int viewportWidth() const { return m_viewportWidth; }

    /** @brief 返回视口高度（像素）。 */
    int viewportHeight() const { return m_viewportHeight; }

    /**
     * @brief 设置缩放。
     * @param zoom 每场景单位的像素数，会被夹到 [minZoom(), maxZoom()]。
     */
    void setZoom(double zoom);

    /** @brief 返回当前缩放（像素/场景单位）。 */
    double zoom() const { return m_zoom; }

    /**
     * @brief 以某个场景点为锚点缩放，锚点在屏幕上的位置保持不变。
     * @param factor 缩放倍数（>1 放大）。
     * @param anchorScene 锚点的场景坐标（通常是鼠标位置）。
     */
    void zoomAround(double factor, const Vec2 &anchorScene);

    /** @brief 返回当前缩放的百分比（100 表示每单位 40 像素的基准视图）。 */
    int zoomPercent() const;

    /**
     * @brief 平移视图。
     * @param sceneDelta 场景坐标增量；正值表示内容向屏幕右/上方移动。
     */
    void panBySceneDelta(const Vec2 &sceneDelta);

    /**
     * @brief 把视图中心移动到指定场景点。
     * @param sceneCenter 目标中心。
     */
    void centerOn(const Vec2 &sceneCenter);

    /** @brief 返回视图中心的场景坐标。 */
    const Vec2 &sceneCenter() const { return m_sceneCenter; }

    /**
     * @brief 屏幕坐标 → 场景坐标。
     * @param screenPos 屏幕（窗口）坐标。
     * @return 对应的场景坐标。
     */
    Vec2 toScene(const QPointF &screenPos) const;

    /**
     * @brief 场景坐标 → 屏幕坐标。
     * @param scenePos 场景坐标。
     * @return 对应的屏幕（窗口）坐标。
     */
    QPointF toScreen(const Vec2 &scenePos) const;

    /**
     * @brief 构造场景 → 屏幕的 QTransform。
     * @return 变换矩阵，供 QPainter::setTransform 使用。
     */
    QTransform sceneToScreenTransform() const;

    /**
     * @brief 返回当前可见区域的场景矩形角点。
     * @param minCorner 输出参数：左下角。
     * @param maxCorner 输出参数：右上角。
     */
    void visibleSceneRect(Vec2 *minCorner, Vec2 *maxCorner) const;

    /**
     * @brief 把长度从屏幕像素换算为场景单位。
     * @param pixels 像素长度。
     * @return 对应的场景长度。
     * @note  命中测试的容差必须走这里换算，否则缩放后"手感"会随缩放变化。
     */
    double pixelsToScene(double pixels) const;

    /**
     * @brief 选取网格步长，使相邻网格线在屏幕上的间距落在合理区间。
     * @return 网格步长（场景单位），取 1/2/5 × 10^n 形式，且不小于 minimumGridStep()。
     */
    double gridStep() const;

    /**
     * @brief 返回坐标轴网格的默认最小精度（每格不少于多少场景单位）。
     * @return 默认最小精度，当前为 1.0。
     * @note  它就是"最小精度为每格长度 1"这个默认约束的来源，同时也是吸附的
     *        最小颗粒度——吸附分辨率由 gridStep() 推出，因此两者永远一致。
     */
    static double defaultMinimumGridStep();

    /**
     * @brief 设置网格的最小精度。
     * @param step 每格不少于多少场景单位；非有限值或非正数将被忽略。
     * @note  这一步只改变**网格与吸附的颗粒度**，不改变缩放上限；
     *        调大它会让网格更粗（缩小时看到的格子更少），调小则允许更细的网格。
     */
    void setMinimumGridStep(double step);

    /**
     * @brief 返回当前网格最小精度。
     * @return 每格不少于多少场景单位。
     */
    double minimumGridStep() const { return m_minimumGridStep; }

    /**
     * @brief 调整视图以容纳给定的场景包围盒。
     * @param minCorner 包围盒左下角。
     * @param maxCorner 包围盒右上角。
     * @param marginRatio 边距比例（0.1 表示四周各留 10%）。
     * @return 包围盒有效（非退化）并成功调整时返回 true。
     */
    bool fitToBounds(const Vec2 &minCorner, const Vec2 &maxCorner, double marginRatio);

    /** @brief 恢复默认视图（原点居中、默认缩放）。 */
    void reset();

private:
    Vec2 m_sceneCenter;  ///< 视口中心对应的场景坐标。
    double m_zoom;       ///< 像素/场景单位。
    int m_viewportWidth; ///< 视口宽度（像素）。
    int m_viewportHeight;///< 视口高度（像素）。
    double m_minimumGridStep; ///< 网格与吸附的最小精度（每格不少于多少场景单位）。
};

#endif // GEOBOARDCAMERA_H
