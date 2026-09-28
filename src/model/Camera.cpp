/**
 * @file    model/Camera.cpp
 * @brief   场景与屏幕坐标变换的实现。
 * @details 变换公式（屏幕像素 x_s、y_s；场景单位 x_c、y_c）：
 *
 *              x_s = W/2 + (x_c - cx) * zoom
 *              y_s = H/2 - (y_c - cy) * zoom
 *
 *          注意 y 方向的**减号**：场景 y 向上而屏幕 y 向下，本类是唯一做这个
 *          翻转的地方，其余代码不必也不应再操心方向问题。
 */

#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace {

/** @brief 默认缩放（像素/场景单位）：40 像素对应 1 个场景单位。 */
const double kDefaultZoom = 40.0;

/** @brief 计算缩放百分比时的基准缩放。 */
const double kReferenceZoom = 40.0;

/**
 * @brief 网格与吸附的最小精度：每格不少于这么多场景单位。
 * @details 取 1.0，即"最小精度为每格长度 1"（用户明确要求）。
 *          它是网格步长的**下限**，同时也决定了吸附的最小颗粒度——
 *          因为吸附的分辨率就是由 gridStep 推出来的。
 */
const double kMinimumGridStep = 1.0;

} // namespace

Camera::Camera()
    : m_sceneCenter(0.0, 0.0)
    , m_zoom(kDefaultZoom)
    , m_viewportWidth(800)
    , m_viewportHeight(600)
    , m_minimumGridStep(kMinimumGridStep)
{
}

double Camera::defaultMinimumGridStep()
{
    return kMinimumGridStep;
}

void Camera::setMinimumGridStep(double step)
{
    // 非法输入直接忽略而不是夹到某个值：夹取会让"设成 0"悄悄变成 1，
    // 调用方以为自己改成功了。忽略至少保证状态不会被污染成无意义的值。
    if (!GeoMath::isFiniteNumber(step) || step <= 0.0) {
        return;
    }
    m_minimumGridStep = step;
}

double Camera::minZoom()
{
    return 0.05;
}

double Camera::maxZoom()
{
    // 300% —— 参考缩放是 40 像素/单位，因此上限取 3 × 40 = 120。
    // 用户明确要求"最多放大到百分之三百"：放大到 100% 时看不清小数点后两位的
    // 坐标差异，300% 才能把相邻的两个点分开看。
    return kReferenceZoom * 3.0;
}

void Camera::setViewportSize(int width, int height)
{
    m_viewportWidth = std::max(0, width);
    m_viewportHeight = std::max(0, height);
}

void Camera::setZoom(double zoom)
{
    if (!GeoMath::isFiniteNumber(zoom) || zoom <= 0.0) {
        return;
    }
    m_zoom = std::min(maxZoom(), std::max(minZoom(), zoom));
}

void Camera::zoomAround(double factor, const Vec2 &anchorScene)
{
    if (!GeoMath::isFiniteNumber(factor) || factor <= 0.0) {
        return;
    }

    // 锚点不变式的做法：先记住锚点在屏幕上的位置，缩放后再把中心补偿回去。
    // 推导：x_s = W/2 + (x_c - cx) * z，若 z 变化而要求 x_s 不变，
    //       则 cx 的增量 = (x_s' - x_s) / z_new；y 方向因屏幕 y 向下而取反。
    const QPointF anchorScreenBefore = toScreen(anchorScene);
    setZoom(m_zoom * factor);
    const QPointF anchorScreenAfter = toScreen(anchorScene);

    const double deltaX = (anchorScreenAfter.x() - anchorScreenBefore.x()) / m_zoom;
    const double deltaY = (anchorScreenAfter.y() - anchorScreenBefore.y()) / m_zoom;

    m_sceneCenter.x += deltaX;
    m_sceneCenter.y -= deltaY;
}

int Camera::zoomPercent() const
{
    return static_cast<int>(std::lround(m_zoom / kReferenceZoom * 100.0));
}

void Camera::panBySceneDelta(const Vec2 &sceneDelta)
{
    if (!sceneDelta.isFinite()) {
        return;
    }
    m_sceneCenter = m_sceneCenter + sceneDelta;
}

void Camera::centerOn(const Vec2 &sceneCenter)
{
    if (!sceneCenter.isFinite()) {
        return;
    }
    m_sceneCenter = sceneCenter;
}

Vec2 Camera::toScene(const QPointF &screenPos) const
{
    if (m_zoom <= 0.0) {
        return m_sceneCenter;
    }
    const double x = m_sceneCenter.x + (screenPos.x() - m_viewportWidth * 0.5) / m_zoom;
    const double y = m_sceneCenter.y - (screenPos.y() - m_viewportHeight * 0.5) / m_zoom;
    return Vec2(x, y);
}

