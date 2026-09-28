/**
 * @file    view/ShapeListPanel.cpp
 * @brief   对象列表面板的实现。
 * @details 界面全部用 C++ 代码装配（本项目禁止 `.ui`）：构造函数只做编排，
 *          说明标签、列表、右键菜单分别拆成 createSummaryLabel()、createTree()、
 *          createContextMenu()。样式纪律与主窗口一致：不指定字体族与字号、
 *          不硬编码颜色（失效行取 palette().color(QPalette::Disabled,
 *          QPalette::Text)）、不设置 Fusion 风格，颜色一律来自调色板。
 *
 *          刷新是**增量对齐**而不是"清空重建"：按 id 复用已有的 QTreeWidgetItem，
 *          只更新变化过的文本与选中态。这样拖动图元时列表不闪、滚动位置与当前行
 *          不丢，图元很多时也不会每次都重建整棵树。
 *
 *          双向同步的防递归措施（两处都是"标志 + QSignalBlocker"双重保险）：
 *          - 列表 → 文档：写文档期间置 m_updatingFromDocument，
 *            GeoDocument::setSelection 同步发出的 selectionChanged() 会被
 *            onDocumentSelectionChanged() 直接忽略；
 *          - 文档 → 列表：同步列表选中态期间置同一个标志，并阻断列表信号，
 *            使 QTreeWidget 的 itemSelectionChanged() 根本不会发出。
 *          于是"列表改 → 文档改 → 列表改"这条链最多走一轮就停住，不会死循环。
 */

#include "ShapeListPanel.h"

#include "model/GeoDocument.h"

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QPoint>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

namespace {

/** @brief "编号"列：显示 #id，同时用自定义角色保存图元 id。 */
const int kIdColumn = 0;

/** @brief "类型"列：点 / 线段 / 圆 …… 的中文名。 */
const int kTypeColumn = 1;

/** @brief "说明"列：该图元最有用的属性。 */
const int kDescriptionColumn = 2;

/** @brief 列数；整行设置前景色时按它遍历。 */
const int kColumnCount = 3;

/** @brief 保存图元 id 的自定义角色；行与 id 的对应关系只经由它。 */
const int kShapeIdRole = Qt::UserRole;

/**
 * @brief 数值格式化。
 * @param value 待格式化的数值。
 * @return 两位小数的文本，与画布上的坐标标签保持一致。
 */
QString formatNumber(double value)
{
    return QString::number(value, 'f', 2);
}

/**
 * @brief 把场景坐标格式化为 "(x, y)"。
 * @param point 场景坐标。
 * @return 形如 "(1.00, -2.50)" 的文本。
 */
QString formatPoint(const Vec2 &point)
{
    return QStringLiteral("(%1, %2)").arg(formatNumber(point.x), formatNumber(point.y));
}

/**
 * @brief 把图元 id 列表格式化为 "#1、#2、#3"。
 * @param ids 图元 id 列表。
 * @return 供界面显示的文本。
 * @note  连接符也是界面文本，因此走 translate 而不是写死字面量；
 *        这里是文件内的辅助函数，没有 QObject 上下文，故显式给出翻译上下文名。
 */
QString formatIdList(const QList<ShapeId> &ids)
{
    QStringList parts;
    for (int i = 0; i < ids.size(); ++i) {
        parts.append(QStringLiteral("#%1").arg(ids.at(i)));
    }
    return parts.join(QCoreApplication::translate("ShapeListPanel", "、"));
}

/**
 * @brief 作用域内把 bool 标志置位、离开时自动还原的 RAII 帮手。
 * @details 刷新与选中同步都有多条出口，手工成对赋值一旦漏掉就会留下
 *          "永远处于同步中"的僵死状态，用它保证任何路径都会复位。
 */
class ScopedFlag
{
public:
    /**
     * @brief 置位标志并记住原值。
     * @param flag 目标标志，不得为 nullptr。
     */
    explicit ScopedFlag(bool *flag) : m_flag(flag), m_previous(*flag) { *m_flag = true; }

