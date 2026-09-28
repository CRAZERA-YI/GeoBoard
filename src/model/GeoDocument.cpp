/**
 * @file    model/GeoDocument.cpp
 * @brief   图元容器与依赖图的实现。
 * @details 重算的调度策略是本文件的核心：
 *          - id 升序即拓扑序（不变式：父 id < 自身 id），因此"收集受影响集合 → 升序重算"
 *            就等价于一次拓扑排序后的增量求值，无需运行时建图排序；
 *          - 拖动等高频率操作必须用 beginUpdate()/endUpdate() 包起来，
 *            否则每个中间状态都会触发一次全画布重绘。
 */

#include "GeoDocument.h"

#include <algorithm>
#include <cmath>

namespace {

/** @brief 判断某图元类型是否为"直的曲线"。 */
bool isStraightCurveType(ShapeType type)
{
    return type == ShapeType::Line || type == ShapeType::PerpendicularLine
        || type == ShapeType::ParallelLine || type == ShapeType::AngleBisector;
}

/**
 * @brief 取"直的曲线"的几何数据。
 * @param shape 待取数据的图元；不是直的曲线时返回未定义的 LineData。
 * @return 直线数据。
 */
LineData straightLineDataOf(const Shape *shape)
{
    if (shape == nullptr || !isStraightCurveType(shape->type())) {
        return LineData();
    }
    return static_cast<const LineShape *>(shape)->lineData();
}

/**
 * @brief 取"圆族曲线"（圆或弧）的几何数据。
 * @param shape 待取数据的图元。
 * @param circle 输出参数：圆数据。
 * @param arc 输出参数：仅当 shape 是弧时被写入，用于按角度区间过滤交点。
 * @return 是圆或弧且几何有效时返回 true。
 */
bool circleDataOf(const Shape *shape, CircleData *circle, ArcData *arc)
{
    if (shape == nullptr) {
        return false;
    }
    if (shape->type() == ShapeType::Circle) {
        const CircleData &data = static_cast<const CircleShape *>(shape)->circleData();
        if (!data.isDefined) {
            return false;
        }
        *circle = data;
        return true;
    }
    if (shape->type() == ShapeType::Arc) {
        const ArcData &data = static_cast<const ArcShape *>(shape)->arcData();
        if (!data.isDefined) {
            return false;
        }
        circle->center = data.center;
        circle->radius = data.radius;
        circle->isDefined = true;
        *arc = data;
        return true;
    }
    return false;
}

/**
 * @brief 判断交点是否落在弧的有效角度区间内。
 * @param point 交点。
 * @param arc 弧数据。
 * @return 落在弧上返回 true。
 */
bool pointOnArcRange(const Vec2 &point, const ArcData &arc)
{
    if (!arc.isDefined) {
        return false;
    }
    const double angle = GeoMath::angleOf(point - arc.center);
    return GeoMath::isAngleBetween(angle, arc.startAngle, arc.endAngle);
}

/**
 * @brief 点到某个图元的距离（用于命中测试）。
 * @param shape 图元；仅对曲线有意义。
 * @param point 场景坐标。
 * @param distance 输出参数：最短距离；图元不可命中时不被写入。
 * @return 该图元可参与命中测试时返回 true。
 */
bool distanceToShape(const Shape *shape, const Vec2 &point, double *distance)
{
    if (shape == nullptr || !shape->isValid()) {
        return false;
    }

    switch (shape->type()) {
    case ShapeType::Line:
    case ShapeType::PerpendicularLine:
    case ShapeType::ParallelLine:
    case ShapeType::AngleBisector: {
        const LineData &line = static_cast<const LineShape *>(shape)->lineData();
        if (!line.isDefined) {
            return false;
        }
        const Vec2 projected = GeoMath::projectPointOnLine(point, line.origin, line.direction);
        const double denominator = line.direction.lengthSquared();
        const double t = (projected - line.origin).dot(line.direction) / denominator;
        // 有界维度上按端点算距离，否则用户会在"看不到的地方"选中线段
        const Vec2 nearest = line.containsParameter(t)
                                 ? projected
                                 : (line.origin
                                    + line.direction * (t < 0.0 ? 0.0 : 1.0));
        *distance = point.distanceTo(nearest);
        return true;
    }
    case ShapeType::Circle: {
        const CircleData &circle = static_cast<const CircleShape *>(shape)->circleData();
        if (!circle.isDefined) {
            return false;
        }
        *distance = std::fabs(point.distanceTo(circle.center) - circle.radius);
        return true;
    }
    case ShapeType::Arc: {
        const ArcData &arc = static_cast<const ArcShape *>(shape)->arcData();
        if (!arc.isDefined) {
            return false;
        }
        const Vec2 projected = GeoMath::projectPointOnCircle(point, arc.center, arc.radius);
        if (pointOnArcRange(projected, arc)) {
            *distance = point.distanceTo(projected);
            return true;
        }
        const Vec2 startPoint = arc.center
            + Vec2(std::cos(arc.startAngle), std::sin(arc.startAngle)) * arc.radius;
        const Vec2 endPoint = arc.center
            + Vec2(std::cos(arc.endAngle), std::sin(arc.endAngle)) * arc.radius;
        *distance = std::min(point.distanceTo(startPoint), point.distanceTo(endPoint));
        return true;
    }
    case ShapeType::Polygon: {
        const QList<Vec2> &vertices = static_cast<const PolygonShape *>(shape)->vertices();
        const int count = vertices.size();
        if (count < 3) {
            return false;
        }
        double best = -1.0;
        for (int i = 0; i < count; ++i) {
            const double candidate = GeoMath::distanceToSegment(
                point, vertices.at(i), vertices.at((i + 1) % count));
            if (best < 0.0 || candidate < best) {
                best = candidate;
            }
        }
        if (best < 0.0) {
            return false;
        }
        *distance = best;
        return true;
    }
    default:
        break;
    }
    return false;
}

/**
 * @brief 求一条边（线段）与任意曲线的交点。
 * @param edge 边的几何数据（必须是两端有界的线段）。
 * @param other 另一条曲线（不是多边形）。
 * @param otherIsStraight other 是否属于"直的曲线"族。
 * @param otherLine other 的直线数据（仅当 otherIsStraight 时有效）。
 * @param otherCircle other 的圆数据（仅当 other 是圆或弧时有效）。
 * @param otherArc other 的弧数据（仅当 other 是弧时有效）。
 * @param results 输出参数：交点会追加到该列表。
 * @return 本次求得的交点个数。
 * @note  多边形的求交统一走这里：把多边形拆成边逐个求解，
 *        这样"多边形 × 直线/圆/弧/多边形"四种组合只需一份实现。
 */
int intersectEdgeWithCurve(const LineData &edge,
                           const Shape *other,
                           bool otherIsStraight,
                           const LineData &otherLine,
                           const CircleData &otherCircle,
                           const ArcData &otherArc,
                           QList<Vec2> *results)
{
    const int before = results->size();
    if (other == nullptr || !edge.isDefined) {
        return 0;
    }

    if (otherIsStraight) {
        if (!otherLine.isDefined) {
            return 0;
        }
        const double denominator = edge.direction.cross(otherLine.direction);
        if (std::fabs(denominator) <= GeoMath::kPositionTolerance) {
            return 0; // 平行或重合
        }
        const double t =
            (otherLine.origin - edge.origin).cross(otherLine.direction) / denominator;
        const double u =
            (otherLine.origin - edge.origin).cross(edge.direction) / denominator;
        if (edge.containsParameter(t) && otherLine.containsParameter(u)) {
            results->append(edge.origin + edge.direction * t);
        }
        return results->size() - before;
    }

    if (!otherCircle.isDefined) {
        return 0;
    }

    QList<Vec2> candidates;
    GeoMath::intersectLineCircle(edge.origin, edge.direction, otherCircle.center,
                                 otherCircle.radius, &candidates);
    for (int i = 0; i < candidates.size(); ++i) {
        // 参数换算统一走 LineData::parameterAt：把"以方向长度为单位的解参数"
        // 与"t=1 落在 direction 终点"的约定分开处理，只保留一个约定
        if (!edge.containsParameter(edge.parameterAt(candidates.at(i)))) {
            continue;
        }
        if (other->type() == ShapeType::Arc && !pointOnArcRange(candidates.at(i), otherArc)) {
            continue;
        }
        results->append(candidates.at(i));
    }
    return results->size() - before;
}

/**
 * @brief 把一个多边形的边提取成线段数据列表。
 * @param polygon 多边形图元。
 * @return 边列表；顶点不足 3 个或含退化边时可能更少。
 */
QList<LineData> edgesOfPolygon(const Shape *polygon)
{
    QList<LineData> edges;
    if (polygon == nullptr || polygon->type() != ShapeType::Polygon) {
        return edges;
    }
    const QList<Vec2> &vertices = static_cast<const PolygonShape *>(polygon)->vertices();
    const int count = vertices.size();
    for (int i = 0; i < count; ++i) {
        LineData edge;
        edge.origin = vertices.at(i);
        edge.direction = vertices.at((i + 1) % count) - vertices.at(i);
        edge.hasStartBound = true;
        edge.hasEndBound = true;
        edge.isDefined = edge.direction.lengthSquared()
            > GeoMath::kPositionTolerance * GeoMath::kPositionTolerance;
        if (edge.isDefined) {
            edges.append(edge);
        }
    }
    return edges;
}

} // namespace

