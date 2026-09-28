/**
 * @file    controller/MainController.h
 * @brief   编排层：工具状态机、撤销栈、文件动作与状态提示。
 * @details 控制器是唯一同时认识 View、Model 与 Service 的地方（main.cpp 负责组装）。
 *          它做三件事：
 *          1. 把画布发来的**语义化信号**翻译成对文档的改动（工具状态机）；
 *          2. 决定何时把改动压入撤销栈（一次拖动 = 一条命令）；
 *          3. 把结果与提示推回视图（调用视图公开方法，不碰任何控件成员）。
 *
 *          不负责：几何求解（model）、绘制（view）、JSON 读写细节（service）。
 */

#ifndef GEOBOARDMAINCONTROLLER_H
#define GEOBOARDMAINCONTROLLER_H

#include "ToolId.h"
#include "model/Commands.h"
#include "model/GeoDocument.h"
#include "model/Shape.h"
#include "service/JsonStore.h"

#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>

class GeoBoardMainWindow;
class SceneExporter;
class QUndoStack;

/**
 * @brief 应用主控制器。
 */
class MainController : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造控制器并完成全部信号槽接线。
     * @param view 主窗口，不拥有（由 main 的栈对象管理）。
     * @param parent 父对象。
     */
    explicit MainController(GeoBoardMainWindow *view, QObject *parent = nullptr);

    /** @brief 析构控制器。 */
    ~MainController() override;

    /**
     * @brief 返回撤销栈。
     * @return 撤销栈指针，归本控制器所有；主要用于测试断言与将来的"历史"面板。
     * @note  头文件不引入 QUndoStack，调用方需要它的成员时自行包含 <QUndoStack>。
     */
    QUndoStack *undoStack() const { return m_undoStack; }

    /**
     * @brief 启动：绑定文档到画布、初始化界面状态。
     */
    void start();

    /**
     * @brief 返回当前工具。
     * @return 当前工具标识。
     * @note  供测试与状态显示使用；切换工具请走 onToolChangeRequested()，
     *        否则工具栏的选中态会与实际状态不一致。
     */
    ToolId currentTool() const { return m_currentTool; }

    /**
     * @brief 在空文档里放一个内置演示图形（圆与两条线段的交点、中点、标注）。
     * @details 用途是**验收与排查**：命令行加 `--demo` 即可在图形界面里直接看到
     *          "拖动点 → 依赖图形跟随"的效果，不必先手工点一遍作图工具；
     *          它同时也是 06 阶段手工清单里"渲染与依赖重算"这一项的快速入口。
     *
     *          本函数只在 `--demo` 时被调用，正常运行路径不会执行到。
     * @note  图形被直接写入文档（不经过撤销栈）：它属于启动状态，不是用户操作。
     */
    void createDemoConstruction();

