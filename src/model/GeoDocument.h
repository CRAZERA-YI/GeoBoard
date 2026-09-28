/**
 * @file    model/GeoDocument.h
 * @brief   图元容器与依赖图：索引、重算调度、增删与查询。
 * @details 本类是 model 层的核心。它只做四件事：
 *          1. 持有图元集合，维护"父 → 子"的索引；
 *          2. 在几何变动后，**只重算受影响的子图**；
 *          3. 提供增删（删除时级联依赖者）、选中集、命中测试等文档级操作；
 *          4. 发出 documentChanged / selectionChanged，供视图重绘。
 *
 *          拓扑序约定：**父图元 id 必然小于自身 id**。正常构建路径天然满足
 *          （先有点才能以它为父），因此 id 升序即拓扑序，重算无需运行时排序；
 *          反序列化路径由 JsonStore 校验该不变式，违反即判定文件损坏或存在循环依赖。
 *
 *          不负责：文件读写（service/JsonStore）、坐标变换（Camera）、绘制（view）。
 *          本类不包含任何界面头文件，可在无 GUI 的测试程序里直接构造。
 */

#ifndef GEOBOARDGEODOCUMENT_H
#define GEOBOARDGEODOCUMENT_H

#include "Shape.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QString>

/**
 * @brief 几何文档：图元集合与依赖图。
 */
class GeoDocument : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造空文档。
     * @param parent 父对象，Qt 对象树负责其生命周期。
     */
    explicit GeoDocument(QObject *parent = nullptr);

    /** @brief 析构并释放全部图元。 */
    ~GeoDocument() override;

    // ----- 查询 -----

    /**
     * @brief 按 id 查询图元。
     * @param id 图元标识。
     * @return 图元指针；不存在时返回 nullptr。
     * @note  返回的指针归本文档所有，调用方不得 delete。
     */
    Shape *shape(ShapeId id) const;

    /**
     * @brief 按 id 查询点图元。
     * @param id 图元标识。
     * @return 点图元指针；id 不存在或不是点时返回 nullptr。
     */
    PointShape *point(ShapeId id) const;

    /**
     * @brief 按 id 查询曲线图元。
     * @param id 图元标识。
     * @return 曲线图元指针；id 不存在或不是曲线时返回 nullptr。
     */
    Shape *curve(ShapeId id) const;

    /**
     * @brief 直接查询两个父图元的点位。
     * @param firstId 第一个点 id。
     * @param secondId 第二个点 id。
     * @param first 输出参数：第一个点坐标。
     * @param second 输出参数：第二个点坐标。
     * @return 两个点都存在时返回 true。
     */
    bool pointsOf(ShapeId firstId, ShapeId secondId, Vec2 *first, Vec2 *second) const;

    /**
     * @brief 按 id 升序返回全部图元。
     * @return 图元列表；顺序即拓扑序。
     */
    QList<Shape *> shapesInOrder() const;

    /** @brief 返回图元总数。 */
    int shapeCount() const { return m_shapes.size(); }

    /** @brief 返回下次分配的图元 id（用于判断 id 是否已存在）。 */
    ShapeId nextShapeId() const { return m_nextId; }

    /** @brief 查询某图元的直接依赖者。 */
    QList<ShapeId> dependentsOf(ShapeId id) const;

    /** @brief 查询某图元的直接父图元。 */
    QList<ShapeId> parentsOf(ShapeId id) const;

    /**
     * @brief 收集某图元的**全部**传递依赖者（不含自身）。
     * @param ids 起点图元集合。
     * @return 受影响的图元 id 集合。
     */
    QSet<ShapeId> descendantIds(const QSet<ShapeId> &ids) const;

    // ----- 修改 -----

    /**
     * @brief 取出下一个可用 id（**并占用它**）。
     * @return 可用的新 id；每次调用返回不同值。
     * @note  用于"先算出各图元的 id 再一起构造"的场景（一次作图往往要同时给出
     *        "第二个点"与"要创建的图形"两个 id）。**每次调用都占用一个新 id**，
     *        调用方若最终没有构造出该图元，这个 id 会被跳过，不会造成重复或冲突。
     * @warning 早先的实现只返回 m_nextId 而不递增，于是同一个动作里两次分配会拿到
     *          同一个 id，后构造的图元被 addShape 以"id 已存在"拒绝并静默丢弃——
     *          用户看到的就是"点了没反应、图形画不出来"（实测定位到的画圆失灵根因）。
     */
    ShapeId allocateShapeId();

    /**
     * @brief 接管一个图元并立即重算它。
     * @param shape 图元所有权移交本文档（不得为 nullptr）。
     * @return 该图元的 id。
     * @warning 若 shape 的 id 已被占用或为 0，将拒绝接管并返回 kInvalidShapeId（图元仍归调用方）。
     */
    ShapeId addShape(Shape *shape);

    /**
     * @brief 删除图元，并级联删除全部依赖它的图元（含传递依赖者）。
     * @param ids 待删除的图元 id 集合。
     * @return 实际被删除的图元 id 列表（含级联部分），按 id 升序。
     * @note  级联是必要的：父图元消失后，派生图元会永久无效，
     *        与其留下悬空引用，不如一并删除并由上层提示用户（删除可撤销）。
     */
    QList<ShapeId> removeShapes(const QSet<ShapeId> &ids);

    /**
     * @brief 启用或禁用一个图元。
     * @param id 图元标识。
     * @param enabled 是否启用。
     * @return 图元存在并成功修改时返回 true。
     */
    bool setShapeEnabled(ShapeId id, bool enabled);

    /**
     * @brief 修改自由点的坐标并重算受影响子图。
     * @param id 点图元 id。
     * @param position 新的场景坐标。
     * @return 修改成功时返回 true。
     * @note  附着点会被投影到父曲线，因此实际位置可能与传入值不同。
     */
    bool movePoint(ShapeId id, const Vec2 &position);

    /**
     * @brief 改变点的构造方式（自由点 ↔ 附着点）。
     * @param id 点图元 id。
     * @param kind 新的构造方式。
     * @param hostId 附着点的父曲线 id；kind 为 Free 时忽略。
     * @return 修改成功时返回 true。
     */
    bool setPointKind(ShapeId id, PointKind kind, ShapeId hostId);

    /**
     * @brief 修改直线族曲线的基准曲线（仅垂线/平行线有意义）。
     * @param id 图元标识。
     * @param baseCurveId 新的基准曲线 id。
     * @return 修改成功时返回 true。
     */
    bool setLineBaseCurve(ShapeId id, ShapeId baseCurveId);

    /**
     * @brief 修改测量标注的锚点。
     * @param id 测量图元 id。
     * @param anchor 新的场景坐标锚点。
     * @return 修改成功时返回 true。
     */
    bool setMeasurementAnchor(ShapeId id, const Vec2 &anchor);

    /**
     * @brief 清空文档，回到"新文档"状态。
     */
    void clear();

    // ----- 选中集 -----

    /** @brief 返回当前选中的图元 id 列表（按 id 升序）。 */
    QList<ShapeId> selection() const;

    /**
     * @brief 设置选中集。
     * @param ids 新的选中图元 id 集合；不存在的 id 会被忽略。
     */
    void setSelection(const QSet<ShapeId> &ids);

    /**
     * @brief 清空选中集。
     */
    void clearSelection();

    /** @brief 查询某图元是否被选中。 */
    bool isSelected(ShapeId id) const { return m_selection.contains(id); }

    // ----- 命中测试 -----

    /**
     * @brief 查找场景坐标附近最合适的**点**图元。
     * @param scenePos 场景坐标。
     * @param tolerance 场景单位容差（由视图按屏幕像素换算后传入）。
     * @return 命中的点 id；未命中返回 kInvalidShapeId。
     * @note  点优先于曲线：点更小，若与曲线同时命中应以点为准。
     */
    ShapeId hitTestPoint(const Vec2 &scenePos, double tolerance) const;

    /**
     * @brief 查找场景坐标附近最合适的**曲线**图元。
     * @param scenePos 场景坐标。
     * @param tolerance 场景单位容差。
     * @return 命中的曲线 id；未命中返回 kInvalidShapeId。
     */
    ShapeId hitTestCurve(const Vec2 &scenePos, double tolerance) const;

    /**
     * @brief 查找场景坐标附近的任意图元（点优先，其次曲线，最后标注）。
     * @param scenePos 场景坐标。
     * @param tolerance 场景单位容差。
     * @return 命中的图元 id；未命中返回 kInvalidShapeId。
     */
    ShapeId hitTestAny(const Vec2 &scenePos, double tolerance) const;

    /**
     * @brief 求两条曲线的全部交点。
     * @param firstCurveId 第一条曲线 id。
     * @param secondCurveId 第二条曲线 id。
     * @param results 输出参数：交点坐标会追加到该列表。
     * @return 交点个数（0、1 或 2）。
     */
    int curveIntersections(ShapeId firstCurveId, ShapeId secondCurveId, QList<Vec2> *results) const;

    /**
     * @brief 计算全部有效图元的包围盒。
     * @param minPoint 输出参数：左下角。
     * @param maxPoint 输出参数：右上角。
     * @return 存在有效图元时返回 true；空文档返回 false。
     */
    bool contentBounds(Vec2 *minPoint, Vec2 *maxPoint) const;

    // ----- 批量更新 -----

    /**
     * @brief 开始批量更新：期间挂起 documentChanged 信号。
     * @note  拖动与撤销重做期间必须成对调用 beginUpdate/endUpdate，
     *        否则每次中间状态都会触发一次全画布重绘。
     */
    void beginUpdate();

    /**
     * @brief 结束批量更新并（若期间有变动）发出一次 documentChanged。
     */
    void endUpdate();

