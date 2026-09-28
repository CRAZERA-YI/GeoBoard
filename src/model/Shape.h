/**
 * @file    model/Shape.h
 * @brief   图元的数据结构与自身几何推导。
 * @details 本文件定义几何画板的全部图元类型。核心约定只有一条：
 *
 *          **几何由点位 id 导出，不缓存坐标。**
 *
 *          线段只记两个点的 id，圆只记圆心点与圆上点的 id。于是"拖动点 →
 *          半径变化 → 依赖图形跟随"是数据结构的自然结果，不需要任何同步代码，
 *          也就不存在"忘了同步"这一类 bug。
 *
 *          唯一的例外是测量值：它是**派生量**，每次重算都重新读取输入点位的当前坐标。
 *
 *          不负责：父子索引与重算调度（见 GeoDocument）、坐标变换（见 Camera）、
 *          绘制（见 view/CanvasWidget）、文件读写（见 service/JsonStore）。
 */

#ifndef GEOBOARDSHAPE_H
#define GEOBOARDSHAPE_H

#include "GeometryMath.h"

#include <QList>
#include <QString>

/** @brief 图元标识；0 保留为"无效 id"。 */
using ShapeId = int;

/** @brief 判空用的无效图元 id。 */
const ShapeId kInvalidShapeId = 0;

/** @brief 图元种类。 */
enum class ShapeType
{
    Point,              ///< 点。
    Line,               ///< 线段 / 射线 / 直线（由 LineKind 细分）。
    Circle,             ///< 圆（圆心 + 圆上一点）。
    Arc,                ///< 三点圆弧。
    Polygon,            ///< 多边形。
    PerpendicularLine,  ///< 垂线（过某点且垂直于基准曲线）。
    ParallelLine,       ///< 平行线（过某点且平行于基准曲线）。
    AngleBisector,      ///< 角平分线（顶点 + 两边上的点）。
    MidPoint,           ///< 两点中点。
    IntersectionPoint,  ///< 两曲线交点。
    Measurement         ///< 只读标注（距离、角度、面积等）。
};

/** @brief 点的构造方式。 */
enum class PointKind
{
    Free,     ///< 自由点：可任意拖动。
    OnObject  ///< 附着点：被约束在父曲线上，拖动时投影到该曲线。
};

/** @brief 线的有界方式。 */
enum class LineKind
{
    Segment,       ///< 线段：两端都有界。
    Ray,           ///< 射线：从起点出发半无界。
    InfiniteLine   ///< 直线：两端都无界。
};

/** @brief 圆弧的构造方式。 */
enum class ArcKind
{
    ThreePoint,     ///< 三点弧：起点、弧上一点、终点（外接圆决定圆心与半径）。
    CenterTwoPoint  ///< 圆心 + 两点：圆心、半径点（决定半径）、弧上一点（决定扫过哪一段）。
};

/** @brief 测量类型。 */
enum class MeasurementKind
{
    Distance,          ///< 两点距离。
    SegmentLength,     ///< 线段长度。
    Angle,             ///< 角度（三点，顶点居中）。
    Radius,            ///< 圆半径。
    Circumference,     ///< 圆周长。
    Area,              ///< 面积（多边形按鞋带公式，圆按 πr²）。
    PointCoordinates   ///< 点的坐标。
};

/**
 * @brief 直的曲线（线段/射线/直线及其派生线）的几何数据。
 * @details 用"起点 + 方向 + 两端是否有界"统一表达三种线与三种派生线，
 *          这样绘制、交点求解、附着点投影都只需实现一份。
 */
struct LineData
{
    Vec2 origin;               ///< 起点（射线的端点、线段的起点）。
    Vec2 direction;            ///< 方向向量，非单位长度，指向"另一端"。
    bool hasStartBound;        ///< true 表示 origin 之前的部分不存在（线段/射线）。
    bool hasEndBound;          ///< true 表示 origin + direction 之后的部分不存在（仅线段）。
    bool isDefined;            ///< 本次重算是否得到了有效几何。

    /** @brief 构造一个未定义的直线数据。 */
    LineData()
        : origin()
        , direction()
        , hasStartBound(false)
        , hasEndBound(false)
        , isDefined(false)
    {
    }