public slots:
    // ----- 画布交互 -----

    /**
     * @brief 处理画布单击。
     * @param scenePos 单击位置的场景坐标。
     */
    void onCanvasClicked(const QPointF &scenePos);

    /**
     * @brief 处理画布双击（结束多边形绘制）。
     * @param scenePos 双击位置的场景坐标。
     */
    void onCanvasDoubleClicked(const QPointF &scenePos);

    /**
     * @brief 处理"开始拖动点"。
     * @param id 被按下的点。
     */
    void onPointDragStarted(ShapeId id);

    /**
     * @brief 处理拖动中的位置更新。
     * @param id 点 id。
     * @param scenePos 新的场景坐标。
     */
    void onPointDragged(ShapeId id, const QPointF &scenePos);

    /** @brief 处理拖动结束：此处才把整段拖动压入撤销栈。 */
    void onPointDragFinished();

    /**
     * @brief 处理"按下-拖动"式作图的开始。
     * @param id 按下时命中的点 id；kInvalidShapeId 表示按在空白处（留待 clicked 新建）。
     * @details 与"点两下"共用同一套状态：按下即确定起点，拖到终点松手完成，
     *          也可以松手后再点一下第二点完成。两种操作方式可以混用。
     */
    void onConstructionDragStarted(ShapeId id);

    /**
     * @brief 处理"按下-拖动"式作图的结束。
     * @param scenePos 松开位置的场景坐标。
     */
    void onConstructionDragFinished(const QPointF &scenePos);

    // ----- 右键快捷添加 -----

    /** @brief 在指定位置添加一个自由点。 @param scenePos 场景坐标。 */
    void onQuickPointRequested(const QPointF &scenePos);

    /** @brief 从指定位置开始画线段。 @param scenePos 场景坐标。 */
    void onQuickSegmentRequested(const QPointF &scenePos);

    /** @brief 以指定位置为圆心开始画圆。 @param scenePos 场景坐标。 */
    void onQuickCircleRequested(const QPointF &scenePos);

    /** @brief 从指定位置开始测量距离。 @param scenePos 场景坐标。 */
    void onQuickDistanceRequested(const QPointF &scenePos);

    /** @brief 右键菜单请求删除选中图元。 */
    void onQuickDeleteRequested();

    /**
     * @brief 处理选中集变化。
     * @param ids 新的选中图元 id 列表。
     */
    void onSelectionChanged(const QList<ShapeId> &ids);

    /** @brief 请求删除选中图元。 */
    void onDeleteRequested();

    /** @brief 请求取消当前未完成的作图。 */
    void onEscapeRequested();

    /**
     * @brief 处理缩放请求（滚轮）。
     * @param factor 缩放倍数。
     * @param scenePos 锚点场景坐标。
     */
    void onZoomRequested(double factor, const QPointF &scenePos);

    /**
     * @brief 处理平移请求（中键拖拽）。
     * @param sceneDelta 场景坐标增量。
     */
    void onPanRequested(const QPointF &sceneDelta);

    // ----- 工具栏与菜单 -----

    /**
     * @brief 切换当前工具。
     * @param tool 目标工具。
     */
    void onToolChangeRequested(ToolId tool);

    /** @brief 切换网格显示。 @param visible 是否显示。 */
    void onGridVisibilityToggled(bool visible);

    /** @brief 切换坐标轴显示。 @param visible 是否显示。 */
    void onAxesVisibilityToggled(bool visible);

    /** @brief 切换点名与标注显示。 @param visible 是否显示。 */
    void onLabelsVisibilityToggled(bool visible);

    /** @brief 新建文档。 */
    void onNewRequested();

    /** @brief 打开文件。 @param path 文件路径；为空表示需要询问。 */
    void onOpenRequested(const QString &path);

    /** @brief 保存文件。 @param path 目标路径；为空表示"另存为"，需要询问。 */
    void onSaveRequested(const QString &path);

    /** @brief 撤销。 */
    void onUndoRequested();

    /** @brief 重做。 */
    void onRedoRequested();

    /** @brief 放大视图。 */
    void onZoomInRequested();

    /** @brief 缩小视图。 */
    void onZoomOutRequested();

    /** @brief 恢复默认视图。 */
    void onZoomResetRequested();

    /** @brief 视图适配全部内容。 */
    void onFitContentRequested();

    /**
     * @brief 导出当前作图为图片（PNG 或 SVG）。
     * @param path 目标路径；为空表示需要弹出保存对话框。
     * @note  格式由路径扩展名决定：`.svg` 走矢量导出，其余按 PNG 处理。
     */
    void onExportRequested(const QString &path);

    /**
     * @brief 切换吸附对齐。
     * @param enabled 是否启用。
     */
    void onSnapToggled(bool enabled);

    /**
     * @brief 调整坐标轴网格的最小精度。
     * @param minimumStep 每格不少于多少场景单位。
     * @note  只影响网格绘制与吸附颗粒度，**不改动任何已存在的图形或坐标**。
     */
    void onGridPrecisionRequested(double minimumStep);

    /** @brief 显示"关于"对话框。 */
    void onAboutRequested();

