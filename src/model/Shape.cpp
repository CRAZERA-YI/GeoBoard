/**
 * @file    model/Shape.cpp
 * @brief   图元的几何推导实现。
 * @details 每个图元的 recompute() 都遵循同一条纪律：**先判断退化，再算几何**。
 *          任何退化（父缺失、共线、无交点、零长度）都只把几何标记为无效，
 *          绝不抛异常、不安断、不删除图元——这样拖动经过退化位形时可以自动恢复。
 */

#include "Shape.h"

#include "GeoDocument.h"

#include <QCoreApplication>
#include <QStringList>

#include <cmath>
#include <cstdio>

namespace {

/** @brief 角度转度数的换算系数。 */
const double kRadiansToDegrees = 180.0 / GeoMath::kPi;

/**
 * @brief 按测量类型格式化数值文本。
 * @param kind 测量类型（坐标类型不走本函数）。
 * @param value 数值（角度为弧度，其余为场景单位）。
 * @return 供界面显示的文本，长度类带场景单位后缀。
 */
QString formatMeasurement(MeasurementKind kind, double value)
{
    switch (kind) {
    case MeasurementKind::Angle:
        return QStringLiteral("%1°").arg(value * kRadiansToDegrees, 0, 'f', 1);
    case MeasurementKind::Area:
        return QStringLiteral("%1 u²").arg(value, 0, 'f', 2);
    case MeasurementKind::Circumference:
    case MeasurementKind::Radius:
    case MeasurementKind::Distance:
    case MeasurementKind::SegmentLength:
        return QStringLiteral("%1 u").arg(value, 0, 'f', 2);
    case MeasurementKind::PointCoordinates:
        break;
    }
    return QString::number(value, 'f', 2);
}

} // namespace

// ---------------------------------------------------------------------------
// LineData
// ---------------------------------------------------------------------------

double LineData::parameterAt(const Vec2 &point) const
{
    const double denominator = direction.lengthSquared();
    if (denominator <= GeoMath::kPositionTolerance * GeoMath::kPositionTolerance) {
        return 0.0; // 方向退化：整条线视为一个点，参数无意义
    }
    // 先把点投影到直线上再取参数，这样"点略微偏离直线"时也能得到稳定的参数
    const Vec2 projected = GeoMath::projectPointOnLine(point, origin, direction);
    return (projected - origin).dot(direction) / denominator;
}

// ---------------------------------------------------------------------------
// Shape
// ---------------------------------------------------------------------------

Shape::Shape(ShapeId id, ShapeType type)
    : m_geometryValid(false)
    , m_id(id)
    , m_type(type)
    , m_enabled(true)
{
}

Shape::~Shape() = default;

bool Shape::isValid() const
{
    return m_enabled && m_geometryValid;
}

bool Shape::parentsValid(const GeoDocument &document) const
{
    for (int i = 0; i < m_parents.size(); ++i) {
        const Shape *parent = document.shape(m_parents.at(i));
        if (parent == nullptr || !parent->isValid()) {
            return false;
        }
    }
    return true;
}

void Shape::copyIdentityFrom(const Shape &source)
{
    m_id = source.m_id;
    m_type = source.m_type;
    m_enabled = source.m_enabled;
    m_parents = source.m_parents;
    m_geometryValid = source.m_geometryValid;
}

// ---------------------------------------------------------------------------
// PointShape
// ---------------------------------------------------------------------------

PointShape::PointShape(ShapeId id, PointKind kind, const Vec2 &position)
    : Shape(id, ShapeType::Point)
    , m_kind(kind)
    , m_position(position)
{
    // 附着点的父曲线 id 不在构造参数里：由 GeoDocument 接管时经 setHostId 登记。
    // 在登记之前 m_parents 为空，recompute 会把附着点当作位置固定的点处理，
    // 不会产生"父 id 为 0"这种非法依赖。
    m_geometryValid = position.isFinite();
}

Shape *PointShape::clone() const
{
    PointShape *copy = new PointShape(ownId(), m_kind, m_position);
    copy->copyIdentityFrom(*this);
    return copy;
}

ShapeId PointShape::hostId() const
{
    if (m_kind != PointKind::OnObject || m_parents.isEmpty()) {
        return kInvalidShapeId;
    }
    return m_parents.first();
}