    /** @brief 还原标志为进入作用域前的值。 */
    ~ScopedFlag() { *m_flag = m_previous; }

private:
    ScopedFlag(const ScopedFlag &) = delete;
    ScopedFlag &operator=(const ScopedFlag &) = delete;

    bool *m_flag;    ///< 目标标志。
    bool m_previous; ///< 进入作用域前的值。
};

/**
 * @brief 列表项：让"编号"列按 id 数值排序。
 * @details 编号列的显示文本是 "#12" 这样的字符串，按文本排序会得到
 *          #1、#10、#2 …… 的乱序，用户按编号找对象时会很别扭。重写
 *          operator< 后按 id 数值比较，其余列沿用基类的比较规则。
 *          Qt 5.12 的 QTreeWidget 排序正是调用这个虚函数，比较依据的列由
 *          基类实现从 treeWidget()->sortColumn() 取得。
 */
class ShapeTreeItem : public QTreeWidgetItem
{
public:
    /**
     * @brief 构造列表项。
     * @param texts 三列的初始文本（编号 / 类型 / 说明）。
     */
    explicit ShapeTreeItem(const QStringList &texts) : QTreeWidgetItem(texts) {}

    /**
     * @brief 比较两个列表项，供 QTreeWidget 排序使用。
     * @param other 另一个列表项。
     * @return 本项应排在 other 之前时返回 true。
     */
    bool operator<(const QTreeWidgetItem &other) const override
    {
        const QTreeWidget *tree = treeWidget();
        if (tree != nullptr && tree->sortColumn() == kIdColumn) {
            return data(kIdColumn, kShapeIdRole).toInt() < other.data(kIdColumn, kShapeIdRole).toInt();
        }
        return QTreeWidgetItem::operator<(other);
    }
};

/**
 * @brief 按"该图元是否还参与绘制"设置整行的前景色。
 * @param item 目标行；为 nullptr 时直接返回。
 * @param enabled true 表示图元有效，恢复调色板默认前景色；
 *                false 表示退化或被停用，整行使用灰色。
 * @param disabledColor 灰色，取自 palette().color(QPalette::Disabled, QPalette::Text)。
 * @note  两个方向都要处理：只"设为灰"而不"清回默认"，图元从退化恢复后就会永远
 *        停在灰色上。清回默认的做法是写入一个无效 QVariant，让前景角色消失、
 *        重新由调色板决定；同时只在颜色真的变化时才写，避免整棵树反复重绘。
 */
void applyRowForeground(QTreeWidgetItem *item, bool enabled, const QColor &disabledColor)
{
    if (item == nullptr) {
        return;
    }
    for (int column = 0; column < kColumnCount; ++column) {
        const QVariant current = item->data(column, Qt::ForegroundRole);
        if (enabled) {
            if (current.isValid()) {
                item->setData(column, Qt::ForegroundRole, QVariant());
            }
            continue;
        }
        if (!current.isValid() || current.value<QBrush>().color() != disabledColor) {
            item->setForeground(column, QBrush(disabledColor));
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 构造与析构
// ---------------------------------------------------------------------------

ShapeListPanel::ShapeListPanel(QWidget *parent)
    : QWidget(parent)
    , m_document(nullptr)
    , m_tree(nullptr)
    , m_summaryLabel(nullptr)
    , m_contextMenu(nullptr)
    , m_focusAction(nullptr)
    , m_deleteAction(nullptr)
    , m_refreshAction(nullptr)
    , m_itemsById()
    , m_updatingFromDocument(false)
{
    // 点击面板空白处也能拿到焦点：这样重写的 keyPressEvent 才可能收到 Delete 键
    // （焦点在列表上时按键先到列表，由 eventFilter 兜住，两条路径发同一个信号）。
    setFocusPolicy(Qt::ClickFocus);
    setMinimumWidth(400);

    createSummaryLabel();
    createTree();
    createContextMenu();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_tree);

    // 未绑定文档时也把"共 0 个对象（选中 0 个）"显示出来，保持一致的空状态
    refresh();
}

ShapeListPanel::~ShapeListPanel() = default;

// ---------------------------------------------------------------------------
// 文档绑定与刷新
// ---------------------------------------------------------------------------

void ShapeListPanel::setDocument(GeoDocument *document)
{
    if (m_document == document) {
        refresh(); // 同一个文档重复绑定：不重复连接信号，只刷一次
        return;
    }

    if (m_document != nullptr) {
        // 先断开旧文档的全部连接：不先断开就换绑，旧文档的变动仍会打到本面板，
        // 而且再绑回来时会叠加第二份连接，一次变动刷新两遍。
        disconnect(m_document, nullptr, this, nullptr);
    }

    m_document = document;
    if (m_document != nullptr) {
        connect(m_document, &GeoDocument::documentChanged, this, &ShapeListPanel::refresh);
        connect(m_document, &GeoDocument::selectionChanged, this, &ShapeListPanel::onDocumentSelectionChanged);
        // 文档先于面板销毁时自动解绑，避免留下悬空指针（见 onDocumentDestroyed）
        connect(m_document, &QObject::destroyed, this, &ShapeListPanel::onDocumentDestroyed);
    }

    refresh();
}

GeoDocument *ShapeListPanel::document() const
{
    return m_document;
}

void ShapeListPanel::refresh()
{
    if (m_tree == nullptr || m_summaryLabel == nullptr) {
        return; // 构造尚未完成：不带着半成品状态去访问控件
    }

    // 整个重建过程都置位：删掉一行被选中的图元同样会让列表发出选中变化信号，
    // 若被当成"用户改选"写回文档，就会在文档正在变更的过程中反向改动选中集。
    const ScopedFlag guard(&m_updatingFromDocument);

    // 重建期间临时关闭排序：插入时不必逐行重排（图元多时是 O(n²)），结束后按
    // 用户原来的列与方向排一次，表头的排序指示器保持原样。
    const bool sortingWasEnabled = m_tree->isSortingEnabled();
    const int indicatorSection = m_tree->header()->sortIndicatorSection();
    const int sortColumn = (indicatorSection >= 0) ? indicatorSection : kIdColumn;
    const Qt::SortOrder sortOrder = m_tree->header()->sortIndicatorOrder();
    m_tree->setSortingEnabled(false);

    // 滚动位置：增量刷新本来不会跳，这里显式保存一次，防止重排把视口重置到顶部
    const int scrollValue = m_tree->verticalScrollBar()->value();

    const QList<Shape *> shapes = (m_document != nullptr) ? m_document->shapesInOrder() : QList<Shape *>();
    const QColor disabledColor = palette().color(QPalette::Disabled, QPalette::Text);

    // 1) 新增或更新：以 id 为准复用已有的行，id 只放在 Qt::UserRole 里
    QSet<ShapeId> listedIds;
    for (int i = 0; i < shapes.size(); ++i) {
        const Shape *shape = shapes.at(i);
        if (shape == nullptr) {
            continue; // shapesInOrder() 不该有空洞，真出现也只跳过这一行
        }

        const ShapeId id = shape->id();
        listedIds.insert(id);

        QTreeWidgetItem *item = m_itemsById.value(id, nullptr);
        const bool isNewItem = (item == nullptr);
        if (isNewItem) {
            item = new ShapeTreeItem(QStringList());
            item->setData(kIdColumn, kShapeIdRole, id);
            m_itemsById.insert(id, item);
        }

        // 只写变化过的文本：文本没变就不发数据变更信号，避免整棵树反复重绘
        const QString idText = QStringLiteral("#%1").arg(id);
        if (item->text(kIdColumn) != idText) {
            item->setText(kIdColumn, idText);
        }
        const QString typeText = typeDisplayName(shape);
        if (item->text(kTypeColumn) != typeText) {
            item->setText(kTypeColumn, typeText);
        }
        const QString description = describeShape(shape);
        if (item->text(kDescriptionColumn) != description) {
            item->setText(kDescriptionColumn, description);
            item->setToolTip(kDescriptionColumn, description); // 说明列较长，鼠标停靠看全文
        }

        // 退化或被停用的图元整行置灰（颜色取自调色板，不硬编码）
        applyRowForeground(item, shape->isValid(), disabledColor);

        if (isNewItem) {
            // 内容填好再插入：插入后再逐列赋值会让每一列都触发一次重绘
            m_tree->addTopLevelItem(item); // 所有权移交列表，由列表负责释放
        }
    }

    // 2) 删除文档里已经不存在的行
    QList<ShapeId> staleIds;
    for (QHash<ShapeId, QTreeWidgetItem *>::const_iterator it = m_itemsById.constBegin();
         it != m_itemsById.constEnd();
         ++it) {
        if (!listedIds.contains(it.key())) {
            staleIds.append(it.key());
        }
    }
    for (int i = 0; i < staleIds.size(); ++i) {
        QTreeWidgetItem *item = m_itemsById.take(staleIds.at(i));
        if (item == nullptr) {
            continue;
        }
        // 先 take 再 delete：takeTopLevelItem 交出所有权，避免列表析构时二次释放
        const int index = m_tree->indexOfTopLevelItem(item);
        if (index >= 0) {
            delete m_tree->takeTopLevelItem(index);
        } else {
            delete item;
        }
    }

    // 3) 恢复排序（用户点过表头就保持他的列与方向）
    m_tree->setSortingEnabled(sortingWasEnabled);
    if (sortingWasEnabled) {
        m_tree->header()->setSortIndicator(sortColumn, sortOrder);
        m_tree->sortItems(sortColumn, sortOrder);
    }
    m_tree->verticalScrollBar()->setValue(scrollValue);

    // 4) 选中态以文档为准（同步期间列表信号已被阻断，不会反过来写文档）
    syncTreeSelectionFromDocument();
    updateSummaryLabel();
}

// ---------------------------------------------------------------------------
// 装配
// ---------------------------------------------------------------------------

void ShapeListPanel::createSummaryLabel()
{
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setTextFormat(Qt::PlainText); // 文本由本类拼装，不含富文本标记
    m_summaryLabel->setText(tr("共 0 个对象（选中 0 个）"));
}

void ShapeListPanel::createTree()
{
    m_tree = new QTreeWidget(this);

    QStringList headers;
    headers << tr("编号") << tr("类型") << tr("说明");
    m_tree->setHeaderLabels(headers);

    m_tree->setRootIsDecorated(false);                              // 平铺清单，不需要展开箭头
    m_tree->setUniformRowHeights(true);                             // 图元多时按等高行布局，快很多
    m_tree->setAlternatingRowColors(true);                          // 斑马纹颜色来自调色板
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);    // 整行选中
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection); // 支持 Ctrl / Shift 多选
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);     // 只读清单，禁止就地编辑
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(kIdColumn, Qt::AscendingOrder);
    // 三列都按**内容取自然宽度**，装不下时由横向滚动条兜底。
    //
    // 为什么不用"前两列自适应 + 说明列 stretch"：说明列比剩余空间长得多
    // （"圆弧 圆心 #7, 半径 2.00"），stretch 只会把它压到面板宽度以内，
    // 于是文字被截成省略号——用户看不到半径、宿主这些关键信息。
    // 这类问题断言查不出来，是从整窗截图里看出来的。
    // 反过来也不能让三列把侧栏撑得很宽：那会挤压画布，得不偿失。
    m_tree->header()->setSectionResizeMode(kIdColumn, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kTypeColumn, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(kDescriptionColumn, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);

    // 右键菜单：菜单项在 onContextMenuRequested() 里按上下文启用/禁用
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ShapeListPanel::onTreeSelectionChanged);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &ShapeListPanel::onTreeItemDoubleClicked);
    connect(m_tree,
            &QTreeWidget::customContextMenuRequested,
            this,
            &ShapeListPanel::onContextMenuRequested);

    // Delete 键：焦点在列表上时按键事件先到列表、不会到面板，所以除了重写
    // keyPressEvent() 之外再装一个事件过滤器，两条路径发出的都是 deleteRequested()。
    m_tree->installEventFilter(this);
    m_tree->viewport()->installEventFilter(this);
}