// ---------------------------------------------------------------------------
// 构造与析构
// ---------------------------------------------------------------------------

GeoDocument::GeoDocument(QObject *parent)
    : QObject(parent)
    , m_nextId(1)
    , m_updateDepth(0)
    , m_pendingChange(false)
{
}

GeoDocument::~GeoDocument()
{
    qDeleteAll(m_shapes);
    m_shapes.clear();
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

Shape *GeoDocument::shape(ShapeId id) const
{
    return m_shapes.value(id, nullptr);
}

PointShape *GeoDocument::point(ShapeId id) const
{
    Shape *found = m_shapes.value(id, nullptr);
    if (found == nullptr || found->type() != ShapeType::Point) {
        return nullptr;
    }
    return static_cast<PointShape *>(found);
}

Shape *GeoDocument::curve(ShapeId id) const
{
    Shape *found = m_shapes.value(id, nullptr);
    if (found == nullptr || !found->isCurve()) {
        return nullptr;
    }
    return found;
}

bool GeoDocument::pointsOf(ShapeId firstId, ShapeId secondId, Vec2 *first, Vec2 *second) const
{
    const PointShape *firstPoint = point(firstId);
    const PointShape *secondPoint = point(secondId);
    if (firstPoint == nullptr || secondPoint == nullptr) {
        return false;
    }
    if (first != nullptr) {
        *first = firstPoint->position();
    }
    if (second != nullptr) {
        *second = secondPoint->position();
    }
    return true;
}

QList<Shape *> GeoDocument::shapesInOrder() const
{
    QList<ShapeId> ids = m_shapes.keys();
    std::sort(ids.begin(), ids.end());
    QList<Shape *> result;
    result.reserve(ids.size());
    for (int i = 0; i < ids.size(); ++i) {
        result.append(m_shapes.value(ids.at(i)));
    }
    return result;
}

QList<ShapeId> GeoDocument::dependentsOf(ShapeId id) const
{
    return m_children.value(id);
}

QList<ShapeId> GeoDocument::parentsOf(ShapeId id) const
{
    const Shape *found = shape(id);
    return (found == nullptr) ? QList<ShapeId>() : found->parents();
}

QSet<ShapeId> GeoDocument::descendantIds(const QSet<ShapeId> &ids) const
{
    // 广度优先收集传递依赖者。依赖图是有向无环图，visited 同时起到防重与防环作用。
    QSet<ShapeId> visited;
    QList<ShapeId> pending;
    for (QSet<ShapeId>::const_iterator it = ids.constBegin(); it != ids.constEnd(); ++it) {
        pending.append(*it);
        visited.insert(*it);
    }

    int cursor = 0;
    while (cursor < pending.size()) {
        const ShapeId current = pending.at(cursor);
        ++cursor;
        const QList<ShapeId> children = m_children.value(current);
        for (int i = 0; i < children.size(); ++i) {
            const ShapeId child = children.at(i);
            if (visited.contains(child)) {
                continue;
            }
            visited.insert(child);
            pending.append(child);
        }
    }
    return visited;
}

// ---------------------------------------------------------------------------
// 修改
// ---------------------------------------------------------------------------

ShapeId GeoDocument::allocateShapeId()
{
    // **必须在这里就递增**：本函数用于"先算出各图元的 id、再一起构造"的场景，
    // 例如一次作图会连续取"第二个点"和"要创建的图形"两个 id。
    // 早先只返回 m_nextId 而不递增，同一个动作里两次调用会拿到同一个 id，
    // 后构造的那个图元被 addShape 以"id 已存在"拒绝接管并静默丢弃——
    // 用户看到的就是"点了没反应、图形没画出来"（实测定位到的画圆失灵根因）。
    //
    // 与 addShape 的配合：addShape 只在传入 id >= m_nextId 时才推进 m_nextId，
    // 因此这里先递增不会造成 id 空洞或重复。
    return m_nextId++;
}

ShapeId GeoDocument::addShape(Shape *shape)
{
    if (shape == nullptr) {
        return kInvalidShapeId;
    }
    if (shape->id() == kInvalidShapeId || m_shapes.contains(shape->id())) {
        // 拒绝接管：id 是依赖图的键，重复或非法 id 会让重算与序列化全部错乱
        return kInvalidShapeId;
    }

    const ShapeId id = shape->id();
    m_shapes.insert(id, shape);
    if (id >= m_nextId) {
        m_nextId = id + 1;
    }
    rebuildIndex();

    // 依赖的 id 必须严格小于自身 id（拓扑序不变式），违反说明调用方构造顺序有误
    const QList<ShapeId> parents = shape->parents();
    for (int i = 0; i < parents.size(); ++i) {
        Q_ASSERT(parents.at(i) < id);
    }

    recomputeSubgraph(QSet<ShapeId>() << id);
    notifyChanged();
    return id;
}

QList<ShapeId> GeoDocument::removeShapes(const QSet<ShapeId> &ids)
{
    // 级联：父图元消失后派生图元会永久无效，与其留下悬空引用，不如一并删除
    // （上层会提示被级联删除的数量，且整次删除可撤销）
    const QSet<ShapeId> affected = descendantIds(ids);
    QList<ShapeId> removed;
    for (QSet<ShapeId>::const_iterator it = affected.constBegin(); it != affected.constEnd(); ++it) {
        if (m_shapes.contains(*it)) {
            removed.append(*it);
        }
    }
    if (removed.isEmpty()) {
        return removed;
    }
    std::sort(removed.begin(), removed.end());

    // 逆序删除：父图元可能被重建（撤销）时保证依赖者先消失，避免中间态出现悬空父引用
    for (int i = removed.size() - 1; i >= 0; --i) {
        Shape *doomed = m_shapes.take(removed.at(i));
        delete doomed;
    }

    rebuildIndex();
    pruneSelection();
    notifyChanged();
    return removed;
}

bool GeoDocument::setShapeEnabled(ShapeId id, bool enabled)
{
    Shape *target = shape(id);
    if (target == nullptr || target->isEnabled() == enabled) {
        return target != nullptr;
    }
    target->setEnabled(enabled);
    recomputeSubgraph(QSet<ShapeId>() << id); // 禁用/启用会改变依赖者的有效性
    return true;
}

bool GeoDocument::movePoint(ShapeId id, const Vec2 &position)
{
    PointShape *target = point(id);
    if (target == nullptr || !position.isFinite()) {
        return false;
    }
    target->setPosition(position);
    recomputeSubgraph(QSet<ShapeId>() << id);
    return true;
}

bool GeoDocument::setPointKind(ShapeId id, PointKind kind, ShapeId hostId)
{
    PointShape *target = point(id);
    if (target == nullptr) {
        return false;
    }
    if (kind == PointKind::OnObject && curve(hostId) == nullptr) {
        return false;
    }

    // 两步都改完再重建索引与重算，避免"kind 已变但宿主还是旧的"这种中间态被观察到
    if (kind == PointKind::OnObject) {
        target->setHostId(hostId);
        target->setKind(PointKind::OnObject);
    } else {
        target->setHostId(kInvalidShapeId);
        target->setKind(PointKind::Free);
    }

    rebuildIndex();
    recomputeSubgraph(QSet<ShapeId>() << id);
    return true;
}

bool GeoDocument::setLineBaseCurve(ShapeId id, ShapeId baseCurveId)
{
    Shape *target = shape(id);
    if (target == nullptr || curve(baseCurveId) == nullptr) {
        return false;
    }
    if (target->type() != ShapeType::PerpendicularLine && target->type() != ShapeType::ParallelLine) {
        return false;
    }
    static_cast<LineShape *>(target)->setBaseCurveId(baseCurveId);
    rebuildIndex();
    recomputeSubgraph(QSet<ShapeId>() << id);
    return true;
}

bool GeoDocument::setMeasurementAnchor(ShapeId id, const Vec2 &anchor)
{
    Shape *target = shape(id);
    if (target == nullptr || target->type() != ShapeType::Measurement) {
        return false;
    }
    static_cast<MeasurementShape *>(target)->setAnchor(anchor);
    notifyChanged(); // 锚点变化不影响几何，无需重算
    return true;
}

void GeoDocument::clear()
{
    qDeleteAll(m_shapes);
    m_shapes.clear();
    m_children.clear();
    m_selection.clear();
    m_nextId = 1;
    notifyChanged();
    emit selectionChanged();
}

// ---------------------------------------------------------------------------
// 选中集
// ---------------------------------------------------------------------------

QList<ShapeId> GeoDocument::selection() const
{
    QList<ShapeId> ids = m_selection.values();
    std::sort(ids.begin(), ids.end());
    return ids;
}

void GeoDocument::setSelection(const QSet<ShapeId> &ids)
{
    QSet<ShapeId> filtered;
    for (QSet<ShapeId>::const_iterator it = ids.constBegin(); it != ids.constEnd(); ++it) {
        if (m_shapes.contains(*it)) {
            filtered.insert(*it);
        }
    }
    if (filtered == m_selection) {
        return;
    }
    m_selection = filtered;
    emit selectionChanged();
}

void GeoDocument::clearSelection()
{
    if (m_selection.isEmpty()) {
        return;
    }
    m_selection.clear();
    emit selectionChanged();
}

// ---------------------------------------------------------------------------
// 命中测试
// ---------------------------------------------------------------------------

ShapeId GeoDocument::hitTestPoint(const Vec2 &scenePos, double tolerance) const
{
    ShapeId bestId = kInvalidShapeId;
    double bestDistance = tolerance;

    const QList<Shape *> shapes = shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        Shape *candidate = shapes.at(i);
        if (!candidate->isValid()) {
            continue;
        }

        Vec2 position;
        bool hasPosition = false;
        switch (candidate->type()) {
        case ShapeType::Point:
            position = static_cast<const PointShape *>(candidate)->position();
            hasPosition = true;
            break;
        case ShapeType::MidPoint:
            position = static_cast<const MidPointShape *>(candidate)->position();
            hasPosition = true;
            break;
        case ShapeType::IntersectionPoint:
            position = static_cast<const IntersectionPointShape *>(candidate)->position();
            hasPosition = true;
            break;
        default:
            break;
        }
        if (!hasPosition) {
            continue;
        }

        const double distance = scenePos.distanceTo(position);
        // 用 <= 保证后创建的图元（id 更大）在完全重合时胜出，符合"新画的在上层"的直觉
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestId = candidate->id();
        }
    }
    return bestId;
}