    /**
     * @brief 把参数 t 裁剪到该曲线的有效区间。
     * @param t 输入参数（点 = origin + direction * t）。
     * @return 裁剪后的参数。
     */
    double clampParameter(double t) const
    {
        if (hasStartBound && t < 0.0) {
            return 0.0;
        }
        if (hasEndBound && t > 1.0) {
            return 1.0;
        }
        return t;
    }

    /**
     * @brief 求某个点在本曲线参数化下的参数。
     * @param point 曲线上的点（由交点求解或投影得到）。
     * @return 参数 t，满足 点 ≈ origin + direction * t。
     * @note  这是**唯一**允许用来把点换算成参数的入口。曾经的缺陷是把
     *        "以方向向量长度为单位"的参数与"t=1 落在 direction 终点"的参数混用，
     *        导致圆与线段的交点被误判在线段之外（少算一个交点）。
     */
    double parameterAt(const Vec2 &point) const;

    /**
     * @brief 判断参数是否落在有效区间内。
     * @param t 输入参数。
     * @param tolerance 参数容差。
     * @return 落在区间内返回 true。
     */
    bool containsParameter(double t, double tolerance = GeoMath::kParameterTolerance) const
    {
        if (hasStartBound && t < -tolerance) {
            return false;
        }
        if (hasEndBound && t > 1.0 + tolerance) {
            return false;
        }
        return true;
    }
};

/** @brief 圆的几何数据。 */
struct CircleData
{
    Vec2 center;        ///< 圆心。
    double radius;      ///< 半径，恒为非负。
    bool isDefined;     ///< 本次重算是否得到了有效几何。

    /** @brief 构造一个未定义的圆数据。 */
    CircleData() : center(), radius(0.0), isDefined(false) {}
};

/** @brief 圆（弧）的几何数据：外接圆 + 角度区间。 */
struct ArcData
{
    Vec2 center;        ///< 外接圆圆心。
    double radius;      ///< 外接圆半径。
    double startAngle;  ///< 起始角（弧度），来自起点。
    double endAngle;    ///< 终止角（弧度），来自终点。
    bool isDefined;     ///< 三点共线等退化情形下为 false。

    /** @brief 构造一个未定义的弧数据。 */
    ArcData() : center(), radius(0.0), startAngle(0.0), endAngle(0.0), isDefined(false) {}

    /**
     * @brief 返回从起始角逆时针扫到终止角的角度（弧度）。
     * @return 扫过的角度，范围 (0, 2π]；起止角重合时按整圆 2π 处理。
     * @note  弧的"起点与终点重合"是合法输入（用户还没定范围），此时按整圆显示，
     *        与早期版本"sweep 为 0 就当整圆"的行为一致，避免出现零长度弧。
     */
    double sweep() const
    {
        const double raw = GeoMath::normalizedAngle(endAngle - startAngle);
        return (raw <= GeoMath::kParameterTolerance) ? GeoMath::kTwoPi : raw;
    }
};

/**
 * @brief 图元的抽象基类。
 * @details 职责：持有 id、类型、父图元 id 列表与有效性；提供自身几何推导入口。
 *          所有子类都必须能回答 parents()，GeoDocument 依赖它建立依赖图。
 */
class Shape
{
public:
    /**
     * @brief 构造图元。
     * @param id 图元标识（由 GeoDocument 分配，全局唯一且不为 0）。
     * @param type 图元种类。
     */
    Shape(ShapeId id, ShapeType type);

    /** @brief 虚析构。 */
    virtual ~Shape();

    /**
     * @brief 复制本图元（保留 id 与父引用）。
     * @return 新分配的副本，调用方负责释放。
     * @note  删除/撤销需要保存图元快照，因此必须有克隆能力。
     */
    virtual Shape *clone() const = 0;

    /**
     * @brief 依据父图元的当前几何重新推导自身几何。
     * @param document 提供按 id 查询图元的只读访问。
     * @note  实现方必须自行处理退化（父不存在、共线、无交点等），
     *        把失败记入几何有效性，**不得**抛出异常或断言失败。
     */
    virtual void recompute(const class GeoDocument &document) = 0;

    /**
     * @brief 判断该图元是否参与绘制与交点求解。
     * @return 用户未禁用且几何有效时返回 true。
     */
    bool isValid() const;