void PointShape::setHostId(ShapeId hostId)
{
    m_parents.clear();
    if (hostId != kInvalidShapeId) {
        m_parents.append(hostId);
    }
}

void PointShape::recompute(const GeoDocument &document)
{
    if (m_kind == PointKind::Free || m_parents.isEmpty()) {
        m_geometryValid = m_position.isFinite();
        return;
    }

    const ShapeId host = m_parents.first();
    const Shape *hostShape = document.shape(host);
    if (hostShape == nullptr || !hostShape->isValid()) {
        m_geometryValid = false;
        return;
    }

    // 附着点始终被投影到宿主曲线上，因此"点贴在线上"是结构性保证，
    // 而不是靠用户在拖动时对准——这是动态几何软件的核心手感来源。
    switch (hostShape->type()) {
    case ShapeType::Line:
    case ShapeType::PerpendicularLine:
    case ShapeType::ParallelLine:
    case ShapeType::AngleBisector: {
        const LineData &line = static_cast<const LineShape *>(hostShape)->lineData();
        if (!line.isDefined) {
            m_geometryValid = false;
            return;
        }
        if (line.direction.lengthSquared() <= GeoMath::kPositionTolerance * GeoMath::kPositionTolerance) {
            m_geometryValid = false;
            return;
        }
        // 无界维度不裁剪，有界维度夹到 [0,1]，由 LineData::clampParameter 统一处理。
        // 参数换算只走 parameterAt，避免与交点求解各用一套参数约定。
        const double t = line.clampParameter(line.parameterAt(m_position));
        m_position = line.origin + line.direction * t;
        m_geometryValid = m_position.isFinite();
        return;
    }
    case ShapeType::Circle: {
        const CircleData &circle = static_cast<const CircleShape *>(hostShape)->circleData();
        if (!circle.isDefined || circle.radius <= GeoMath::kPositionTolerance) {
            m_geometryValid = false;
            return;
        }
        m_position = GeoMath::projectPointOnCircle(m_position, circle.center, circle.radius);
        m_geometryValid = m_position.isFinite();
        return;
    }
    case ShapeType::Arc: {
        const ArcData &arc = static_cast<const ArcShape *>(hostShape)->arcData();
        if (!arc.isDefined || arc.radius <= GeoMath::kPositionTolerance) {
            m_geometryValid = false;
            return;
        }
        Vec2 projected = GeoMath::projectPointOnCircle(m_position, arc.center, arc.radius);
        const double projectedAngle = GeoMath::angleOf(projected - arc.center);
        if (!GeoMath::isAngleBetween(projectedAngle, arc.startAngle, arc.endAngle)) {
            // 投影落在弧的缺口一侧：改投到较近的端点，保证点始终在弧上
            const Vec2 startPoint = arc.center
                + Vec2(std::cos(arc.startAngle), std::sin(arc.startAngle)) * arc.radius;
            const Vec2 endPoint = arc.center
                + Vec2(std::cos(arc.endAngle), std::sin(arc.endAngle)) * arc.radius;
            projected = (m_position.distanceTo(startPoint) <= m_position.distanceTo(endPoint))
                            ? startPoint
                            : endPoint;
        }
        m_position = projected;
        m_geometryValid = m_position.isFinite();
        return;
    }
    case ShapeType::Polygon: {
        const PolygonShape *polygon = static_cast<const PolygonShape *>(hostShape);
        const QList<ShapeId> &vertexIds = polygon->vertexPointIds();
        const int count = vertexIds.size();
        if (count < 3) {
            m_geometryValid = false;
            return;
        }
        // 多边形边上滑动：逐条边求最近点，取全局最近的那条
        Vec2 best = m_position;
        double bestDistance = -1.0;
        for (int i = 0; i < count; ++i) {
            const PointShape *first = document.point(vertexIds.at(i));
            const PointShape *second = document.point(vertexIds.at((i + 1) % count));
            if (first == nullptr || second == nullptr) {
                continue;
            }
            const Vec2 candidate = GeoMath::projectPointOnSegment(
                m_position, first->position(), second->position());
            const double distance = m_position.distanceTo(candidate);
            if (bestDistance < 0.0 || distance < bestDistance) {
                bestDistance = distance;
                best = candidate;
            }
        }
        if (bestDistance < 0.0) {
            m_geometryValid = false;
            return;
        }
        m_position = best;
        m_geometryValid = m_position.isFinite();
        return;
    }
    default:
        break;
    }

    // 宿主是点、中点、交点、测量或未知类型：不构成"曲线"，附着无意义
    m_geometryValid = false;
}

