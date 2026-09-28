/**
 * @file    view/ShapeListPanel.h
 * @brief   对象列表面板：把文档里的全部图元摊平成一张可排序、可多选的清单。
 * @details 面板解决的是"图形多了以后看不见、选不中、删不掉"的问题：它把
 *          GeoDocument 里的图元列成"编号 / 类型 / 说明"三列，并只承担三件事：
 *
 *          1. **双向选中同步**：在清单里选中若干行 → 写入文档的选中集；
 *             文档选中集变化（例如在画布上点选）→ 清单高亮对应行。
 *             两个方向都有防递归处理（见实现文件中 onTreeSelectionChanged()
 *             与 syncTreeSelectionFromDocument() 的注释）；
 *          2. **聚焦请求**：双击某一行发 focusRequested()，由控制器决定如何动相机；
 *          3. **删除请求**：右键菜单与 Delete 键都只发 deleteRequested()。
 *
 *          为什么用 QTreeWidget 而不是 QTableWidget：
 *          - 本面板是"一行一个图元"的只读清单，QTreeWidget 直接支持
 *            setUniformRowHeights(true)（图元多时按等高行布局）与按列排序；
 *          - 每个 QTreeWidgetItem 都能挂自定义角色数据，本面板把 ShapeId 存在
 *            Qt::UserRole 里，于是**排序之后行与 id 的对应关系依然正确**，
 *            不需要（也不允许）用行号反查图元；
 *          - 图元之间本来就有父子依赖（附着点的父是曲线、派生线的父是基准曲线），
 *            若以后要按依赖关系折叠展示，QTreeWidget 天然支持层级，不必换控件；
 *            而 QTableWidget 做不到层级，且对只读清单要多写一层单元格管理代码。
 *
 *          不负责：删除图元（删除涉及撤销栈，属于 controller 的职责）、
 *          移动相机与缩放（只发 focusRequested）、图元的几何推导与重算
 *          （见 model/Shape、model/GeoDocument）。本类不新增、不删除、不修改
 *          任何图元，对文档只做"读图元 + 改选中集"两件事。
 */

#ifndef GEOBOARDSHAPELISTPANEL_H
#define GEOBOARDSHAPELISTPANEL_H

#include "model/Shape.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

class GeoDocument;
class QAction;
class QEvent;
class QKeyEvent;
class QLabel;
class QMenu;
class QPoint;
class QTreeWidget;
class QTreeWidgetItem;

/**
 * @brief 对象列表面板：列出并管理当前文档中的全部图元。
 * @details 用法：构造后调用 setDocument() 绑定文档即可，之后列表会随
 *          documentChanged() 自动刷新；把本控件放进 QDockWidget 就得到
 *          一个可停靠的对象面板。所有"改动文档"的意图都以信号形式交给上层。
 */
class ShapeListPanel : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造面板。
     * @param parent 父控件，Qt 对象树负责其生命周期（通常放进 QDockWidget）。
     */
    explicit ShapeListPanel(QWidget *parent = nullptr);

    /** @brief 析构面板；列表项、右键菜单都归 Qt 对象树所有，无需手工释放。 */
    ~ShapeListPanel() override;

    /**
     * @brief 绑定要展示的文档。
     * @param document 文档指针，**不拥有**；传 nullptr 表示清空面板。
     * @note  绑定后会连接 documentChanged/selectionChanged 自动刷新；重复绑定
     *        同一个文档不会叠加连接，先绑定别的文档再绑定回来也不会重复刷新。
     */
    void setDocument(GeoDocument *document);

    /**
     * @brief 返回当前文档。
     * @return 文档指针；未绑定时返回 nullptr。
     */
    GeoDocument *document() const;

    /**
     * @brief 立即刷新列表。
     * @note  正常情况下由 documentChanged() 自动触发；对外暴露是为了便于测试
     *        与强制刷新。刷新是**增量**的（按 id 复用已有的行），因此不会丢失
     *        滚动位置与当前行。
     */
    void refresh();

signals:
    /**
     * @brief 用户在列表里选中了若干对象（面板已同步写入文档的选中集）。
     * @param ids 新的选中图元 id 列表，按 id 升序。
     */
    void selectionChanged(const QList<ShapeId> &ids);

    /**
     * @brief 用户双击了某个对象，请求把视图聚焦到它。
     * @param id 目标图元 id。
     */
    void focusRequested(ShapeId id);

    /** @brief 用户请求删除选中的对象（Delete 键或右键菜单）。 */
    void deleteRequested();