    /** @brief 返回图元标识。 */
    ShapeId id() const { return m_id; }

    /** @brief 返回图元种类。 */
    ShapeType type() const { return m_type; }

    /** @brief 返回父图元 id 列表（无依赖时为空）。 */
    const QList<ShapeId> &parents() const { return m_parents; }

    /** @brief 查询几何有效性（上一次 recompute 的结果）。 */
    bool isGeometryValid() const { return m_geometryValid; }

    /**
     * @brief 启用或禁用该图元。
     * @param enabled false 表示用户主动隐藏/停用，图元保留但不参与计算与绘制。
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }

    /** @brief 查询是否被用户启用。 */
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief 该图元是否为曲线（可参与求交与提供附着点）。
     * @return 是曲线返回 true。
     */
    virtual bool isCurve() const { return false; }

protected:
    /**
     * @brief 判断全部父图元是否存在且有效。
     * @param document 图元查询入口。
     * @return 全部有效返回 true；任一缺失或无效返回 false。
     * @note  派生图元的 recompute 应当先调用它：父无效时自己必然无效。
     */
    bool parentsValid(const GeoDocument &document) const;

    /**
     * @brief 用另一个图元的身份与状态初始化本图元。
     * @param source 源图元。
     * @note  仅供各子类的 clone() 使用。id、类型、启用状态是私有的，
     *        子类无法直接赋值，因此统一经由此函数复制，避免每个 clone 各写一遍。
     */
    void copyIdentityFrom(const Shape &source);

    /**
     * @brief 返回自身 id 的受保护访问入口。
     * @return 图元标识。
     * @note  子类（例如 LineShape::recompute）需要按自身类型分支，
     *        而 id/类型是基类私有成员，故在此提供只读入口。
     */
    ShapeId ownId() const { return m_id; }

    /** @brief 返回自身类型的受保护访问入口。 @return 图元种类。 */
    ShapeType ownType() const { return m_type; }

    QList<ShapeId> m_parents;      ///< 父图元 id 列表；子类在构造时填写。
    bool m_geometryValid;          ///< 上一次 recompute 是否成功。

private:
    ShapeId m_id;                  ///< 图元标识。
    ShapeType m_type;              ///< 图元种类。
    bool m_enabled;                ///< 用户是否启用。
};

/**
 * @brief 点。
 * @details 自由点由用户拖动；附着点被约束在父曲线上，拖动时投影到父曲线。
 */
class PointShape : public Shape
{
public:
    /**
     * @brief 构造点。
     * @param id 图元标识。
     * @param kind 点的构造方式。
     * @param position 初始位置（附着点在首次重算时会被投影到父曲线）。
     */
    PointShape(ShapeId id, PointKind kind, const Vec2 &position);

    Shape *clone() const override;
    void recompute(const GeoDocument &document) override;

    /** @brief 返回点的构造方式。 */
    PointKind kind() const { return m_kind; }

    /** @brief 返回当前坐标（几何无效时为最后一次成功的坐标）。 */
    const Vec2 &position() const { return m_position; }

    /** @brief 设置坐标（仅自由点有意义；附着点的坐标由重算决定）。 */
    void setPosition(const Vec2 &position) { m_position = position; }

    /** @brief 返回附着点的父曲线 id；自由点返回 kInvalidShapeId。 */
    ShapeId hostId() const;

    /**
     * @brief 设置附着点的宿主曲线。
     * @param hostId 宿主曲线 id。
     * @note  构造时无法把 id 写进 m_parents（构造参数里没有它），
     *        因此由 GeoDocument 在接管图元时立即调用本函数登记依赖关系。
     */
    void setHostId(ShapeId hostId);

    /**
     * @brief 修改点的构造方式。
     * @param kind 新的构造方式。
     * @note  与 setHostId 配合使用：先设宿主，再设 kind，两步都完成后
     *        GeoDocument 会重建索引并重算，中间态不会被对外观察到。
     */
    void setKind(PointKind kind) { m_kind = kind; }

private:
    PointKind m_kind;  ///< 构造方式。
    Vec2 m_position;   ///< 当前坐标。
};