void ShapeListPanel::createContextMenu()
{
    m_contextMenu = new QMenu(this);

    m_focusAction = new QAction(tr("聚焦到此对象"), this);
    m_deleteAction = new QAction(tr("删除选中的对象"), this);
    m_refreshAction = new QAction(tr("刷新列表"), this);

    // 刻意**不**给"删除"设置 Delete 快捷键：画布已经用 Delete 键请求删除，
    // 两处快捷键同时生效会让同一次按键发出两次删除请求（删除不可重复执行）。
    m_contextMenu->addAction(m_focusAction);
    m_contextMenu->addAction(m_deleteAction);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_refreshAction);

    connect(m_deleteAction, &QAction::triggered, this, &ShapeListPanel::deleteRequested);
    connect(m_refreshAction, &QAction::triggered, this, &ShapeListPanel::refresh);
    // 聚焦动作要带上"点中的是哪一行"，因此在 onContextMenuRequested() 里按
    // exec() 的返回值处理，而不是在这里直接连信号。
}

// ---------------------------------------------------------------------------
// 选中集双向同步
// ---------------------------------------------------------------------------

void ShapeListPanel::onTreeSelectionChanged()
{
    if (m_updatingFromDocument) {
        // 由文档同步（或刷新）引起的选中变化，不是用户操作，直接忽略。
        // 这一条是防递归的第一道闸门。
        return;
    }
    writeSelectionToDocument();
}

