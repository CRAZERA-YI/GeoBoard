/**
 * @file    model/GeometryMath.h
 * @brief   二维向量运算与几何求解的**无状态**工具集。
 * @details 本文件是几何内核的数学基础：平面点/向量、交点、投影、外接圆、
 *          角平分线、面积与距离。它不认识"图元"概念，也不持有任何状态，
 *          因此可以在没有 QApplication 的情况下独立测试。
 *
 *          不负责：图元的父子关系与重算调度（见 GeoDocument）、
 *          坐标变换（见 Camera）、绘制（见 view/CanvasWidget）。
 */

#ifndef GEOBOARDGEOMETRYMATH_H
#define GEOBOARDGEOMETRYMATH_H

#include <QList>
#include <QPointF>

/**
 * @brief 平面向量/点（双精度）。
 * @details 与 QPointF 的区别：本类型是**数学**意义上的向量，y 轴向上；
 *          QPointF 在本项目中只用于场景坐标的容器与绘制，两者可互相转换。
 */
struct Vec2
{
    double x; ///< 横坐标。
    double y; ///< 纵坐标。

    /** @brief 构造零向量。 */
    Vec2() : x(0.0), y(0.0) {}

    /**
     * @brief 由分量构造。
     * @param xValue 横坐标。
     * @param yValue 纵坐标。
     */
    Vec2(double xValue, double yValue) : x(xValue), y(yValue) {}

    /**
     * @brief 由 QPointF 构造。
     * @param point 源点。
     */
    explicit Vec2(const QPointF &point) : x(point.x()), y(point.y()) {}

    /**
     * @brief 转换为 QPointF。
     * @return 分量相同的 QPointF。
     */
    QPointF toPointF() const { return QPointF(x, y); }

    /** @brief 向量加法。 @param other 加数。 @return 和向量。 */
    Vec2 operator+(const Vec2 &other) const { return Vec2(x + other.x, y + other.y); }

    /** @brief 向量减法。 @param other 减数。 @return 差向量。 */
    Vec2 operator-(const Vec2 &other) const { return Vec2(x - other.x, y - other.y); }

    /** @brief 数乘。 @param factor 缩放系数。 @return 结果向量。 */
    Vec2 operator*(double factor) const { return Vec2(x * factor, y * factor); }

    /** @brief 数除。 @param divisor 除数，不得为 0（由调用方保证）。 @return 结果向量。 */
    Vec2 operator/(double divisor) const { return Vec2(x / divisor, y / divisor); }

    /** @brief 向量取反。 @return 反向向量。 */
    Vec2 operator-() const { return Vec2(-x, -y); }

    /**
     * @brief 点积。
     * @param other 另一个向量。
     * @return 点积结果。
     */
    double dot(const Vec2 &other) const { return x * other.x + y * other.y; }

    /**
     * @brief 二维叉积（z 分量）。
     * @param other 另一个向量。
     * @return 叉积结果；符号表示两向量的旋转方向。
     */
    double cross(const Vec2 &other) const { return x * other.y - y * other.x; }

    /**
     * @brief 向量长度。
     * @return 欧几里得模长。
     */
    double length() const;

    /**
     * @brief 长度平方。
     * @return 模长的平方；比较距离时用它可避免开方。
     */
    double lengthSquared() const { return x * x + y * y; }

    /**
     * @brief 单位化。
     * @return 单位向量；零向量返回 (0,0) 而不产生 NaN。
     */
    Vec2 normalized() const;

    /**
     * @brief 逆时针旋转 90 度。
     * @return 旋转后的向量。
     */
    Vec2 perpendicular() const { return Vec2(-y, x); }

    /**
     * @brief 两点距离。
     * @param other 另一个点。
     * @return 欧几里得距离。
     */
    double distanceTo(const Vec2 &other) const { return (*this - other).length(); }