/**
 * @brief 直线族曲线：线段、射线、直线以及垂线、平行线、角平分线。
 * @details 派生线（垂线/平行线/角平分线）额外持有一个"基准曲线 id"，
 *          在重算时读取基准曲线的当前几何——这样基准一动，派生线自动跟随。
 */
class LineShape : public Shape
{
public:
    /**
     * @brief 构造由两点确定的线。
     * @param id 图元标识。
     * @param kind 线段 / 射线 / 直线。
     * @param firstPointId 起点（射线的端点、线段的起点）。
     * @param secondPointId 第二点（决定方向；线段为终点）。
     */
    LineShape(ShapeId id, LineKind kind, ShapeId firstPointId, ShapeId secondPointId);

    /**
     * @brief 构造派生线（垂线或平行线）。
     * @param id 图元标识。
     * @param type 必须是 ShapeType::PerpendicularLine 或 ShapeType::ParallelLine。
     * @param baseCurveId 基准曲线 id。
     * @param throughPointId 通过点 id。
     */
    LineShape(ShapeId id, ShapeType type, ShapeId baseCurveId, ShapeId throughPointId);

    /**
     * @brief 构造角平分线。
     * @param id 图元标识。
     * @param vertexPointId 角的顶点。
     * @param firstRayPointId 第一条边上的点（非顶点）。
     * @param secondRayPointId 第二条边上的点（非顶点）。
     */
    LineShape(ShapeId id, ShapeId vertexPointId, ShapeId firstRayPointId, ShapeId secondRayPointId);

    Shape *clone() const override;
    void recompute(const GeoDocument &document) override;
    bool isCurve() const override { return true; }

    /** @brief 返回线的有界方式；派生线一律按无限直线处理。 */
    LineKind lineKind() const { return m_lineKind; }

    /** @brief 返回两条定义点的位置（线段的起点与终点在此意义上与 kind 无关）。 */
    ShapeId firstPointId() const { return m_firstPointId; }

    /** @brief 返回第二定义点；派生线返回通过点。 */
    ShapeId secondPointId() const { return m_secondPointId; }

    /** @brief 返回基准曲线 id（仅垂线/平行线有效）。 */
    ShapeId baseCurveId() const { return m_baseCurveId; }

    /**
     * @brief 修改基准曲线。
     * @param baseCurveId 新的基准曲线 id。
     * @note  只改数据不重算：重算与索引重建由 GeoDocument 统一调度，
     *        避免"改一半就对外可见"的中间态。
     */
    void setBaseCurveId(ShapeId baseCurveId) { m_baseCurveId = baseCurveId; }

    /** @brief 返回当前几何。 */
    const LineData &lineData() const { return m_lineData; }

private:
    LineKind m_lineKind;       ///< 有界方式。
    ShapeId m_firstPointId;    ///< 起点 / 顶点 / 基准曲线（按类型解释）。
    ShapeId m_secondPointId;   ///< 第二点 / 通过点 / 第一边上点。
    ShapeId m_thirdPointId;    ///< 仅角平分线使用：第二边上的点。
    ShapeId m_baseCurveId;     ///< 基准曲线（垂线/平行线）。
    LineData m_lineData;       ///< 重算产物。
};

/**
 * @brief 圆：由圆心点与圆上一点确定。
 * @details 半径由两点距离导出，因此拖动任何一个点，半径都会自动跟随。
 */
class CircleShape : public Shape
{
public:
    /**
     * @brief 构造圆。
     * @param id 图元标识。
     * @param centerPointId 圆心点 id。
     * @param rimPointId 圆上一点 id（决定半径）。
     */
    CircleShape(ShapeId id, ShapeId centerPointId, ShapeId rimPointId);

    Shape *clone() const override;
    void recompute(const GeoDocument &document) override;
    bool isCurve() const override { return true; }

    /** @brief 返回圆心点 id。 */
    ShapeId centerPointId() const { return m_centerPointId; }

    /** @brief 返回圆上点 id。 */
    ShapeId rimPointId() const { return m_rimPointId; }

    /** @brief 返回当前几何。 */
    const CircleData &circleData() const { return m_circleData; }

private:
    ShapeId m_centerPointId;    ///< 圆心点。
    ShapeId m_rimPointId;       ///< 圆上点。
    CircleData m_circleData;    ///< 重算产物。
};