protected:
    /**
     * @brief 建议尺寸。
     * @return 一个足够放下"编号 / 类型 / 说明"三列内容的尺寸。
     * @note  必须给出这个提示：面板放在 QDockWidget 里时，Qt 会直接采用控件的
     *        sizeHint 决定停靠区初始宽度。若不覆盖，默认提示会小到把"说明"列
     *        截成省略号，而"说明"恰恰是这一列最有价值的信息（半径、宿主、坐标）。
     *        这个宽度与"三列都按内容自适应"配合，才既有信息又不挤压画布。
     */
    QSize sizeHint() const override;

    /**
     * @brief 处理面板自身的按键。
     * @param event 按键事件。
     * @note  Delete 键发 deleteRequested()。焦点在列表上时按键先到列表、不会
     *        到这里，因此实现里另装了事件过滤器，两条路径发同一个信号。
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @brief 事件过滤器：把落在列表上的 Delete 键也翻译成 deleteRequested()。
     * @param watched 事件接收者（本面板只关心列表及其视口）。
     * @param event 事件。
     * @return 已处理返回 true，否则交回基类。
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    /**
     * @brief 列表选中项变化 → 同步到文档的选中集。
     */
    void onTreeSelectionChanged();

    /**
     * @brief 文档的选中集变化 → 同步到列表。
     * @note  它与 onTreeSelectionChanged() 会互相触发，实现里用
     *        m_updatingFromDocument 标志 + QSignalBlocker 双重阻断递归。
     */
    void onDocumentSelectionChanged();

    /**
     * @brief 双击某一行 → 请求聚焦该对象。
     * @param item 被双击的行；为 nullptr 时忽略。
     * @param column 被双击的列；本面板对整行生效，不使用该参数。
     */
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);

    /**
     * @brief 弹出右键菜单（删除选中对象 / 聚焦 / 刷新列表）。
     * @param position 右键位置，坐标为列表**视口**坐标。
     */
    void onContextMenuRequested(const QPoint &position);

    /**
     * @brief 文档被销毁时自动解绑。
     * @param object 被销毁的对象。
     * @note  本面板只持有文档指针、不拥有文档；若文档先于面板销毁而不处理，
     *        m_document 就成了悬空指针，之后任何一次 refresh() 都会崩。
     *        Qt 会在对象销毁时断开连接，所以这里只需把指针清掉并清空列表。
     */
    void onDocumentDestroyed(QObject *object);

private:
    /** @brief 创建顶部的说明标签。 */
    void createSummaryLabel();

    /** @brief 创建图元列表（列、排序、多选、右键菜单策略、事件过滤器）。 */
    void createTree();

    /** @brief 创建右键菜单与其中的动作。 */
    void createContextMenu();

    /**
     * @brief 返回图元类型的中文名称（线段/射线/直线由 LineKind 细分）。
     * @param shape 图元；为 nullptr 时返回空串。
     * @return 供"类型"列显示的名称。
     * @note  本函数覆盖 ShapeType 的**全部**取值，刻意不写 default 分支：
     *        将来新增枚举值时编译器能在这里提醒补全，而不是悄悄显示空白。
     */
    static QString typeDisplayName(const Shape *shape);

    /**
     * @brief 返回图元最有用的属性，供"说明"列显示。
     * @param shape 图元；为 nullptr 时返回空串。
     * @return 说明文本；退化图元会追加"（失效）"，被停用的追加"（已停用）"。
     */
    static QString describeShape(const Shape *shape);

    /**
     * @brief 返回测量类型的中文名称。
     * @param kind 测量类型。
     * @return 名称文本。
     */
    static QString measurementKindName(MeasurementKind kind);

    /**
     * @brief 读取列表项里保存的图元 id。
     * @param item 列表项；可为 nullptr。
     * @return 图元 id；该项不存在或数据缺失时返回 kInvalidShapeId。
     * @note  id 一律从 Qt::UserRole 里取，**绝不**用行号推 id：列表可排序、
     *        可增删，行号与 id 的对应关系随时会变。
     */
    static ShapeId shapeIdOf(const QTreeWidgetItem *item);

    /**
     * @brief 以文档的选中集为准刷新列表的选中态。
     * @note  同步期间会阻断列表信号并置 m_updatingFromDocument，避免回调
     *        onTreeSelectionChanged() 再写回文档而形成往复。
     */
    void syncTreeSelectionFromDocument();

    /**
     * @brief 以列表的选中项为准写入文档的选中集，并发出 selectionChanged()。
     * @note  写文档期间置 m_updatingFromDocument，使文档同步发出的
     *        selectionChanged() 不会被当作"外部改选"再处理一遍。
     */
    void writeSelectionToDocument();

    /**
     * @brief 把选中集收敛到单独一行（右键点击未选中行时使用）。
     * @param item 要选中的行；为 nullptr 时不做任何事。
     * @note  收敛过程阻断列表信号，结束后只写一次文档，避免"清空 + 选中"
     *        各写一遍选中集。
     */
    void selectOnlyItem(QTreeWidgetItem *item);

    /** @brief 按当前图元总数与选中数更新顶部说明标签。 */
    void updateSummaryLabel();

    GeoDocument *m_document;                        ///< 被展示的文档，不拥有；可为 nullptr。
    QTreeWidget *m_tree;                            ///< 图元列表。
    QLabel *m_summaryLabel;                         ///< 顶部说明标签（共 N 个 / 选中 M 个）。
    QMenu *m_contextMenu;                           ///< 右键菜单（创建一次，反复使用）。
    QAction *m_focusAction;                         ///< 右键菜单：聚焦到此对象。
    QAction *m_deleteAction;                        ///< 右键菜单：删除选中的对象。
    QAction *m_refreshAction;                       ///< 右键菜单：刷新列表。
    QHash<ShapeId, QTreeWidgetItem *> m_itemsById;  ///< id → 列表项；行与 id 的对应关系只认这一张表。
    bool m_updatingFromDocument;                    ///< 正在由文档（或程序）同步列表，用于阻断递归。
};

#endif // GEOBOARDSHAPELISTPANEL_H
