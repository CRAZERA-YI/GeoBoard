/**
 * @file    model/Commands.h
 * @brief   撤销/重做的命令对象。
 * @details 采用 `QUndoStack` + 命令对象，而不是"整份文档快照回滚"：命令对象只保存
 *          受影响的图元，内存开销小；语义清晰（每个命令对应一次用户操作），
 *          并且天然支持"一次拖动 = 一条命令"这种合并语义。
 *
 *          所有命令都通过 GeoDocument 的公开修改接口生效（addShape / removeShapes /
 *          movePoint …），因此撤销重做与正常编辑走的是同一套依赖图与重算逻辑，
 *          不存在"撤销时忘了重算"这类漏洞。
 *
 *          不负责：决定何时压栈（见 controller/MainController）。
 */

#ifndef GEOBOARDCOMMANDS_H
#define GEOBOARDCOMMANDS_H

#include "GeoDocument.h"
#include "Shape.h"

#include <QList>
#include <QUndoCommand>

/**
 * @brief 新增图元的命令。
 * @details 命令**持有传入图元的快照**，每次 redo 都向文档递交一份克隆。
 *          因此调用方在构造本命令后仍应释放自己那份（或直接移交，见构造函数说明），
 *          而命令自身可被反复 undo/redo 而不失效。
 */
class AddShapesCommand : public QUndoCommand
{
public:
    /**
     * @brief 构造新增命令。
     * @param document 目标文档，不得为 nullptr。
     * @param shapes 待新增的图元（所有权移交本命令），按 id 升序传入。
     * @param text 命令文本（显示在撤销菜单里）。
     * @param parent 父命令。
     */
    AddShapesCommand(GeoDocument *document,
                     const QList<Shape *> &shapes,
                     const QString &text,
                     QUndoCommand *parent = nullptr);

    /** @brief 析构并释放本命令持有的快照。 */
    ~AddShapesCommand() override;

    void undo() override;
    void redo() override;

private:
    GeoDocument *m_document;      ///< 目标文档，不拥有。
    QList<Shape *> m_snapshots;   ///< 本命令持有的图元快照，按 id 升序。
};

/**
 * @brief 删除图元的命令。
 * @details 删除会被 GeoDocument 级联扩展到依赖者，因此本命令保存的是
 *          **首次执行时实际被删除的全部图元**（含级联部分），撤销时原样恢复；
 *          后续 redo 也严格按这份快照的 id 删除，保证与 undo 对称。
 *          命令持有的是克隆快照，因此可反复 undo/redo 而不失效。
 */
class RemoveShapesCommand : public QUndoCommand
{
public:
    /**
     * @brief 构造删除命令。
     * @param document 目标文档，不得为 nullptr。
     * @param ids 待删除的图元 id（级联删除由文档在执行时决定）。
     * @param text 命令文本。
     * @param parent 父命令。
     */
    RemoveShapesCommand(GeoDocument *document,
                        const QSet<ShapeId> &ids,
                        const QString &text,
                        QUndoCommand *parent = nullptr);

    /** @brief 析构并释放仍被本命令持有的图元。 */
    ~RemoveShapesCommand() override;

    void undo() override;
    void redo() override;

    /**
     * @brief 返回上一次执行时实际删除的图元 id 列表（含级联删除）。
     * @return 被删除的 id，按升序；未执行过时为空。
     */
    QList<ShapeId> removedIds() const { return m_removedIds; }

private:
    GeoDocument *m_document;    ///< 目标文档，不拥有。
    QSet<ShapeId> m_requestedIds; ///< 用户请求删除的 id。
    QList<ShapeId> m_removedIds;  ///< 实际被删除的 id（含级联）。
    QList<Shape *> m_snapshots;   ///< 被删除图元的副本，按 id 升序。
    bool m_firstExecution;        ///< 是否尚未记录过删除结果。
};

/**
 * @brief 移动点的命令。
 * @details 一次拖动只在**结束时**压入一条命令，用 QUndoCommand::mergeWith 之外的
 *          做法更直观：构造时传入起点与终点坐标，命令本身就是一次完整移动。
 */
class MovePointCommand : public QUndoCommand
{
public:
    /**
     * @brief 构造移动命令。
     * @param document 目标文档，不得为 nullptr。
     * @param pointId 被移动的点 id。
     * @param from 拖动起点坐标（场景单位）。
     * @param to 拖动终点坐标（场景单位）。
     * @param text 命令文本。
     * @param parent 父命令。
     */
    MovePointCommand(GeoDocument *document,
                     ShapeId pointId,
                     const Vec2 &from,
                     const Vec2 &to,
                     const QString &text,
                     QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    /**
     * @brief 把点移动到指定位置。
     * @param position 目标坐标。
     */
    void applyPosition(const Vec2 &position);

    GeoDocument *m_document; ///< 目标文档，不拥有。
    ShapeId m_pointId;       ///< 被移动的点。
    Vec2 m_from;             ///< 起点坐标。
    Vec2 m_to;               ///< 终点坐标。
};

#endif // GEOBOARDCOMMANDS_H
