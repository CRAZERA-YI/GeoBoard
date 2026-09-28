/**
 * @file    model/Commands.cpp
 * @brief   撤销/重做命令的实现。
 * @details 所有权模型（两条命令完全对称，务必按此理解代码）：
 *
 *          **命令永远持有自己的快照，文档永远持有自己那一份克隆。**
 *
 *          文档的 addShape 只接管"传进去的那个指针"，removeShapes 会 delete
 *          文档持有的对象。若让命令把自己的图元直接交给文档，undo 时就只能拿到
 *          悬空指针。因此两个方向都传克隆：命令的快照寿命与命令相同，
 *          反复撤销/重做既不会泄漏，也不会重复释放。
 */

#include "Commands.h"

#include <algorithm>

// ---------------------------------------------------------------------------
// AddShapesCommand
// ---------------------------------------------------------------------------

AddShapesCommand::AddShapesCommand(GeoDocument *document,
                                   const QList<Shape *> &shapes,
                                   const QString &text,
                                   QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_snapshots(shapes)
{
}

AddShapesCommand::~AddShapesCommand()
{
    qDeleteAll(m_snapshots);
    m_snapshots.clear();
}

void AddShapesCommand::redo()
{
    if (m_document == nullptr) {
        return;
    }

    m_document->beginUpdate();
    for (int i = 0; i < m_snapshots.size(); ++i) {
        // 传入克隆：文档持有它并在删除时释放，本命令的快照保持完好以备再次 redo
        m_document->addShape(m_snapshots.at(i)->clone());
    }
    m_document->endUpdate();
}

void AddShapesCommand::undo()
{
    if (m_document == nullptr) {
        return;
    }

    QSet<ShapeId> ids;
    for (int i = 0; i < m_snapshots.size(); ++i) {
        ids.insert(m_snapshots.at(i)->id());
    }

    m_document->beginUpdate();
    m_document->removeShapes(ids);
    m_document->endUpdate();
}

// ---------------------------------------------------------------------------
// RemoveShapesCommand
// ---------------------------------------------------------------------------

RemoveShapesCommand::RemoveShapesCommand(GeoDocument *document,
                                         const QSet<ShapeId> &ids,
                                         const QString &text,
                                         QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_requestedIds(ids)
    , m_firstExecution(true)
{
}

RemoveShapesCommand::~RemoveShapesCommand()
{
    qDeleteAll(m_snapshots);
    m_snapshots.clear();
}

void RemoveShapesCommand::redo()
{
    if (m_document == nullptr) {
        return;
    }

    m_document->beginUpdate();

    if (m_firstExecution) {
        // 首次执行：先按用户点名的 id 克隆快照，再让文档级联删除。
        // 级联的实际范围就是"用户选中的 + 依赖它们的"，撤销时必须原样恢复。
        QList<Shape *> shapesInOrder = m_document->shapesInOrder();
        for (int i = 0; i < shapesInOrder.size(); ++i) {
            Shape *candidate = shapesInOrder.at(i);
            if (!m_requestedIds.contains(candidate->id())) {
                continue;
            }
            m_snapshots.append(candidate->clone());
        }

        m_removedIds = m_document->removeShapes(m_requestedIds);
        m_firstExecution = false;
    } else {
        // 后续 redo：删除范围取首次的实际结果，保证与 undo 严格对称
        QSet<ShapeId> targets;
        for (int i = 0; i < m_removedIds.size(); ++i) {
            targets.insert(m_removedIds.at(i));
        }
        m_removedIds = m_document->removeShapes(targets);
    }

    m_document->endUpdate();
}

void RemoveShapesCommand::undo()
{
    if (m_document == nullptr) {
        return;
    }

    // 快照按 id 升序，正好满足"父先于子"的插入顺序要求
    m_document->beginUpdate();
    for (int i = 0; i < m_snapshots.size(); ++i) {
        m_document->addShape(m_snapshots.at(i)->clone());
    }
    m_document->endUpdate();
}

// ---------------------------------------------------------------------------
// MovePointCommand
// ---------------------------------------------------------------------------

MovePointCommand::MovePointCommand(GeoDocument *document,
                                   ShapeId pointId,
                                   const Vec2 &from,
                                   const Vec2 &to,
                                   const QString &text,
                                   QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_document(document)
    , m_pointId(pointId)
    , m_from(from)
    , m_to(to)
{
}

void MovePointCommand::redo()
{
    applyPosition(m_to);
}

void MovePointCommand::undo()
{
    applyPosition(m_from);
}

void MovePointCommand::applyPosition(const Vec2 &position)
{
    if (m_document == nullptr) {
        return;
    }
    m_document->beginUpdate();
    m_document->movePoint(m_pointId, position);
    m_document->endUpdate();
}