void ShapeListPanel::onDocumentSelectionChanged()
{
    if (m_updatingFromDocument) {
        // 自己刚写进文档的选中集又回调回来了：忽略，避免"列表 ↔ 文档"往复。
        return;
    }
    syncTreeSelectionFromDocument();
    updateSummaryLabel();
}

void ShapeListPanel::syncTreeSelectionFromDocument()
{
    if (m_tree == nullptr) {
        return;
    }

    // 双重保险：
    // - ScopedFlag 让回调 onDocumentSelectionChanged()/onTreeSelectionChanged()
    //   认出"这是程序在同步"；
    // - QSignalBlocker 直接掐掉列表的 itemSelectionChanged()：即使将来有人改坏了
    //   标志判断，也不可能形成"列表 → 文档 → 列表"的往复触发。
    const ScopedFlag guard(&m_updatingFromDocument);
    const QSignalBlocker blocker(m_tree);

    for (QHash<ShapeId, QTreeWidgetItem *>::const_iterator it = m_itemsById.constBegin();
         it != m_itemsById.constEnd();
         ++it) {
        const bool selected = (m_document != nullptr) && m_document->isSelected(it.key());
        it.value()->setSelected(selected);
    }
}

void ShapeListPanel::writeSelectionToDocument()
{
    if (m_document == nullptr || m_tree == nullptr) {
        return;
    }

    QSet<ShapeId> ids;
    const QList<QTreeWidgetItem *> selectedItems = m_tree->selectedItems();
    for (int i = 0; i < selectedItems.size(); ++i) {
        const ShapeId id = shapeIdOf(selectedItems.at(i));
        if (id != kInvalidShapeId) {
            ids.insert(id);
        }
    }

    {
        // 写文档期间置位：GeoDocument::setSelection 会**同步**发出 selectionChanged，
        // 那条回调若再回来重建列表选中态，就形成"列表 → 文档 → 列表"的往复。
        // 这是防递归的第二道闸门。
        const ScopedFlag guard(&m_updatingFromDocument);
        m_document->setSelection(ids);
    }

    // 文档会剔除不存在的 id，所以再以文档为准收口一次；此时列表信号已被阻断，
    // 不会再回调 onTreeSelectionChanged()。
    syncTreeSelectionFromDocument();
    updateSummaryLabel();

    emit selectionChanged(m_document->selection());
}