// ---------------------------------------------------------------------------
// LineShape
// ---------------------------------------------------------------------------

LineShape::LineShape(ShapeId id, LineKind kind, ShapeId firstPointId, ShapeId secondPointId)
    : Shape(id, ShapeType::Line)
    , m_lineKind(kind)
    , m_firstPointId(firstPointId)
    , m_secondPointId(secondPointId)
    , m_thirdPointId(kInvalidShapeId)
    , m_baseCurveId(kInvalidShapeId)
{
    m_parents << firstPointId << secondPointId;
}

LineShape::LineShape(ShapeId id, ShapeType type, ShapeId baseCurveId, ShapeId throughPointId)
    : Shape(id, type)
    , m_lineKind(LineKind::InfiniteLine) // 派生线一律按无限直线处理
    , m_firstPointId(baseCurveId)
    , m_secondPointId(throughPointId)
    , m_thirdPointId(kInvalidShapeId)
    , m_baseCurveId(baseCurveId)
{
    m_parents << baseCurveId << throughPointId;
}

LineShape::LineShape(ShapeId id, ShapeId vertexPointId, ShapeId firstRayPointId, ShapeId secondRayPointId)
    : Shape(id, ShapeType::AngleBisector)
    , m_lineKind(LineKind::InfiniteLine)
    , m_firstPointId(vertexPointId)
    , m_secondPointId(firstRayPointId)
    , m_thirdPointId(secondRayPointId)
    , m_baseCurveId(kInvalidShapeId)
{
    m_parents << vertexPointId << firstRayPointId << secondRayPointId;
}

Shape *LineShape::clone() const
{
    LineShape *copy = nullptr;
    switch (ownType()) {
    case ShapeType::PerpendicularLine:
    case ShapeType::ParallelLine:
        copy = new LineShape(ownId(), ownType(), m_baseCurveId, m_secondPointId);
        break;
    case ShapeType::AngleBisector:
        copy = new LineShape(ownId(), m_firstPointId, m_secondPointId, m_thirdPointId);
        break;
    default:
        copy = new LineShape(ownId(), m_lineKind, m_firstPointId, m_secondPointId);
        break;
    }
    copy->copyIdentityFrom(*this);
    return copy;
}

void LineShape::recompute(const GeoDocument &document)
{
    m_lineData.isDefined = false;

    if (!parentsValid(document)) {
        m_geometryValid = false;
        return;
    }

    switch (ownType()) {
    case ShapeType::Line: {
        const PointShape *first = document.point(m_firstPointId);
        const PointShape *second = document.point(m_secondPointId);
        if (first == nullptr || second == nullptr) {
            m_geometryValid = false;
            return;
        }
        m_lineData.origin = first->position();
        m_lineData.direction = second->position() - first->position();
        m_lineData.hasStartBound = (m_lineKind != LineKind::InfiniteLine);
        m_lineData.hasEndBound = (m_lineKind == LineKind::Segment);
        if (m_lineData.direction.lengthSquared()
            <= GeoMath::kPositionTolerance * GeoMath::kPositionTolerance) {
            m_geometryValid = false; // 两点重合：线不存在
            return;
        }
        m_lineData.isDefined = true;
        m_geometryValid = true;
        return;
    }
    case ShapeType::PerpendicularLine:
    case ShapeType::ParallelLine: {
        const Shape *base = document.curve(m_baseCurveId);
        const PointShape *through = document.point(m_secondPointId);
        if (base == nullptr || through == nullptr) {
            m_geometryValid = false;
            return;
        }

        // "平行/垂直"只对直的曲线有定义：基准是圆或弧时该派生线无效
        // （需求与设计文档都未把"平行于圆"列为功能，这里显式判无效而非猜测语义）
        const ShapeType baseType = base->type();
        if (baseType != ShapeType::Line && baseType != ShapeType::PerpendicularLine
            && baseType != ShapeType::ParallelLine && baseType != ShapeType::AngleBisector) {
            m_geometryValid = false;
            return;
        }
        const LineData baseLine = static_cast<const LineShape *>(base)->lineData();
        if (!baseLine.isDefined) {
            m_geometryValid = false;
            return;
        }

        m_lineData.origin = through->position();
        m_lineData.direction = (ownType() == ShapeType::PerpendicularLine)
                                   ? baseLine.direction.perpendicular()
                                   : baseLine.direction;
        m_lineData.hasStartBound = false;
        m_lineData.hasEndBound = false;
        if (m_lineData.direction.lengthSquared()
            <= GeoMath::kPositionTolerance * GeoMath::kPositionTolerance) {
            m_geometryValid = false;
            return;
        }
        m_lineData.isDefined = true;
        m_geometryValid = true;
        return;
    }
    case ShapeType::AngleBisector: {
        const PointShape *vertex = document.point(m_firstPointId);
        const PointShape *firstRay = document.point(m_secondPointId);
        const PointShape *secondRay = document.point(m_thirdPointId);
        if (vertex == nullptr || firstRay == nullptr || secondRay == nullptr) {
            m_geometryValid = false;
            return;
        }
        Vec2 direction;
        if (!GeoMath::angleBisectorDirection(vertex->position(), firstRay->position(),
                                            secondRay->position(), &direction)) {
            m_geometryValid = false;
            return;
        }
        m_lineData.origin = vertex->position();
        m_lineData.direction = direction;
        m_lineData.hasStartBound = true; // 角平分线是射线：只从顶点向外
        m_lineData.hasEndBound = false;
        m_lineData.isDefined = true;
        m_geometryValid = true;
        return;
    }
    default:
        break;
    }

    m_geometryValid = false;
}