/**
 * @brief 圆弧：支持两种构造方式。
 * @details
 *          - **ThreePoint（三点弧）**：由起点、弧上一点、终点确定外接圆，
 *            弧的范围取"从起点到终点且经过弧上点"的那一段；
 *          - **CenterTwoPoint（圆心 + 两点）**：圆心与半径点直接给出圆，弧从半径点出发、
 *            逆时针扫到弧上点。这种方式在教学中更直观（先定圆心，再定半径，最后划范围），
 *            而且圆弧的半径不再依赖三点共线的稳定性。
 *
 *          两种方式共用同一个 ArcData，因此绘制、求交、附着点投影都只有一份实现。
 */
class ArcShape : public Shape
{
public:
    /**
     * @brief 构造三点弧。
     * @param id 图元标识。
     * @param startPointId 起点。
     * @param middlePointId 弧上一点（决定取哪一段弧）。
     * @param endPointId 终点。
     */
    ArcShape(ShapeId id, ShapeId startPointId, ShapeId middlePointId, ShapeId endPointId);

    /**
     * @brief 构造"圆心 + 两点"弧。
     * @param id 图元标识。
     * @param centerPointId 圆心。
     * @param radiusPointId 半径点（与圆心的距离即半径）。
     * @param arcPointId 弧上一点（决定从半径点逆时针扫到哪里）。
     * @param kind 必须为 ArcKind::CenterTwoPoint；保留该参数是为了让两个构造函数
     *             的调用点一眼能看出用的是哪种构造方式。
     */
    ArcShape(ShapeId id,
             ShapeId centerPointId,
             ShapeId radiusPointId,
             ShapeId arcPointId,
             ArcKind kind);

    Shape *clone() const override;
    void recompute(const GeoDocument &document) override;
    bool isCurve() const override { return true; }

    /** @brief 返回构造方式。 */
    ArcKind kind() const { return m_kind; }

    /** @brief 返回起点 id。 */
    ShapeId startPointId() const { return m_startPointId; }

    /** @brief 返回弧上点 id。 */
    ShapeId middlePointId() const { return m_middlePointId; }

    /** @brief 返回终点 id。 */
    ShapeId endPointId() const { return m_endPointId; }

    /** @brief 返回当前几何。 */
    const ArcData &arcData() const { return m_arcData; }

private:
    ArcKind m_kind;             ///< 构造方式。
    ShapeId m_startPointId;     ///< 起点 / 圆心。
    ShapeId m_middlePointId;    ///< 弧上点。
    ShapeId m_endPointId;       ///< 终点 / 半径点。
    ArcData m_arcData;          ///< 重算产物。
};

/**
 * @brief 多边形（顶点不少于 3 个）。
 */
class PolygonShape : public Shape
{
public:
    /**
     * @brief 构造多边形。
     * @param id 图元标识。
     * @param vertexPointIds 顶点 id 列表，按顺序；少于 3 个视为无效几何。
     */
    PolygonShape(ShapeId id, const QList<ShapeId> &vertexPointIds);

    Shape *clone() const override;
    void recompute(const GeoDocument &document) override;
    bool isCurve() const override { return true; }

    /** @brief 返回顶点 id 列表。 */
    const QList<ShapeId> &vertexPointIds() const { return m_vertexPointIds; }

    /** @brief 返回当前顶点坐标（几何无效时可能不足 3 个）。 */
    const QList<Vec2> &vertices() const { return m_vertices; }

private:
    QList<ShapeId> m_vertexPointIds;  ///< 顶点 id。
    QList<Vec2> m_vertices;           ///< 重算产物。
};

/**
 * @brief 派生点：两点中点。
 */
class MidPointShape : public Shape
{
public:
    /**
     * @brief 构造中点。
     * @param id 图元标识。
     * @param firstPointId 第一个点。
     * @param secondPointId 第二个点。
     */
    MidPointShape(ShapeId id, ShapeId firstPointId, ShapeId secondPointId);

    Shape *clone() const override;
    void recompute(const GeoDocument &document) override;

    /** @brief 返回第一个点 id。 */
    ShapeId firstPointId() const { return m_firstPointId; }