void ShapeListPanel::selectOnlyItem(QTreeWidgetItem *item)
{
    if (item == nullptr || m_tree == nullptr) {
        return;
    }

    {
        const ScopedFlag guard(&m_updatingFromDocument);
        const QSignalBlocker blocker(m_tree);
        m_tree->clearSelection();
        item->setSelected(true);
        m_tree->setCurrentItem(item);
    }

    // 收敛完成后只写一次文档：否则"清空选中"与"选中该行"会各写一遍选中集，
    // 上层会收到两次内容相同的 selectionChanged。
    writeSelectionToDocument();
}

// ---------------------------------------------------------------------------
// 交互
// ---------------------------------------------------------------------------

void ShapeListPanel::onTreeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column) // 双击整行都算，列号不参与判断

    const ShapeId id = shapeIdOf(item);
    if (id != kInvalidShapeId) {
        emit focusRequested(id);
    }
}

void ShapeListPanel::onContextMenuRequested(const QPoint &position)
{
    if (m_tree == nullptr || m_contextMenu == nullptr) {
        return;
    }

    QTreeWidgetItem *item = m_tree->itemAt(position); // position 是列表视口坐标

    // 右键点在未选中的行上：先把选中集收敛到这一行（与常见列表的习惯一致），
    // 否则"删除选中的对象"会删掉一批用户当前并没有在看的东西。
    if (item != nullptr && !item->isSelected()) {
        selectOnlyItem(item);
    } else if (item != nullptr) {
        m_tree->setCurrentItem(item);
    }

    const ShapeId contextId = shapeIdOf(item);
    m_focusAction->setEnabled(contextId != kInvalidShapeId);
    m_deleteAction->setEnabled(!m_tree->selectedItems().isEmpty());

    QAction *chosen = m_contextMenu->exec(m_tree->viewport()->mapToGlobal(position));
    if (chosen == m_focusAction && contextId != kInvalidShapeId) {
        emit focusRequested(contextId);
    }
}