    /**
     * @brief 判断两个点/向量是否在容差内相等。
     * @param other 另一个点。
     * @param tolerance 容差，默认 1e-9。
     * @return 分量差均不超过容差时返回 true。
     */
    bool isCloseTo(const Vec2 &other, double tolerance = 1e-9) const;

    /**
     * @brief 判断两个分量是否都是有限值（非 NaN、非 inf）。
     * @return 全部有限返回 true。
     */
    bool isFinite() const;
};

/**
 * @brief 无状态的几何求解函数集合。
 * @details 所有函数的容差语义统一：位置容差用于"点是否重合"这类判断，
 *          参数容差用于"解是否落在有界区间内"的判断（避免浮点误差把端点解判掉）。
 */
namespace GeoMath {

/** @brief 圆周率。 */
const double kPi = 3.14159265358979323846;

/** @brief 2π，用于角度规范化。 */
const double kTwoPi = 6.28318530717958647692;

/** @brief 位置容差：小于此距离视为同一点（场景单位）。 */
const double kPositionTolerance = 1e-9;

/** @brief 参数容差：单参数曲线上的参数比较容差（相对于参数区间长度）。 */
const double kParameterTolerance = 1e-9;

/**
 * @brief 判断两实数在容差内相等。
 * @param a 第一个数。
 * @param b 第二个数。
 * @param tolerance 容差。
 * @return 差值绝对值不超过容差时返回 true。
 */
bool fuzzyEquals(double a, double b, double tolerance = kPositionTolerance);

/**
 * @brief 判断实数是否为有限值。
 * @param value 待判断的数。
 * @return 有限返回 true；NaN 或 inf 返回 false。
 */
bool isFiniteNumber(double value);

/**
 * @brief 点到直线的投影点。
 * @param point 待投影的点。
 * @param lineOrigin 直线上任意一点。
 * @param lineDirection 直线方向（不要求单位长度，但不得为零向量）。
 * @return 投影点；方向为零向量时返回 lineOrigin。
 */
Vec2 projectPointOnLine(const Vec2 &point, const Vec2 &lineOrigin, const Vec2 &lineDirection);

/**
 * @brief 点到线段的最近点（在线段内投影，否则取端点）。
 * @param point 待投影的点。
 * @param segA 线段起点。
 * @param segB 线段终点。
 * @return 线段上距离 point 最近的点。
 */
Vec2 projectPointOnSegment(const Vec2 &point, const Vec2 &segA, const Vec2 &segB);

/**
 * @brief 点到射线的最近点（投影参数为负时取起点）。
 * @param point 待投影的点。
 * @param rayOrigin 射线起点。
 * @param rayThrough 射线经过的另一点。
 * @return 射线上距离 point 最近的点。
 */
Vec2 projectPointOnRay(const Vec2 &point, const Vec2 &rayOrigin, const Vec2 &rayThrough);

/**
 * @brief 点到圆的最近点（沿半径方向投影到圆周）。
 * @param point 待投影的点。
 * @param center 圆心。
 * @param radius 半径（应为正数）。
 * @return 圆周上距离 point 最近的点；半径为 0 时返回圆心。
 */
Vec2 projectPointOnCircle(const Vec2 &point, const Vec2 &center, double radius);

/**
 * @brief 点到线段的距离。
 * @param point 待测点。
 * @param segA 线段起点。
 * @param segB 线段终点。
 * @return 最短距离。
 */
double distanceToSegment(const Vec2 &point, const Vec2 &segA, const Vec2 &segB);

/**
 * @brief 三点确定的圆的圆心。
 * @param a 第一点。
 * @param b 第二点。
 * @param c 第三点。
 * @param center 输出参数：圆心；三点共线或重合时不被写入。
 * @return 三点不共线时返回 true；共线（外接圆不存在）返回 false。
 * @note  调用方必须先判断返回值再使用 center，这是"三点弧退化"的判据。
 */
bool circumcenter(const Vec2 &a, const Vec2 &b, const Vec2 &c, Vec2 *center);

/**
 * @brief 三点确定的圆的半径。
 * @param a 第一点。
 * @param b 第二点。
 * @param c 第三点。
 * @param radius 输出参数：半径；三点共线时不被写入。
 * @return 三点不共线时返回 true。
 */
bool circumradius(const Vec2 &a, const Vec2 &b, const Vec2 &c, double *radius);

/**
 * @brief 向量相对 x 轴正方向的极角。
 * @param vector 输入向量。
 * @return 弧度值，范围 (-pi, pi]；零向量返回 0。
 */
double angleOf(const Vec2 &vector);

/**
 * @brief 向量相对 x 轴正方向的极角，规范化到 [0, 2*pi)。
 * @param vector 输入向量。
 * @return 弧度值，范围 [0, 2*pi)；零向量返回 0。
 */
double normalizedAngle(double angle);

/**
 * @brief 判断角度是否落在从 start 逆时针扫到 end 的角度区间内。
 * @param angle 待判断的角度（任意实数，函数内部会规范化）。
 * @param start 区间起始角。
 * @param end 区间结束角。
 * @return 落在区间内（含端点容差）返回 true。
 * @note  本函数是圆弧绘制与圆弧求交的过滤器：弧的"存在范围"就靠它表达。
 */
bool isAngleBetween(double angle, double start, double end);

/**
 * @brief 射线/直线与圆的交点。
 * @param lineOrigin 直线上的已知点。
 * @param lineDirection 直线方向（不得为零向量）。
 * @param center 圆心。
 * @param radius 半径。
 * @param results 输出参数：求得的交点会**追加**到该列表。
 * @return 交点个数（0、1 或 2）。
 */
int intersectLineCircle(const Vec2 &lineOrigin,
                        const Vec2 &lineDirection,
                        const Vec2 &center,
                        double radius,
                        QList<Vec2> *results);

/**
 * @brief 两圆的交点。
 * @param centerA 第一个圆心。
 * @param radiusA 第一个半径。
 * @param centerB 第二个圆心。
 * @param radiusB 第二个半径。
 * @param results 输出参数：求得的交点会**追加**到该列表。
 * @return 交点个数（0、1 或 2）；两圆重合（无穷多交点）时返回 0。
 * @note  重合圆不作为约束，返回 0 让调用方把交点标记为无效。
 */
int intersectCircleCircle(const Vec2 &centerA,
                          double radiusA,
                          const Vec2 &centerB,
                          double radiusB,
                          QList<Vec2> *results);

/**
 * @brief 内角平分线的方向向量。
 * @param vertex 角的顶点。
 * @param pointOnFirstRay 第一条边上的点（非顶点）。
 * @param pointOnSecondRay 第二条边上的点（非顶点）。
 * @param direction 输出参数：单位化的内角平分线方向。
 * @return 两条边都非退化时返回 true；任一条边为零长度或两边反向共线时返回 false。
 */
bool angleBisectorDirection(const Vec2 &vertex,
                            const Vec2 &pointOnFirstRay,
                            const Vec2 &pointOnSecondRay,
                            Vec2 *direction);

/**
 * @brief 多边形的面积（鞋带公式）。
 * @param vertices 顶点列表（按顺序，不要求凸）。
 * @return 面积绝对值；顶点少于 3 个时返回 0。
 */
double polygonArea(const QList<Vec2> &vertices);

/**
 * @brief 点相对于圆的幂（用于判断点与圆的位置关系）。
 * @param point 待判断的点。
 * @param center 圆心。
 * @param radius 半径。
 * @return 距离平方与半径平方之差；负数表示点在圆内。
 */
double powerOfPoint(const Vec2 &point, const Vec2 &center, double radius);

} // namespace GeoMath

#endif // GEOBOARDGEOMETRYMATH_H