// ---------------------------------------------------------------------------
// CircleShape
// ---------------------------------------------------------------------------

CircleShape::CircleShape(ShapeId id, ShapeId centerPointId, ShapeId rimPointId)
    : Shape(id, ShapeType::Circle)
    , m_centerPointId(centerPointId)
    , m_rimPointId(rimPointId)
{
    m_parents << centerPointId << rimPointId;
}

Shape *CircleShape::clone() const
{
    CircleShape *copy = new CircleShape(ownId(), m_centerPointId, m_rimPointId);
    copy->copyIdentityFrom(*this);
    return copy;
}

void CircleShape::recompute(const GeoDocument &document)
{
    m_circleData.isDefined = false;

    if (!parentsValid(document)) {
        m_geometryValid = false;
        return;
    }

    const PointShape *center = document.point(m_centerPointId);
    const PointShape *rim = document.point(m_rimPointId);
    if (center == nullptr || rim == nullptr) {
        m_geometryValid = false;
        return;
    }

    m_circleData.center = center->position();
    m_circleData.radius = center->position().distanceTo(rim->position());
    if (m_circleData.radius <= GeoMath::kPositionTolerance) {
        m_geometryValid = false; // 圆上点与圆心重合：圆退化为点
        return;
    }

    m_circleData.isDefined = true;
    m_geometryValid = true;
}

// ---------------------------------------------------------------------------
// ArcShape
// ---------------------------------------------------------------------------

ArcShape::ArcShape(ShapeId id, ShapeId startPointId, ShapeId middlePointId, ShapeId endPointId)
    : Shape(id, ShapeType::Arc)
    , m_kind(ArcKind::ThreePoint)
    , m_startPointId(startPointId)
    , m_middlePointId(middlePointId)
    , m_endPointId(endPointId)
{
    m_parents << startPointId << middlePointId << endPointId;
}

ArcShape::ArcShape(ShapeId id,
                   ShapeId centerPointId,
                   ShapeId radiusPointId,
                   ShapeId arcPointId,
                   ArcKind kind)
    : Shape(id, ShapeType::Arc)
    , m_kind(kind)
    , m_startPointId(centerPointId)
    , m_middlePointId(arcPointId)
    , m_endPointId(radiusPointId)
{
    // 父集合的登记顺序不影响语义（依赖图只关心"依赖谁"），
    // 但保持一致有助于调试时阅读
    m_parents << centerPointId << arcPointId << radiusPointId;
}

Shape *ArcShape::clone() const
{
    ArcShape *copy = nullptr;
    if (m_kind == ArcKind::CenterTwoPoint) {
        copy = new ArcShape(ownId(), m_startPointId, m_endPointId, m_middlePointId,
                            ArcKind::CenterTwoPoint);
    } else {
        copy = new ArcShape(ownId(), m_startPointId, m_middlePointId, m_endPointId);
    }
    copy->copyIdentityFrom(*this);
    return copy;
}

