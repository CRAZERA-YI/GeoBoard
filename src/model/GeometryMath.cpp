/**
 * @file    model/GeometryMath.cpp
 * @brief   二维向量运算与几何求解的实现。
 * @details 与头文件同名的实现文件。所有函数都不持有状态，可重入。
 */

#include "GeometryMath.h"

#include <cmath>

namespace {

/** @brief 极坐标规范化：把任意角度折到 [0, 2π)。 */
double normalizeToTwoPi(double angle)
{
    // 刻意不用 M_PI：它不是 C++ 标准宏（MinGW 的 <cmath> 在严格模式下不提供，
    // MSVC 却提供），依赖它会让同一份代码在不同编译器下表现不一致。
    const double twoPi = GeoMath::kTwoPi;
    double result = std::fmod(angle, twoPi);
    if (result < 0.0) {
        result += twoPi;
    }
    return result;
}

} // namespace

double Vec2::length() const
{
    return std::sqrt(x * x + y * y);
}

Vec2 Vec2::normalized() const
{
    const double len = length();
    // 零向量不参与除法：返回零向量而不是产生 NaN，让退化在调用方被显式判断
    if (len <= GeoMath::kPositionTolerance) {
        return Vec2(0.0, 0.0);
    }
    return Vec2(x / len, y / len);
}

bool Vec2::isCloseTo(const Vec2 &other, double tolerance) const
{
    return std::fabs(x - other.x) <= tolerance && std::fabs(y - other.y) <= tolerance;
}

bool Vec2::isFinite() const
{
    return GeoMath::isFiniteNumber(x) && GeoMath::isFiniteNumber(y);
}