void ShapeListPanel::onDocumentDestroyed(QObject *object)
{
    if (object != m_document) {
        return; // 只关心当前绑定的那个文档
    }
    // 文档析构时 QObject 已经断开了与本文档的全部连接，此处只需放弃指针；
    // 先清指针再刷新，refresh() 就会走"无文档"分支，不会碰到将死的对象。
    m_document = nullptr;
    refresh();
}

QSize ShapeListPanel::sizeHint() const
{
    // 440 × 420：宽度是按"编号 + 类型 + 说明"三列的自然宽度估的，
    // 高度足够显示十来行而不必立刻滚动。QDockWidget 会用它作为初始停靠宽度。
    return QSize(440, 420);
}

void ShapeListPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) {
        // 只发请求，绝不在这里删图元：删除要进撤销栈，属于 controller 的职责。
        emit deleteRequested();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool ShapeListPanel::eventFilter(QObject *watched, QEvent *event)
{
    const bool isTreeEvent = (watched == m_tree) || (m_tree != nullptr && watched == m_tree->viewport());
    if (isTreeEvent && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Delete) {
            emit deleteRequested();
            return true; // 已处理，不再交给列表，免得它另外解释这次按键
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// 文本与辅助
// ---------------------------------------------------------------------------

QString ShapeListPanel::typeDisplayName(const Shape *shape)
{
    if (shape == nullptr) {
        return QString();
    }

    // 刻意不写 default 分支：将来给 ShapeType 增加取值时，编译器会在这里
    // 提醒"枚举未完全处理"，而不是让新类型在界面上显示成空白。
    switch (shape->type()) {
    case ShapeType::Point:
        return tr("点");
    case ShapeType::Line: {
        const LineShape *line = static_cast<const LineShape *>(shape);
        switch (line->lineKind()) {
        case LineKind::Segment:
            return tr("线段");
        case LineKind::Ray:
            return tr("射线");
        case LineKind::InfiniteLine:
            return tr("直线");
        }
        return tr("直线");
    }
    case ShapeType::Circle:
        return tr("圆");
    case ShapeType::Arc:
        return tr("圆弧");
    case ShapeType::Polygon:
        return tr("多边形");
    case ShapeType::PerpendicularLine:
        return tr("垂线");
    case ShapeType::ParallelLine:
        return tr("平行线");
    case ShapeType::AngleBisector:
        return tr("角平分线");
    case ShapeType::MidPoint:
        return tr("中点");
    case ShapeType::IntersectionPoint:
        return tr("交点");
    case ShapeType::Measurement:
        return tr("标注");
    }
    return QString();
}

QString ShapeListPanel::describeShape(const Shape *shape)
{
    if (shape == nullptr) {
        return QString();
    }

    QString text;
    switch (shape->type()) {
    case ShapeType::Point: {
        const PointShape *point = static_cast<const PointShape *>(shape);
        text = formatPoint(point->position());
        if (point->kind() == PointKind::OnObject) {
            const ShapeId hostId = point->hostId();
            text += (hostId != kInvalidShapeId) ? tr("，在 #%1 上").arg(hostId) : tr("，在曲线上");
        } else {
            text += tr("，自由");
        }
        break;
    }
    case ShapeType::Line: {
        const LineShape *line = static_cast<const LineShape *>(shape);
        switch (line->lineKind()) {
        case LineKind::Segment:
            text = tr("端点 #%1、#%2").arg(line->firstPointId()).arg(line->secondPointId());
            break;
        case LineKind::Ray:
            text = tr("端点 #%1，方向点 #%2").arg(line->firstPointId()).arg(line->secondPointId());
            break;
        case LineKind::InfiniteLine:
            text = tr("过 #%1、#%2").arg(line->firstPointId()).arg(line->secondPointId());
            break;
        }
        break;
    }
    case ShapeType::Circle: {
        const CircleShape *circle = static_cast<const CircleShape *>(shape);
        text = tr("圆心 #%1，半径 %2")
                   .arg(circle->centerPointId())
                   .arg(formatNumber(circle->circleData().radius));
        break;
    }
    case ShapeType::Arc: {
        const ArcShape *arc = static_cast<const ArcShape *>(shape);
        if (arc->kind() == ArcKind::ThreePoint) {
            text = tr("三点（#%1、#%2、#%3），半径 %4")
                       .arg(arc->startPointId())
                       .arg(arc->middlePointId())
                       .arg(arc->endPointId())
                       .arg(formatNumber(arc->arcData().radius));
        } else {
            // 圆心式：startPointId 是圆心，endPointId 是半径点，middlePointId 是弧上点
            text = tr("圆心 #%1，半径点 #%2，半径 %3")
                       .arg(arc->startPointId())
                       .arg(arc->endPointId())
                       .arg(formatNumber(arc->arcData().radius));
        }
        break;
    }
    case ShapeType::Polygon: {
        const PolygonShape *polygon = static_cast<const PolygonShape *>(shape);
        text = tr("%1 个顶点：%2")
                   .arg(polygon->vertexPointIds().size())
                   .arg(formatIdList(polygon->vertexPointIds()));
        break;
    }
    case ShapeType::PerpendicularLine:
    case ShapeType::ParallelLine: {
        const LineShape *line = static_cast<const LineShape *>(shape);
        const QString relation =
            (shape->type() == ShapeType::PerpendicularLine) ? tr("垂直于") : tr("平行于");
        // 派生线：secondPointId 是通过点，baseCurveId 是基准曲线
        text = tr("过 #%1，%2 #%3")
                   .arg(line->secondPointId())
                   .arg(relation)
                   .arg(line->baseCurveId());
        break;
    }
    case ShapeType::AngleBisector: {
        const LineShape *line = static_cast<const LineShape *>(shape);
        // 第二边上的点没有公开取值函数，但它是本图元的父图元之一，且 LineShape
        // 的构造顺序保证 parents() = [顶点, 第一边上点, 第二边上点]。
        const QList<ShapeId> &parentIds = shape->parents();
        const ShapeId secondRayPointId = (parentIds.size() >= 3) ? parentIds.at(2) : kInvalidShapeId;
        text = tr("顶点 #%1，两边过 #%2、#%3")
                   .arg(line->firstPointId())
                   .arg(line->secondPointId())
                   .arg(secondRayPointId);
        break;
    }
    case ShapeType::MidPoint: {
        const MidPointShape *midpoint = static_cast<const MidPointShape *>(shape);
        text = tr("#%1 与 #%2 的中点，%3")
                   .arg(midpoint->firstPointId())
                   .arg(midpoint->secondPointId())
                   .arg(formatPoint(midpoint->position()));
        break;
    }
    case ShapeType::IntersectionPoint: {
        const IntersectionPointShape *intersection = static_cast<const IntersectionPointShape *>(shape);
        text = tr("#%1 与 #%2 的第 %3 个交点，%4")
                   .arg(intersection->firstCurveId())
                   .arg(intersection->secondCurveId())
                   .arg(intersection->index() + 1)
                   .arg(formatPoint(intersection->position()));
        break;
    }
    case ShapeType::Measurement: {
        const MeasurementShape *measurement = static_cast<const MeasurementShape *>(shape);
        text = tr("%1：%2")
                   .arg(measurementKindName(measurement->kind()))
                   .arg(measurement->displayText());
        break;
    }
    }

    // 退化与停用是两种不同的"不参与绘制"，分别注明原因；
    // 整行置灰由 refresh() 依据 Shape::isValid() 统一处理。
    if (!shape->isGeometryValid()) {
        text += tr("（失效）");
    }
    if (!shape->isEnabled()) {
        text += tr("（已停用）");
    }
    return text;
}

QString ShapeListPanel::measurementKindName(MeasurementKind kind)
{
    switch (kind) {
    case MeasurementKind::Distance:
        return tr("距离");
    case MeasurementKind::SegmentLength:
        return tr("线段长");
    case MeasurementKind::Angle:
        return tr("角度");
    case MeasurementKind::Radius:
        return tr("半径");
    case MeasurementKind::Circumference:
        return tr("周长");
    case MeasurementKind::Area:
        return tr("面积");
    case MeasurementKind::PointCoordinates:
        return tr("坐标");
    }
    return QString();
}

ShapeId ShapeListPanel::shapeIdOf(const QTreeWidgetItem *item)
{
    if (item == nullptr) {
        return kInvalidShapeId;
    }
    // 只认自定义角色里的 id：列表可排序、可增删，行号与 id 的对应关系随时会变
    return item->data(kIdColumn, kShapeIdRole).toInt();
}

void ShapeListPanel::updateSummaryLabel()
{
    if (m_summaryLabel == nullptr) {
        return;
    }
    const int total = m_itemsById.size();
    const int selectedCount = (m_document != nullptr) ? m_document->selection().size() : 0;
    m_summaryLabel->setText(tr("共 %1 个对象（选中 %2 个）").arg(total).arg(selectedCount));
}