private:
    /**
     * @brief 在指定位置取得一个可用于作图的点：命中已有点则复用，否则新建自由点。
     * @param scenePos 场景坐标。
     * @param shapes 输出参数：本函数新建的图元会被追加到这里（供调用方组装命令）。
     * @param excludedIds 需要排除的图元 id 列表（通常是本次作图已经用过的点）。
     * @return 可用的点 id。
     * @note  复用已有点是几何画板类软件的标准行为：否则用户必须精确点在空白处
     *        才能得到"独立的点"，作图会变得难以控制。
     * @note  `excludedIds` 是必要的：测量角度时若第三下点在顶点附近，吸附会把顶点
     *        当成第三个点，角退化成 0° 且界面上看不到任何反馈（标注显示"—"）——
     *        这是用户反馈"角度功能不能正常使用"的直接成因。排除已用点后会在
     *        旁边新建一个点，角就能正常显示。
     */
    ShapeId acquirePoint(const QPointF &scenePos, QList<Shape *> *shapes,
                         const QList<ShapeId> &excludedIds = QList<ShapeId>());

    /**
     * @brief 以某个已有点为起点开始作图（右键快捷菜单与拖动作图共用）。
     * @param startPos 起点场景坐标。
     * @param tool 要使用的工具。
     * @return 起点图元 id；工具不适用或起点无法确定时返回 kInvalidShapeId。
     */
    ShapeId beginConstructionAt(const QPointF &startPos, ToolId tool);

    /**
     * @brief 查找指定位置下的曲线。
     * @param scenePos 场景坐标。
     * @return 曲线 id；未命中返回 kInvalidShapeId。
     */
    ShapeId curveAt(const QPointF &scenePos) const;

    /**
     * @brief 查找指定位置下的图元（点优先）。
     * @param scenePos 场景坐标。
     * @return 图元 id；未命中返回 kInvalidShapeId。
     */
    ShapeId shapeAt(const QPointF &scenePos) const;

    /**
     * @brief 处理需要"曲线 + 点"的工具（垂线、平行线）。
     * @param scenePos 点击位置。
     */
    void handlePerpendicularFamily(const QPointF &scenePos);

    /**
     * @brief 处理交点工具。
     * @param scenePos 点击位置。
     */
    void handleIntersection(const QPointF &scenePos);

    /**
     * @brief 处理测量类工具。
     * @param scenePos 点击位置。
     */
    void handleMeasurement(const QPointF &scenePos);

    /**
     * @brief 处理角平分线（三次点击）。
     * @param scenePos 点击位置。
     */
    void handleAngleBisector(const QPointF &scenePos);

    /**
     * @brief 处理多边形（多次点击，双击结束）。
     * @param scenePos 点击位置。
     */
    void handlePolygon(const QPointF &scenePos);

    /**
     * @brief 结束多边形绘制并生成图元。
     */
    void finishPolygon();

    /**
     * @brief 用两个已确定的点完成作图（线段/射线/直线/圆/中点/距离）。
     * @param scenePos 终点位置的场景坐标。
     * @param tool 要完成的工具。
     * @param startId 已确定的起点。
     * @note  这是"完成一次两点作图"的**唯一**落点：点两下、按住拖动、双击起笔
     *        三种操作方式都汇聚到这里，因此不会出现某种方式下行为不一致。
     */
    void finishTwoPointConstruction(const QPointF &scenePos, ToolId tool, ShapeId startId);

    /**
     * @brief 处理两点确定的作图工具（线段、射线、直线、圆、中点）。
     * @param scenePos 点击位置。
     * @param tool 当前工具。
     */
    void handleTwoPointTool(const QPointF &scenePos, ToolId tool);

    /**
     * @brief 处理三点圆弧（三次点击）。
     * @param scenePos 点击位置。
     */
    void handleArc(const QPointF &scenePos);

    /**
     * @brief 处理"圆心 + 两点"圆弧（三次点击：圆心、半径点、弧上点）。
     * @param scenePos 点击位置。
     * @note  与三点弧分开处理而不是合并：两者的取点含义不同（圆心/半径点 vs 起点/弧上点），
     *        合并只会让一个函数里塞两套分支，反而更容易出错。
     */
    void handleArcCenter(const QPointF &scenePos);

    /**
     * @brief 放弃当前未完成的作图，恢复初始状态。
     */
    void cancelPendingDrawing();

    /**
     * @brief 响应对象列表的选中变化。
     * @param ids 新的选中图元 id 列表（面板已经写入文档，这里只更新状态栏）。
     * @note  这里**不再**回写文档的选中集：面板已经写过了，回写会绕成环。
     */
    void onShapeListSelectionChanged(const QList<ShapeId> &ids);

    /**
     * @brief 把视图聚焦到对象列表里双击的那个图元。
     * @param id 目标图元 id。
     */
    void onShapeListFocusRequested(ShapeId id);

    /** @brief 删除对象列表里选中的图元（复用与画布 Delete 相同的删除逻辑）。 */
    void onShapeListDeleteRequested();

    /**
     * @brief 更新圆心式圆弧的分步预览。
     * @param center 已确定的圆心（圆心还没定时为鼠标位置）。
     * @param cursor 当前鼠标位置（场景坐标）。
     * @note  预览内容取决于状态机走到第几步，因此判定放在控制器里；
     *        画布只负责把画面画出来。这样"画布以为的进度"与"控制器记的进度"
     *        不会变成两份互相打架的判断。
     */
    void onArcCenterPreviewNeeded(const QPointF &center, const QPointF &cursor);

    /**
     * @brief 刷新作图预览与状态栏提示。
     * @param scenePos 当前鼠标位置（场景坐标）。
     */
    void refreshPreview(const QPointF &scenePos);

    /**
     * @brief 高亮当前步骤期待的输入图元（例如基准曲线）。
     * @param scenePos 鼠标位置（场景坐标）。
     */
    void refreshInputHighlight(const QPointF &scenePos);

    /**
     * @brief 返回图元的中文名称（与画布上的标号一致）。
     * @param id 图元 id。
     * @return 名称文本；图元不存在时返回空串。
     */
    QString shapeDisplayName(ShapeId id) const;

    /**
     * @brief 在未保存时询问用户，必要时保存。
     * @return 用户取消操作时返回 false；可以继续（已保存或选择不保存）返回 true。
     */
    bool confirmDiscardChanges();

    /**
     * @brief 文件加载成功后重置全部界面状态。
     * @param path 已加载的文件路径（空串表示新文档）。
     */
    void resetForNewDocument(const QString &path);

    /**
     * @brief 更新状态栏的当前坐标文本。
     * @param scenePos 场景坐标。
     */
    void updateMouseStatus(const QPointF &scenePos);

    GeoBoardMainWindow *m_view;   ///< 主窗口，不拥有。
    GeoDocument *m_document;      ///< 几何文档，本控制器的子对象。
    JsonStore *m_store;           ///< 文件读写，本控制器的子对象。
    SceneExporter *m_exporter;    ///< 图片导出（PNG / SVG），本控制器的子对象。
    QUndoStack *m_undoStack;      ///< 撤销栈，本控制器的子对象。
    QString m_filePath;           ///< 当前文件路径；空串表示尚未保存过。

    ToolId m_currentTool;         ///< 当前工具。

    // 作图状态机（一次只可能有一个工具处于进行中）
    ShapeId m_baseCurveId;        ///< 垂线/平行线已选定的基准曲线。
    ShapeId m_firstCurveId;       ///< 交点工具已选定的第一条曲线。
    ShapeId m_curveFilterShapeId; ///< 测量工具已选定的被测图元。
    ShapeId m_toolPointAId;       ///< 第一个已确定的点。
    ShapeId m_toolPointBId;       ///< 第二个已确定的点。
    ShapeId m_arcCenterPointId;   ///< 圆心式圆弧：已确定的圆心。
    ShapeId m_arcRadiusPointId;   ///< 圆心式圆弧：已确定的半径点。
    QList<ShapeId> m_polygonPointIds; ///< 多边形已确定的顶点。
    QList<QPointF> m_previewScenePoints; ///< 预览用的已确定顶点坐标。

    // 拖动状态
    ShapeId m_dragPointId;        ///< 正在拖动的点。
    Vec2 m_dragStartPosition;     ///< 拖动起点（用于构造撤销命令）。
    bool m_isDraggingPoint;       ///< 是否真的产生了位移。
};

#endif // GEOBOARDMAINCONTROLLER_H