ShapeId GeoDocument::hitTestCurve(const Vec2 &scenePos, double tolerance) const
{
    ShapeId bestId = kInvalidShapeId;
    double bestDistance = tolerance;

    const QList<Shape *> shapes = shapesInOrder();
    for (int i = shapes.size() - 1; i >= 0; --i) { // 后创建的优先
        Shape *candidate = shapes.at(i);
        if (candidate == nullptr || !candidate->isCurve()) {
            continue;
        }
        double distance = 0.0;
        if (!distanceToShape(candidate, scenePos, &distance)) {
            continue;
        }
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestId = candidate->id();
        }
    }
    return bestId;
}

ShapeId GeoDocument::hitTestAny(const Vec2 &scenePos, double tolerance) const
{
    const ShapeId pointHit = hitTestPoint(scenePos, tolerance);
    if (pointHit != kInvalidShapeId) {
        return pointHit;
    }
    const ShapeId curveHit = hitTestCurve(scenePos, tolerance);
    if (curveHit != kInvalidShapeId) {
        return curveHit;
    }

    // 最后才考虑标注：标注的锚点通常压在图形上，优先级最低才不会挡住作图
    ShapeId bestId = kInvalidShapeId;
    double bestDistance = tolerance;
    const QList<Shape *> shapes = shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        Shape *candidate = shapes.at(i);
        if (candidate->type() != ShapeType::Measurement || !candidate->isValid()) {
            continue;
        }
        const double distance =
            scenePos.distanceTo(static_cast<const MeasurementShape *>(candidate)->anchor());
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestId = candidate->id();
        }
    }
    return bestId;
}