namespace GeoMath {

bool fuzzyEquals(double a, double b, double tolerance)
{
    return std::fabs(a - b) <= tolerance;
}

bool isFiniteNumber(double value)
{
    // 不能用 std::isfinite：MSVC 的 <cmath> 在 C++11 下把它放在 std 命名空间，
    // 但为了同时兼容 MinGW 的宏实现，这里用自比较排除 NaN，再排除 inf。
    if (value != value) {
        return false; // NaN
    }
    return value <= std::numeric_limits<double>::max() && value >= -std::numeric_limits<double>::max();
}

Vec2 projectPointOnLine(const Vec2 &point, const Vec2 &lineOrigin, const Vec2 &lineDirection)
{
    const double denominator = lineDirection.lengthSquared();
    if (denominator <= kPositionTolerance * kPositionTolerance) {
        return lineOrigin; // 方向退化：把整个直线视为一个点
    }
    const double t = (point - lineOrigin).dot(lineDirection) / denominator;
    return lineOrigin + lineDirection * t;
}

Vec2 projectPointOnSegment(const Vec2 &point, const Vec2 &segA, const Vec2 &segB)
{
    const Vec2 direction = segB - segA;
    const double denominator = direction.lengthSquared();
    if (denominator <= kPositionTolerance * kPositionTolerance) {
        return segA; // 退化成点
    }
    double t = (point - segA).dot(direction) / denominator;
    // 线段是有界的：把参数夹到 [0,1]，否则附着点会跑到线段延长线上
    if (t < 0.0) {
        t = 0.0;
    } else if (t > 1.0) {
        t = 1.0;
    }
    return segA + direction * t;
}

Vec2 projectPointOnRay(const Vec2 &point, const Vec2 &rayOrigin, const Vec2 &rayThrough)
{
    const Vec2 direction = rayThrough - rayOrigin;
    const double denominator = direction.lengthSquared();
    if (denominator <= kPositionTolerance * kPositionTolerance) {
        return rayOrigin;
    }
    double t = (point - rayOrigin).dot(direction) / denominator;
    // 射线是半无界的：t < 0 的部分不存在，夹到起点
    if (t < 0.0) {
        t = 0.0;
    }
    return rayOrigin + direction * t;
}

Vec2 projectPointOnCircle(const Vec2 &point, const Vec2 &center, double radius)
{
    const Vec2 offset = point - center;
    const double distance = offset.length();
    if (distance <= kPositionTolerance || radius <= kPositionTolerance) {
        // 点在圆心上（方向不定）或半径为零：只能返回一个确定点，避免 NaN
        return Vec2(center.x + radius, center.y);
    }
    return center + offset * (radius / distance);
}

double distanceToSegment(const Vec2 &point, const Vec2 &segA, const Vec2 &segB)
{
    return point.distanceTo(projectPointOnSegment(point, segA, segB));
}

bool circumcenter(const Vec2 &a, const Vec2 &b, const Vec2 &c, Vec2 *center)
{
    if (center == nullptr) {
        return false;
    }

    // 用两条中垂线求交。d 是两个方向向量的叉积，三点共线时 d 为 0（外接圆不存在）
    const Vec2 ab = b - a;
    const Vec2 ac = c - a;
    const double d = 2.0 * ab.cross(ac);
    if (std::fabs(d) <= kPositionTolerance) {
        return false;
    }

    const double abLengthSquared = ab.lengthSquared();
    const double acLengthSquared = ac.lengthSquared();

    const double ux = (ac.y * abLengthSquared - ab.y * acLengthSquared) / d;
    const double uy = (ab.x * acLengthSquared - ac.x * abLengthSquared) / d;

    center->x = a.x + ux;
    center->y = a.y + uy;
    return center->isFinite();
}

bool circumradius(const Vec2 &a, const Vec2 &b, const Vec2 &c, double *radius)
{
    Vec2 center;
    if (!circumcenter(a, b, c, &center)) {
        return false;
    }
    if (radius == nullptr) {
        return false;
    }
    *radius = center.distanceTo(a);
    return isFiniteNumber(*radius);
}

double angleOf(const Vec2 &vector)
{
    return std::atan2(vector.y, vector.x);
}

double normalizedAngle(double angle)
{
    return normalizeToTwoPi(angle);
}

bool isAngleBetween(double angle, double start, double end)
{
    const double sweep = normalizeToTwoPi(end - start);
    const double offset = normalizeToTwoPi(angle - start);
    // sweep 为 0 表示"整圆"这一退化输入，此时任何角度都算在区间内
    if (sweep <= kParameterTolerance) {
        return true;
    }
    return offset <= sweep + kParameterTolerance;
}

int intersectLineCircle(const Vec2 &lineOrigin,
                        const Vec2 &lineDirection,
                        const Vec2 &center,
                        double radius,
                        QList<Vec2> *results)
{
    if (results == nullptr || radius < 0.0) {
        return 0;
    }

    const double a = lineDirection.lengthSquared();
    if (a <= kPositionTolerance * kPositionTolerance) {
        return 0; // 方向退化，不构成曲线
    }

    const Vec2 offset = lineOrigin - center;
    const double b = 2.0 * offset.dot(lineDirection);
    const double c = offset.lengthSquared() - radius * radius;
    const double discriminant = b * b - 4.0 * a * c;

    if (discriminant < -kPositionTolerance) {
        return 0; // 直线与圆不相交
    }

    if (std::fabs(discriminant) <= kPositionTolerance) {
        // 相切：只有一个交点，数值上极易漏判，必须单独处理
        const Vec2 hit = lineOrigin + lineDirection * (-b / (2.0 * a));
        if (hit.isFinite()) {
            results->append(hit);
            return 1;
        }
        return 0;
    }

    const double root = std::sqrt(discriminant);
    int count = 0;
    const double ts[2] = {(-b - root) / (2.0 * a), (-b + root) / (2.0 * a)};
    for (int i = 0; i < 2; ++i) {
        const Vec2 hit = lineOrigin + lineDirection * ts[i];
        if (hit.isFinite()) {
            results->append(hit);
            ++count;
        }
    }
    return count;
}

int intersectCircleCircle(const Vec2 &centerA,
                          double radiusA,
                          const Vec2 &centerB,
                          double radiusB,
                          QList<Vec2> *results)
{
    if (results == nullptr || radiusA < 0.0 || radiusB < 0.0) {
        return 0;
    }

    const Vec2 delta = centerB - centerA;
    const double distance = delta.length();

    // 同心：半径相同则重合（无穷多交点，不作为约束），半径不同则无交点
    if (distance <= kPositionTolerance) {
        return 0;
    }
    if (distance > radiusA + radiusB + kPositionTolerance) {
        return 0; // 相离
    }
    if (distance < std::fabs(radiusA - radiusB) - kPositionTolerance) {
        return 0; // 内含
    }

    // 以两圆心连线为 x 轴：a 是交点到 centerA 连线上的投影长度，h 是半弦长
    const double a = (radiusA * radiusA - radiusB * radiusB + distance * distance) / (2.0 * distance);
    const double hSquared = radiusA * radiusA - a * a;
    const Vec2 unit = delta / distance;
    const Vec2 foot = centerA + unit * a;

    if (hSquared <= kPositionTolerance * kPositionTolerance) {
        // 相切：两圆只有一个交点
        if (foot.isFinite()) {
            results->append(foot);
            return 1;
        }
        return 0;
    }

    const double h = std::sqrt(hSquared);
    const Vec2 offset = unit.perpendicular() * h;
    int count = 0;
    const Vec2 candidates[2] = {foot + offset, foot - offset};
    for (int i = 0; i < 2; ++i) {
        if (candidates[i].isFinite()) {
            results->append(candidates[i]);
            ++count;
        }
    }
    return count;
}

bool angleBisectorDirection(const Vec2 &vertex,
                            const Vec2 &pointOnFirstRay,
                            const Vec2 &pointOnSecondRay,
                            Vec2 *direction)
{
    if (direction == nullptr) {
        return false;
    }

    const Vec2 first = (pointOnFirstRay - vertex).normalized();
    const Vec2 second = (pointOnSecondRay - vertex).normalized();
    if (first.lengthSquared() <= kPositionTolerance || second.lengthSquared() <= kPositionTolerance) {
        return false; // 有一条边退化为点，角不存在
    }

    Vec2 sum = first + second;
    if (sum.lengthSquared() <= kPositionTolerance * kPositionTolerance) {
        // 两条边反向（180 度角）：平分线方向不唯一（内角平分线垂直于两边）
        *direction = first.perpendicular();
        return direction->lengthSquared() > kPositionTolerance * kPositionTolerance;
    }

    *direction = sum.normalized();
    return true;
}

double polygonArea(const QList<Vec2> &vertices)
{
    const int count = vertices.size();
    if (count < 3) {
        return 0.0;
    }

    double twiceArea = 0.0;
    for (int i = 0; i < count; ++i) {
        const Vec2 &current = vertices.at(i);
        const Vec2 &next = vertices.at((i + 1) % count);
        twiceArea += current.cross(next);
    }
    return std::fabs(twiceArea) * 0.5;
}

double powerOfPoint(const Vec2 &point, const Vec2 &center, double radius)
{
    const double distanceSquared = (point - center).lengthSquared();
    return distanceSquared - radius * radius;
}

} // namespace GeoMath