void ArcShape::recompute(const GeoDocument &document)
{
    m_arcData.isDefined = false;

    if (!parentsValid(document)) {
        m_geometryValid = false;
        return;
    }

    const PointShape *start = document.point(m_startPointId);
    const PointShape *middle = document.point(m_middlePointId);
    const PointShape *end = document.point(m_endPointId);
    if (start == nullptr || middle == nullptr || end == nullptr) {
        m_geometryValid = false;
        return;
    }

    if (m_kind == ArcKind::CenterTwoPoint) {
        // 圆心 + 两点：start 是圆心，end 是半径点，middle 决定逆时针扫到哪里。
        // 圆由此直接确定，**不再依赖三点是否共线**——这正是该构造方式更稳的原因。
        const Vec2 center = start->position();
        const Vec2 radiusPoint = end->position();
        const double radius = center.distanceTo(radiusPoint);
        if (radius <= GeoMath::kPositionTolerance) {
            m_geometryValid = false; // 半径点与圆心重合：圆退化为点
            return;
        }

        m_arcData.center = center;
        m_arcData.radius = radius;

        const double startAngle = GeoMath::angleOf(radiusPoint - center);
        // 弧上点与半径点重合时说明用户还没给出有效范围，按整圆处理
        const double endAngle =
            middle->position().isCloseTo(radiusPoint, GeoMath::kPositionTolerance)
                ? startAngle
                : GeoMath::angleOf(middle->position() - center);
        m_arcData.startAngle = startAngle;
        m_arcData.endAngle = endAngle;
        m_arcData.isDefined = true;
        m_geometryValid = true;
        return;
    }

    Vec2 center;
    double radius = 0.0;
    if (!GeoMath::circumcenter(start->position(), middle->position(), end->position(), &center)
        || !GeoMath::circumradius(start->position(), middle->position(), end->position(), &radius)
        || radius <= GeoMath::kPositionTolerance) {
        m_geometryValid = false; // 三点共线：外接圆不存在，弧不存在
        return;
    }

    m_arcData.center = center;
    m_arcData.radius = radius;

    const double startAngle = GeoMath::angleOf(start->position() - center);
    const double endAngle = GeoMath::angleOf(end->position() - center);
    const double middleAngle = GeoMath::angleOf(middle->position() - center);

    // 弧的方向由"哪一段经过弧上点"决定：逆时针一段不含弧上点时，就取顺时针一段。
    // 这样用户点击顺序无关紧要，弧永远是"看起来该是的那一段"。
    if (GeoMath::isAngleBetween(middleAngle, startAngle, endAngle)) {
        m_arcData.startAngle = startAngle;
        m_arcData.endAngle = endAngle;
    } else {
        m_arcData.startAngle = endAngle;
        m_arcData.endAngle = startAngle;
    }

    m_arcData.isDefined = true;
    m_geometryValid = true;
}

// ---------------------------------------------------------------------------
// PolygonShape
// ---------------------------------------------------------------------------

PolygonShape::PolygonShape(ShapeId id, const QList<ShapeId> &vertexPointIds)
    : Shape(id, ShapeType::Polygon)
    , m_vertexPointIds(vertexPointIds)
{
    m_parents = vertexPointIds;
}

Shape *PolygonShape::clone() const
{
    PolygonShape *copy = new PolygonShape(ownId(), m_vertexPointIds);
    copy->copyIdentityFrom(*this);
    return copy;
}

void PolygonShape::recompute(const GeoDocument &document)
{
    m_vertices.clear();

    if (m_vertexPointIds.size() < 3 || !parentsValid(document)) {
        m_geometryValid = false;
        return;
    }

    for (int i = 0; i < m_vertexPointIds.size(); ++i) {
        const PointShape *vertex = document.point(m_vertexPointIds.at(i));
        if (vertex == nullptr) {
            m_vertices.clear();
            m_geometryValid = false;
            return;
        }
        m_vertices.append(vertex->position());
    }

    m_geometryValid = true;
}

// ---------------------------------------------------------------------------
// MidPointShape
// ---------------------------------------------------------------------------