signals:
    /** @brief 图元集合或几何发生变化，视图应重绘。 */
    void documentChanged();

    /** @brief 选中集发生变化。 */
    void selectionChanged();

private:
    /**
     * @brief 允许几何自测程序读取内部索引，用于验证"父 id < 自身 id"这一拓扑序不变式。
     * @note  仅测试代码使用，生产代码不得依赖。
     */
    friend class GeometrySmokeTest;

    /**
     * @brief 重建"父 → 子"索引。
     */
    void rebuildIndex();

    /**
     * @brief 重算指定图元及其全部传递依赖者（按 id 升序）。
     * @param changedIds 发生变化的图元集合（其自身也会被重算）。
     */
    void recomputeSubgraph(const QSet<ShapeId> &changedIds);

    /**
     * @brief 重算单个图元。
     * @param id 图元 id。
     */
    void recomputeShape(ShapeId id);

    /**
     * @brief 标记有未提交的变动，并在非批量状态下发出 documentChanged。
     */
    void notifyChanged();

    /**
     * @brief 从选中集中剔除已不存在的 id。
     */
    void pruneSelection();

    QHash<ShapeId, Shape *> m_shapes;         ///< id → 图元。
    QHash<ShapeId, QList<ShapeId> > m_children; ///< 父 id → 直接子图元列表。
    QSet<ShapeId> m_selection;                ///< 当前选中集。
    ShapeId m_nextId;                         ///< 下一个可用 id。
    int m_updateDepth;                        ///< 批量更新嵌套深度。
    bool m_pendingChange;                     ///< 批量期间是否有未发出的变动。
};

#endif // GEOBOARDGEODOCUMENT_H