    /** @brief 返回第二个点 id。 */
    ShapeId secondPointId() const { return m_secondPointId; }

    /** @brief 返回当前坐标。 */
    const Vec2 &position() const { return m_position; }

private:
    ShapeId m_firstPointId;  ///< 第一个点。
    ShapeId m_secondPointId; ///< 第二个点。
    Vec2 m_position;         ///< 重算产物。
};

/**
 * @brief 派生点：两曲线交点。
 * @details 交点数量会随位形变化（0、1 或 2 个），本图元固定表示其中**第 index 个**。
 *          当该序号的交点不存在时，本图元geometric 无效而不被绘制。
 */
class IntersectionPointShape : public Shape
{
public:
    /**
     * @brief 构造交点。
     * @param id 图元标识。
     * @param firstCurveId 第一条曲线。
     * @param secondCurveId 第二条曲线。
     * @param index 序号（0 或 1），表示取第几个交点。
     */
    IntersectionPointShape(ShapeId id, ShapeId firstCurveId, ShapeId secondCurveId, int index);

    Shape *clone() const override;
    void recompute(const GeoDocument &document) override;

    /** @brief 返回第一条曲线 id。 */
    ShapeId firstCurveId() const { return m_firstCurveId; }

    /** @brief 返回第二条曲线 id。 */
    ShapeId secondCurveId() const { return m_secondCurveId; }

    /** @brief 返回交点序号（0 或 1）。 */
    int index() const { return m_index; }

    /** @brief 返回当前坐标。 */
    const Vec2 &position() const { return m_position; }

private:
    ShapeId m_firstCurveId;   ///< 第一条曲线。
    ShapeId m_secondCurveId;  ///< 第二条曲线。
    int m_index;              ///< 交点序号。
    Vec2 m_position;          ///< 重算产物。
};

/**
 * @brief 只读标注。
 * @details 测量值是派生量，重算时读取输入点位的当前坐标。它**不参与后续作图**
 *          （见 docs/requirements.md 的「明确不做」）。
 */
class MeasurementShape : public Shape
{
public:
    /**
     * @brief 构造测量标注。
     * @param id 图元标识。
     * @param kind 测量类型。
     * @param inputPointIds 输入点 id 列表（1~3 个，含义随类型而定）。
     * @param referenceShapeId 关联图元 id（如线段长度关联线段、圆相关测量关联圆）；无则 kInvalidShapeId。
     * @param anchor 标注文字的显示锚点（场景坐标，由创建时的点击位置决定）。
     */
    MeasurementShape(ShapeId id,
                     MeasurementKind kind,
                     const QList<ShapeId> &inputPointIds,
                     ShapeId referenceShapeId,
                     const Vec2 &anchor);

    Shape *clone() const override;
    void recompute(const GeoDocument &document) override;

    /** @brief 返回测量类型。 */
    MeasurementKind kind() const { return m_kind; }

    /** @brief 返回输入点 id 列表。 */
    const QList<ShapeId> &inputPointIds() const { return m_inputPointIds; }

    /** @brief 返回关联图元 id。 */
    ShapeId referenceShapeId() const { return m_referenceShapeId; }

    /** @brief 返回标注文字的锚点。 */
    const Vec2 &anchor() const { return m_anchor; }

    /** @brief 设置标注锚点（用户可拖动标注位置）。 */
    void setAnchor(const Vec2 &anchor) { m_anchor = anchor; }

    /** @brief 返回计算出的数值（角度为弧度、其余为场景单位）。 */
    double value() const { return m_value; }

    /**
     * @brief 返回供界面显示的文本（已按类型格式化并带单位）。
     * @return 例如 "5.00"、"90.0°"、"28.27"。
     */
    QString displayText() const;

private:
    MeasurementKind m_kind;        ///< 测量类型。
    QList<ShapeId> m_inputPointIds;///< 输入点。
    ShapeId m_referenceShapeId;    ///< 关联图元。
    Vec2 m_anchor;                 ///< 显示锚点。
    double m_value;                ///< 重算产物：数值（角度为弧度）。
    double m_coordinateY;          ///< 重算产物：坐标标注的纵坐标（其他类型不使用）。
};

#endif // GEOBOARDSHAPE_H