int GeoDocument::curveIntersections(ShapeId firstCurveId,
                                    ShapeId secondCurveId,
                                    QList<Vec2> *results) const
{
    if (results == nullptr || firstCurveId == secondCurveId) {
        return 0; // 同一条曲线与自身"相交"没有意义（重合，无穷多交点）
    }

    const Shape *first = curve(firstCurveId);
    const Shape *second = curve(secondCurveId);
    if (first == nullptr || second == nullptr || !first->isValid() || !second->isValid()) {
        return 0;
    }

    QList<Vec2> raw;

    const bool firstIsStraight = isStraightCurveType(first->type());
    const bool secondIsStraight = isStraightCurveType(second->type());
    const bool firstIsCircleLike =
        (first->type() == ShapeType::Circle || first->type() == ShapeType::Arc);
    const bool secondIsCircleLike =
        (second->type() == ShapeType::Circle || second->type() == ShapeType::Arc);

    // 统一在这里抽取几何数据，供后面各组合分支复用（含多边形拆边后的逐段求解）
    const LineData firstLine = straightLineDataOf(first);
    const LineData secondLine = straightLineDataOf(second);
    CircleData firstCircle;
    CircleData secondCircle;
    ArcData firstArc;
    ArcData secondArc;
    if (firstIsCircleLike) {
        circleDataOf(first, &firstCircle, &firstArc);
    }
    if (secondIsCircleLike) {
        circleDataOf(second, &secondCircle, &secondArc);
    }

    if (firstIsStraight && secondIsStraight) {
        const LineData &a = firstLine;
        const LineData &b = secondLine;
        const double denominator = a.direction.cross(b.direction);
        if (std::fabs(denominator) <= GeoMath::kPositionTolerance) {
            return 0; // 平行或重合：无交点或无穷多交点，都不作为约束
        }
        const double t = (b.origin - a.origin).cross(b.direction) / denominator;
        const double u = (b.origin - a.origin).cross(a.direction) / denominator;
        if (a.containsParameter(t) && b.containsParameter(u)) {
            raw.append(a.origin + a.direction * t);
        }
    } else if (firstIsStraight && secondIsCircleLike) {
        if (secondCircle.isDefined) {
            QList<Vec2> candidates;
            GeoMath::intersectLineCircle(firstLine.origin, firstLine.direction, secondCircle.center,
                                         secondCircle.radius, &candidates);
            for (int i = 0; i < candidates.size(); ++i) {
                // 同 intersectEdgeWithCurve：参数换算统一走 LineData::parameterAt
                if (!firstLine.containsParameter(firstLine.parameterAt(candidates.at(i)))) {
                    continue;
                }
                if (second->type() == ShapeType::Arc && !pointOnArcRange(candidates.at(i), secondArc)) {
                    continue;
                }
                raw.append(candidates.at(i));
            }
        }
    } else if (firstIsCircleLike && secondIsStraight) {
        // 交给同一套逻辑处理，交换参数即可（交点与顺序无关）
        return curveIntersections(secondCurveId, firstCurveId, results);
    } else if (firstIsCircleLike && secondIsCircleLike) {
        if (firstCircle.isDefined && secondCircle.isDefined) {
            QList<Vec2> candidates;
            GeoMath::intersectCircleCircle(firstCircle.center, firstCircle.radius, secondCircle.center,
                                           secondCircle.radius, &candidates);
            for (int i = 0; i < candidates.size(); ++i) {
                if (first->type() == ShapeType::Arc && !pointOnArcRange(candidates.at(i), firstArc)) {
                    continue;
                }
                if (second->type() == ShapeType::Arc && !pointOnArcRange(candidates.at(i), secondArc)) {
                    continue;
                }
                raw.append(candidates.at(i));
            }
        }
    } else {
        // 至少有一边是多边形：把多边形的边拆出来逐段求交，复用上面同一套判定
        if (first->type() == ShapeType::Polygon) {
            const QList<LineData> edges = edgesOfPolygon(first);
            for (int i = 0; i < edges.size(); ++i) {
                intersectEdgeWithCurve(edges.at(i), second, secondIsStraight, secondLine, secondCircle,
                                       secondArc, &raw);
            }
        } else { // 此时 second 必为多边形
            const QList<LineData> edges = edgesOfPolygon(second);
            for (int i = 0; i < edges.size(); ++i) {
                intersectEdgeWithCurve(edges.at(i), first, firstIsStraight, firstLine, firstCircle,
                                       firstArc, &raw);
            }
        }
    }

    for (int i = 0; i < raw.size(); ++i) {
        if (raw.at(i).isFinite()) {
            results->append(raw.at(i));
        }
    }
    return results->size();
}