QPointF Camera::toScreen(const Vec2 &scenePos) const
{
    const double x = m_viewportWidth * 0.5 + (scenePos.x - m_sceneCenter.x) * m_zoom;
    const double y = m_viewportHeight * 0.5 - (scenePos.y - m_sceneCenter.y) * m_zoom;
    return QPointF(x, y);
}

QTransform Camera::sceneToScreenTransform() const
{
    // 组合顺序：先平移到以中心为原点，再缩放，再翻转 y，最后移到屏幕中心
    QTransform transform;
    transform.translate(m_viewportWidth * 0.5, m_viewportHeight * 0.5);
    transform.scale(m_zoom, -m_zoom);
    transform.translate(-m_sceneCenter.x, -m_sceneCenter.y);
    return transform;
}

void Camera::visibleSceneRect(Vec2 *minCorner, Vec2 *maxCorner) const
{
    if (minCorner == nullptr || maxCorner == nullptr) {
        return;
    }
    const Vec2 topLeft = toScene(QPointF(0.0, 0.0));
    const Vec2 bottomRight = toScene(QPointF(m_viewportWidth, m_viewportHeight));
    minCorner->x = std::min(topLeft.x, bottomRight.x);
    minCorner->y = std::min(topLeft.y, bottomRight.y);
    maxCorner->x = std::max(topLeft.x, bottomRight.x);
    maxCorner->y = std::max(topLeft.y, bottomRight.y);
}

double Camera::pixelsToScene(double pixels) const
{
    if (m_zoom <= 0.0) {
        return pixels;
    }
    return pixels / m_zoom;
}

double Camera::gridStep() const
{
    // 目标：屏幕上相邻网格线间距落在 [40, 120] 像素之间。
    // 先算出"理想步长"，再向上取到 1/2/5 × 10^n 这一族，避免出现 3.7 这种刻度。
    const double targetPixels = 80.0;
    const double idealStep = targetPixels / m_zoom;
    if (!GeoMath::isFiniteNumber(idealStep) || idealStep <= 0.0) {
        return 1.0;
    }

    const double exponent = std::floor(std::log10(idealStep));
    const double base = std::pow(10.0, exponent);
    const double normalized = idealStep / base;

    double multiplier = 5.0;
    if (normalized <= 1.0) {
        multiplier = 1.0;
    } else if (normalized <= 2.0) {
        multiplier = 2.0;
    } else if (normalized <= 5.0) {
        multiplier = 5.0;
    }
    // 最小精度：每格不少于 minimumGridStep()（默认为 1 个场景单位，用户明确要求）。
    // 不设这个下限时，放大到 300% 会让网格细到 0.2 —— 于是吸附也只能吸到 0.2 的
    // 倍数，用户再也点不到整数坐标，而这恰恰是栅格作图最常用的落点。
    return std::max(m_minimumGridStep, multiplier * base);
}

bool Camera::fitToBounds(const Vec2 &minCorner, const Vec2 &maxCorner, double marginRatio)
{
    if (!minCorner.isFinite() || !maxCorner.isFinite()) {
        return false;
    }
    if (maxCorner.x < minCorner.x || maxCorner.y < minCorner.y) {
        return false;
    }
    if (m_viewportWidth <= 0 || m_viewportHeight <= 0) {
        return false;
    }

    const double width = maxCorner.x - minCorner.x;
    const double height = maxCorner.y - minCorner.y;
    const double margin = std::max(0.0, marginRatio);

    // 退化包围盒（单个点或一条水平/垂直线段）无法决定缩放，保持当前缩放只调整中心
    if (width <= GeoMath::kPositionTolerance && height <= GeoMath::kPositionTolerance) {
        centerOn(Vec2((minCorner.x + maxCorner.x) * 0.5, (minCorner.y + maxCorner.y) * 0.5));
        return true;
    }

    const double usableWidth = m_viewportWidth * (1.0 - 2.0 * margin);
    const double usableHeight = m_viewportHeight * (1.0 - 2.0 * margin);
    if (usableWidth <= 0.0 || usableHeight <= 0.0) {
        return false;
    }

    double zoom = maxZoom();
    if (width > GeoMath::kPositionTolerance) {
        zoom = std::min(zoom, usableWidth / width);
    }
    if (height > GeoMath::kPositionTolerance) {
        zoom = std::min(zoom, usableHeight / height);
    }

    setZoom(zoom);
    centerOn(Vec2((minCorner.x + maxCorner.x) * 0.5, (minCorner.y + maxCorner.y) * 0.5));
    return true;
}

void Camera::reset()
{
    m_sceneCenter = Vec2(0.0, 0.0);
    m_zoom = kDefaultZoom;
}