MidPointShape::MidPointShape(ShapeId id, ShapeId firstPointId, ShapeId secondPointId)
    : Shape(id, ShapeType::MidPoint)
    , m_firstPointId(firstPointId)
    , m_secondPointId(secondPointId)
{
    m_parents << firstPointId << secondPointId;
}

Shape *MidPointShape::clone() const
{
    MidPointShape *copy = new MidPointShape(ownId(), m_firstPointId, m_secondPointId);
    copy->copyIdentityFrom(*this);
    return copy;
}

void MidPointShape::recompute(const GeoDocument &document)
{
    Vec2 first;
    Vec2 second;
    if (!document.pointsOf(m_firstPointId, m_secondPointId, &first, &second)) {
        m_geometryValid = false;
        return;
    }
    m_position = (first + second) * 0.5;
    m_geometryValid = m_position.isFinite();
}

// ---------------------------------------------------------------------------
// IntersectionPointShape
// ---------------------------------------------------------------------------

IntersectionPointShape::IntersectionPointShape(ShapeId id,
                                               ShapeId firstCurveId,
                                               ShapeId secondCurveId,
                                               int index)
    : Shape(id, ShapeType::IntersectionPoint)
    , m_firstCurveId(firstCurveId)
    , m_secondCurveId(secondCurveId)
    , m_index(index < 0 ? 0 : index)
{
    m_parents << firstCurveId << secondCurveId;
}

Shape *IntersectionPointShape::clone() const
{
    IntersectionPointShape *copy =
        new IntersectionPointShape(ownId(), m_firstCurveId, m_secondCurveId, m_index);
    copy->copyIdentityFrom(*this);
    return copy;
}

void IntersectionPointShape::recompute(const GeoDocument &document)
{
    QList<Vec2> intersections;
    const int count = document.curveIntersections(m_firstCurveId, m_secondCurveId, &intersections);
    if (count <= m_index) {
        // 该序号的交点当前不存在（两曲线暂时分开、相切后退化等）。
        // 标记无效而非删除：位形恢复时交点会自动回来。
        m_geometryValid = false;
        return;
    }
    m_position = intersections.at(m_index);
    m_geometryValid = m_position.isFinite();
}

// ---------------------------------------------------------------------------
// MeasurementShape
// ---------------------------------------------------------------------------

MeasurementShape::MeasurementShape(ShapeId id,
                                   MeasurementKind kind,
                                   const QList<ShapeId> &inputPointIds,
                                   ShapeId referenceShapeId,
                                   const Vec2 &anchor)
    : Shape(id, ShapeType::Measurement)
    , m_kind(kind)
    , m_inputPointIds(inputPointIds)
    , m_referenceShapeId(referenceShapeId)
    , m_anchor(anchor)
    , m_value(0.0)
    , m_coordinateY(0.0)
{
    m_parents = inputPointIds;
    if (referenceShapeId != kInvalidShapeId && !m_parents.contains(referenceShapeId)) {
        m_parents.append(referenceShapeId);
    }
}

Shape *MeasurementShape::clone() const
{
    MeasurementShape *copy =
        new MeasurementShape(ownId(), m_kind, m_inputPointIds, m_referenceShapeId, m_anchor);
    copy->copyIdentityFrom(*this);
    copy->m_value = m_value;
    copy->m_coordinateY = m_coordinateY;
    return copy;
}