bool GeoDocument::contentBounds(Vec2 *minPoint, Vec2 *maxPoint) const
{
    if (minPoint == nullptr || maxPoint == nullptr) {
        return false;
    }

    bool hasAny = false;
    Vec2 minCorner;
    Vec2 maxCorner;

    const QList<Shape *> shapes = shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        Shape *candidate = shapes.at(i);
        if (!candidate->isValid()) {
            continue;
        }

        QList<Vec2> samples;
        switch (candidate->type()) {
        case ShapeType::Point:
            samples.append(static_cast<const PointShape *>(candidate)->position());
            break;
        case ShapeType::MidPoint:
            samples.append(static_cast<const MidPointShape *>(candidate)->position());
            break;
        case ShapeType::IntersectionPoint:
            samples.append(static_cast<const IntersectionPointShape *>(candidate)->position());
            break;
        case ShapeType::Polygon:
            samples = static_cast<const PolygonShape *>(candidate)->vertices();
            break;
        case ShapeType::Circle: {
            const CircleData &circle = static_cast<const CircleShape *>(candidate)->circleData();
            samples.append(Vec2(circle.center.x - circle.radius, circle.center.y - circle.radius));
            samples.append(Vec2(circle.center.x + circle.radius, circle.center.y + circle.radius));
            break;
        }
        case ShapeType::Arc: {
            const ArcData &arc = static_cast<const ArcShape *>(candidate)->arcData();
            // 弧的两端一定在弧上；四个轴向极值点只有落在角度区间内才算
            samples.append(arc.center
                           + Vec2(std::cos(arc.startAngle), std::sin(arc.startAngle)) * arc.radius);
            samples.append(arc.center
                           + Vec2(std::cos(arc.endAngle), std::sin(arc.endAngle)) * arc.radius);
            for (int k = 0; k < 4; ++k) {
                const double angle = k * GeoMath::kPi / 2.0;
                if (GeoMath::isAngleBetween(angle, arc.startAngle, arc.endAngle)) {
                    samples.append(arc.center + Vec2(std::cos(angle), std::sin(angle)) * arc.radius);
                }
            }
            break;
        }
        case ShapeType::Line:
        case ShapeType::PerpendicularLine:
        case ShapeType::ParallelLine:
        case ShapeType::AngleBisector: {
            const LineData &line = static_cast<const LineShape *>(candidate)->lineData();
            if (!line.isDefined) {
                break;
            }
            // 只用两个定义点圈定范围；无限直线本身就是"没有界"的，
            // 其范围由视图的可见区域决定，不该参与"缩放到全部内容"的计算
            samples.append(line.origin);
            if (line.hasEndBound) {
                samples.append(line.origin + line.direction);
            }
            break;
        }
        case ShapeType::Measurement:
            samples.append(static_cast<const MeasurementShape *>(candidate)->anchor());
            break;
        default:
            break;
        }

        for (int k = 0; k < samples.size(); ++k) {
            const Vec2 &sample = samples.at(k);
            if (!sample.isFinite()) {
                continue;
            }
            if (!hasAny) {
                minCorner = sample;
                maxCorner = sample;
                hasAny = true;
                continue;
            }
            minCorner.x = std::min(minCorner.x, sample.x);
            minCorner.y = std::min(minCorner.y, sample.y);
            maxCorner.x = std::max(maxCorner.x, sample.x);
            maxCorner.y = std::max(maxCorner.y, sample.y);
        }
    }

    if (!hasAny) {
        return false;
    }
    *minPoint = minCorner;
    *maxPoint = maxCorner;
    return true;
}