void MeasurementShape::recompute(const GeoDocument &document)
{
    if (!parentsValid(document)) {
        m_geometryValid = false;
        return;
    }

    switch (m_kind) {
    case MeasurementKind::Distance: {
        if (m_inputPointIds.size() < 2) {
            m_geometryValid = false;
            return;
        }
        Vec2 first;
        Vec2 second;
        if (!document.pointsOf(m_inputPointIds.at(0), m_inputPointIds.at(1), &first, &second)) {
            m_geometryValid = false;
            return;
        }
        m_value = first.distanceTo(second);
        m_geometryValid = true;
        return;
    }
    case MeasurementKind::SegmentLength: {
        const Shape *reference = document.shape(m_referenceShapeId);
        if (reference == nullptr || reference->type() != ShapeType::Line) {
            m_geometryValid = false;
            return;
        }
        const LineShape *line = static_cast<const LineShape *>(reference);
        const PointShape *first = document.point(line->firstPointId());
        const PointShape *second = document.point(line->secondPointId());
        if (first == nullptr || second == nullptr) {
            m_geometryValid = false;
            return;
        }
        m_value = first->position().distanceTo(second->position());
        m_geometryValid = true;
        return;
    }
    case MeasurementKind::Angle: {
        if (m_inputPointIds.size() < 3) {
            m_geometryValid = false;
            return;
        }
        const PointShape *vertexPoint = document.point(m_inputPointIds.at(1));
        const PointShape *firstPoint = document.point(m_inputPointIds.at(0));
        const PointShape *secondPoint = document.point(m_inputPointIds.at(2));
        if (vertexPoint == nullptr || firstPoint == nullptr || secondPoint == nullptr) {
            m_geometryValid = false;
            return;
        }
        const Vec2 vertex = vertexPoint->position();
        const Vec2 firstEdge = (firstPoint->position() - vertex).normalized();
        const Vec2 secondEdge = (secondPoint->position() - vertex).normalized();
        if (firstEdge.lengthSquared() <= GeoMath::kPositionTolerance
            || secondEdge.lengthSquared() <= GeoMath::kPositionTolerance) {
            m_geometryValid = false; // 顶点与边上点重合：角不存在
            return;
        }
        double cosine = firstEdge.dot(secondEdge);
        // 浮点误差会让 cos 略微越过 ±1，acos 会返回 NaN，必须夹紧
        if (cosine > 1.0) {
            cosine = 1.0;
        } else if (cosine < -1.0) {
            cosine = -1.0;
        }
        m_value = std::acos(cosine);
        m_geometryValid = true;
        return;
    }
    case MeasurementKind::Radius:
    case MeasurementKind::Circumference: {
        const Shape *reference = document.shape(m_referenceShapeId);
        if (reference == nullptr || reference->type() != ShapeType::Circle) {
            m_geometryValid = false;
            return;
        }
        const CircleData &circle = static_cast<const CircleShape *>(reference)->circleData();
        if (!circle.isDefined) {
            m_geometryValid = false;
            return;
        }
        m_value = (m_kind == MeasurementKind::Radius) ? circle.radius : 2.0 * GeoMath::kPi * circle.radius;
        m_geometryValid = true;
        return;
    }
    case MeasurementKind::Area: {
        const Shape *reference = document.shape(m_referenceShapeId);
        if (reference == nullptr) {
            m_geometryValid = false;
            return;
        }

        // 面积有两种来源：多边形（鞋带公式）与圆（πr²）。
        // 圆的面积是常见需求，且圆本身没有"顶点"可供鞋带公式使用，
        // 因此这里按类型分派，而不是要求调用方区分两种面积。
        if (reference->type() == ShapeType::Polygon) {
            const PolygonShape *polygon = static_cast<const PolygonShape *>(reference);
            if (polygon->vertices().size() < 3) {
                m_geometryValid = false;
                return;
            }
            m_value = GeoMath::polygonArea(polygon->vertices());
            m_geometryValid = true;
            return;
        }

        if (reference->type() == ShapeType::Circle) {
            const CircleData &circle = static_cast<const CircleShape *>(reference)->circleData();
            if (!circle.isDefined) {
                m_geometryValid = false;
                return;
            }
            m_value = GeoMath::kPi * circle.radius * circle.radius;
            m_geometryValid = true;
            return;
        }

        m_geometryValid = false; // 其他图元没有面积
        return;
    }
    case MeasurementKind::PointCoordinates: {
        if (m_inputPointIds.isEmpty()) {
            m_geometryValid = false;
            return;
        }
        const PointShape *point = document.point(m_inputPointIds.first());
        if (point == nullptr) {
            m_geometryValid = false;
            return;
        }
        m_value = point->position().x;
        m_coordinateY = point->position().y;
        m_geometryValid = true;
        return;
    }
    }

    m_geometryValid = false;
}

QString MeasurementShape::displayText() const
{
    if (!m_geometryValid) {
        return QStringLiteral("—");
    }
    if (m_kind == MeasurementKind::PointCoordinates) {
        return QStringLiteral("(%1, %2)").arg(m_value, 0, 'f', 2).arg(m_coordinateY, 0, 'f', 2);
    }
    return formatMeasurement(m_kind, m_value);
}