// ---------------------------------------------------------------------------
// 批量更新
// ---------------------------------------------------------------------------

void GeoDocument::beginUpdate()
{
    ++m_updateDepth;
}

void GeoDocument::endUpdate()
{
    if (m_updateDepth > 0) {
        --m_updateDepth;
    }
    if (m_updateDepth == 0 && m_pendingChange) {
        m_pendingChange = false;
        emit documentChanged();
    }
}

// ---------------------------------------------------------------------------
// 私有实现
// ---------------------------------------------------------------------------

void GeoDocument::rebuildIndex()
{
    m_children.clear();
    for (QHash<ShapeId, Shape *>::const_iterator it = m_shapes.constBegin();
         it != m_shapes.constEnd(); ++it) {
        const QList<ShapeId> parents = it.value()->parents();
        for (int i = 0; i < parents.size(); ++i) {
            m_children[parents.at(i)].append(it.key());
        }
    }
}

void GeoDocument::recomputeSubgraph(const QSet<ShapeId> &changedIds)
{
    QSet<ShapeId> affected = descendantIds(changedIds);
    QList<ShapeId> ordered;
    for (QSet<ShapeId>::const_iterator it = affected.constBegin(); it != affected.constEnd(); ++it) {
        if (m_shapes.contains(*it)) {
            ordered.append(*it);
        }
    }
    std::sort(ordered.begin(), ordered.end()); // id 升序 == 拓扑序

    for (int i = 0; i < ordered.size(); ++i) {
        recomputeShape(ordered.at(i));
    }
    notifyChanged();
}

void GeoDocument::recomputeShape(ShapeId id)
{
    Shape *target = shape(id);
    if (target == nullptr) {
        return;
    }
    target->recompute(*this);
}

void GeoDocument::notifyChanged()
{
    if (m_updateDepth > 0) {
        m_pendingChange = true;
        return;
    }
    emit documentChanged();
}

void GeoDocument::pruneSelection()
{
    QSet<ShapeId> surviving;
    for (QSet<ShapeId>::const_iterator it = m_selection.constBegin();
         it != m_selection.constEnd(); ++it) {
        if (m_shapes.contains(*it)) {
            surviving.insert(*it);
        }
    }
    if (surviving == m_selection) {
        return;
    }
    m_selection = surviving;
    emit selectionChanged();
}
