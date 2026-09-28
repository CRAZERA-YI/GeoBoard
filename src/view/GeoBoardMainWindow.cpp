/**
 * @file    view/GeoBoardMainWindow.cpp
 * @brief   主窗口的实现。
 * @details 界面全部用 C++ 代码装配（本 skill 禁止 `.ui`）：构造函数只做编排，
 *          动作、菜单、工具栏、状态栏、中央部件各自拆成一个 createXxx()。
 *
 *          样式遵循 templates/qt/ui_defaults.md 的经典 Windows 风格：布局用
 *          QHBoxLayout/QVBoxLayout，不指定字体族与字号，不硬编码颜色（取
 *          palette()），不设置 Fusion 风格，工具栏用文字按钮而非自绘图标。
 */

#include "GeoBoardMainWindow.h"

#include "CanvasWidget.h"
#include "ShapeListPanel.h"
#include "version.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

namespace {

/** @brief 本程序的文件扩展名（不含点）。 */
const char *const kFileExtension = "gboard";

/** @brief 文件对话框的过滤器。 */
QString fileDialogFilter()
{
    return QCoreApplication::translate("GeoBoardMainWindow",
                                       "GeoBoard 图形 (*.gboard);;所有文件 (*.*)");
}

} // namespace

// ---------------------------------------------------------------------------
// 构造与析构
// ---------------------------------------------------------------------------

GeoBoardMainWindow::GeoBoardMainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_canvas(nullptr)
    , m_toolBar(nullptr)
    , m_toolGroup(nullptr)
    , m_shapeListPanel(nullptr)
    , m_shapeListDock(nullptr)
    , m_coordinateLabel(nullptr)
    , m_zoomLabel(nullptr)
    , m_fileName()
    , m_newAction(nullptr)
    , m_openAction(nullptr)
    , m_saveAction(nullptr)
    , m_saveAsAction(nullptr)
    , m_exitAction(nullptr)
    , m_undoAction(nullptr)
    , m_redoAction(nullptr)
    , m_gridAction(nullptr)
    , m_axesAction(nullptr)
    , m_labelsAction(nullptr)
    , m_zoomInAction(nullptr)
    , m_zoomOutAction(nullptr)
    , m_zoomResetAction(nullptr)
    , m_fitContentAction(nullptr)
    , m_snapAction(nullptr)
    , m_shapeListAction(nullptr)
    , m_gridPrecisionGroup(nullptr)
    , m_gridPrecisionMenu(nullptr)
    , m_exportAction(nullptr)
    , m_aboutAction(nullptr)
{
    // [*] 是 Qt 的"未保存标记"占位符，由 setWindowModified() 控制它的显示
    setWindowTitle(tr("[*]未命名 — GeoBoard"));
    setWindowModified(false);
    resize(1024, 720);

    createActions();
    createCentralWidget(); // 先建画布：状态栏的坐标标签要连它的信号
    createMenus();
    createToolBar();
    createShapeListDock(); // 对象列表面板（可停靠、可关闭）
    createStatusBar();

    setActiveTool(ToolId::Select);
}

GeoBoardMainWindow::~GeoBoardMainWindow() = default;

// ---------------------------------------------------------------------------
// 装配
// ---------------------------------------------------------------------------

void GeoBoardMainWindow::createCentralWidget()
{
    m_canvas = new CanvasWidget(this);

    // 边距设为 0：画布应当铺满中央区域，默认的 9px 边距会浪费作图空间
    QVBoxLayout *layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_canvas);

    QWidget *central = new QWidget(this);
    central->setLayout(layout);
    setCentralWidget(central);
}

void GeoBoardMainWindow::createShapeListDock()
{
    m_shapeListPanel = new ShapeListPanel(this);

    m_shapeListDock = new QDockWidget(tr("对象列表"), this);
    m_shapeListDock->setObjectName(QStringLiteral("shapeListDock"));
    m_shapeListDock->setWidget(m_shapeListPanel);
    // 允许左右停靠并浮动：用户可以把清单拖到顺手的一侧，或拖出来当独立窗口
    m_shapeListDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_shapeListDock->setFeatures(QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable
                                 | QDockWidget::DockWidgetClosable);
    m_shapeListDock->setStatusTip(tr("列出全部对象，可选中、聚焦、删除"));
    addDockWidget(Qt::RightDockWidgetArea, m_shapeListDock);

    // 给一个够用的默认宽度：面板挤成一条缝时，说明列会被截断成省略号，
    // 而"说明"恰恰是这个面板最有价值的一列（半径、宿主、坐标都在里面）。
    resizeDocks(QList<QDockWidget *>() << m_shapeListDock, QList<int>() << 340,
                Qt::Horizontal);

    // 停靠窗口自带"关闭"按钮，关闭后要能从视图菜单再打开：把菜单动作与它绑定
    if (m_shapeListAction != nullptr) {
        m_shapeListAction->setChecked(true);
        connect(m_shapeListAction, &QAction::toggled, m_shapeListDock, &QWidget::setVisible);
        connect(m_shapeListDock, &QDockWidget::visibilityChanged, m_shapeListAction,
                &QAction::setChecked);
    }
}

void GeoBoardMainWindow::setShapeListVisible(bool visible)
{
    if (m_shapeListDock != nullptr) {
        m_shapeListDock->setVisible(visible);
    }
}

bool GeoBoardMainWindow::isShapeListVisible() const
{
    return m_shapeListDock != nullptr && m_shapeListDock->isVisible();
}

void GeoBoardMainWindow::focusOnShape(ShapeId id)
{
    if (m_canvas == nullptr || m_canvas->document() == nullptr) {
        return;
    }
    const Shape *shape = m_canvas->document()->shape(id);
    if (shape == nullptr) {
        return;
    }

    // 用**该图元的父点位置**求包围盒。刻意不引入 Shape::boundingBox()：
    // 那需要为 11 种图元各写一套（圆要算圆心±半径、弧要算端点的三角值），
    // 而对"把视图挪到能看见这个对象"这个用途来说，父点的范围已经足够——
    // 它们本来就把图元撑起来了。圆的半径仍由一个父点表达，因此也落在框内。
    Vec2 minCorner;
    Vec2 maxCorner;
    bool hasBounds = false;
    const QList<ShapeId> parents = shape->parents();
    for (int i = 0; i < parents.size(); ++i) {
        const PointShape *point = m_canvas->document()->point(parents.at(i));
        if (point == nullptr) {
            continue;
        }
        const Vec2 position = point->position();
        if (!hasBounds) {
            minCorner = position;
            maxCorner = position;
            hasBounds = true;
        } else {
            minCorner.x = qMin(minCorner.x, position.x);
            minCorner.y = qMin(minCorner.y, position.y);
            maxCorner.x = qMax(maxCorner.x, position.x);
            maxCorner.y = qMax(maxCorner.y, position.y);
        }
    }

    if (!hasBounds) {
        // 没有可用的父点（例如点自身无父）：至少把视图居中到它，别让用户以为没反应
        const PointShape *self = m_canvas->document()->point(id);
        if (self != nullptr) {
            m_canvas->camera()->centerOn(self->position());
            m_canvas->update();
        }
        return;
    }

    // 只有一个父点时包围盒退化为一个点，fitToBounds 会缩放到极限；
    // 给它人为撑开一点范围，效果就是"以该点为中心、保持一个可读的缩放级别"
    const double spanX = maxCorner.x - minCorner.x;
    const double spanY = maxCorner.y - minCorner.y;
    const double minimumSpan = 0.5;
    if (spanX < minimumSpan) {
        minCorner.x -= (minimumSpan - spanX) * 0.5;
        maxCorner.x += (minimumSpan - spanX) * 0.5;
    }
    if (spanY < minimumSpan) {
        minCorner.y -= (minimumSpan - spanY) * 0.5;
        maxCorner.y += (minimumSpan - spanY) * 0.5;
    }

    m_canvas->camera()->fitToBounds(minCorner, maxCorner, 0.35);
    m_canvas->update();
}

void GeoBoardMainWindow::createGridPrecisionMenu()
{
    // "坐标轴最小精度"做成互斥单选项：它决定网格与吸附的最小颗粒度，
    // 用户在"1 / 2 / 5 / 10 每格"之间选一个即可，不必输入数值。
    // 说明写进 statusTip —— 这几个数字对不熟悉的人并不自明。
    m_gridPrecisionMenu = new QMenu(tr("坐标轴最小精度(&G)"), this);
    // 起个对象名：测试据它定位这个子菜单，而不依赖界面文案。
    // 文案以后可能改（或需要翻译），对象名是稳定的契约。
    m_gridPrecisionMenu->setObjectName(QStringLiteral("gridPrecisionMenu"));
    m_gridPrecisionGroup = new QActionGroup(this);
    m_gridPrecisionGroup->setExclusive(true);

    const double options[] = { 1.0, 2.0, 5.0, 10.0 };
    for (int i = 0; i < 4; ++i) {
        const double step = options[i];
        QAction *action = new QAction(tr("%1 每格").arg(step, 0, 'g', 2), this);
        action->setCheckable(true);
        action->setChecked(i == 0); // 默认"1 每格"，与 Camera 的默认最小精度一致
        action->setStatusTip(tr("网格与吸附的最小颗粒度为每格 %1 个场景单位；"
                                "调大它会让网格更粗、吸附步进更大")
                                 .arg(step, 0, 'g', 2));
        action->setData(step);
        m_gridPrecisionGroup->addAction(action);
        m_gridPrecisionMenu->addAction(action);
    }
    connect(m_gridPrecisionGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        emit gridPrecisionRequested(action->data().toDouble());
    });
}

void GeoBoardMainWindow::createStatusBar()
{
    // 坐标用等宽字体：数值跳动时宽度稳定，不会让状态栏文字左右抖动
    QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    m_coordinateLabel = new QLabel(tr("x = 0.00, y = 0.00"), this);
    m_coordinateLabel->setFont(fixedFont);
    m_coordinateLabel->setMinimumWidth(180);

    // 选中数量：需求要求界面能显示当前选中的对象数量，
    // 放在状态栏右侧与坐标、缩放并列，避免占用画布空间
    m_selectionLabel = new QLabel(tr("未选中"), this);
    m_selectionLabel->setMinimumWidth(72);
    m_selectionLabel->setToolTip(tr("当前选中的图元数量（用选择工具点击图元，Ctrl 点击可多选）"));

    m_zoomLabel = new QLabel(tr("100%"), this);
    m_zoomLabel->setFont(fixedFont);
    m_zoomLabel->setMinimumWidth(56);
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    statusBar()->addPermanentWidget(m_selectionLabel);
    statusBar()->addPermanentWidget(m_coordinateLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
}

QAction *GeoBoardMainWindow::createToolAction(ToolId tool, const QString &shortcut)
{
    QAction *action = new QAction(toolName(tool), this);
    action->setCheckable(true);
    action->setToolTip(QStringLiteral("%1：%2").arg(toolName(tool), toolHint(tool)));
    action->setStatusTip(toolHint(tool));
    // QAction 没有 setAccessibleName（那是 QWidget 的接口），QAction 的无障碍名称
    // 由文本与提示共同提供，因此这里不再重复设置属性
    action->setData(static_cast<int>(tool));
    if (!shortcut.isEmpty()) {
        action->setShortcut(QKeySequence(shortcut));
    }
    m_toolGroup->addAction(action);
    return action;
}

void GeoBoardMainWindow::createActions()
{
    m_newAction = new QAction(tr("新建(&N)"), this);
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setStatusTip(tr("清空画布，开始一个新的作图"));
    connect(m_newAction, &QAction::triggered, this, [this]() { emit newRequested(); });

    m_openAction = new QAction(tr("打开(&O)…"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(tr("打开一个已保存的 .gboard 图形文件"));
    connect(m_openAction, &QAction::triggered, this, [this]() { emit openRequested(QString()); });

    m_saveAction = new QAction(tr("保存(&S)"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setStatusTip(tr("保存当前作图为 .gboard 文件"));
    connect(m_saveAction, &QAction::triggered, this, [this]() { emit saveRequested(QString()); });

    m_saveAsAction = new QAction(tr("另存为(&A)…"), this);
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    m_saveAsAction->setStatusTip(tr("把当前作图另存为一个新文件"));
    connect(m_saveAsAction, &QAction::triggered, this, [this]() {
        emit saveRequested(promptSavePath(m_fileName));
    });

    m_exitAction = new QAction(tr("退出(&X)"), this);
    m_exitAction->setShortcut(QKeySequence::Quit);
    m_exitAction->setStatusTip(tr("退出 GeoBoard"));
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = new QAction(tr("撤销(&U)"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setStatusTip(tr("撤销上一步操作"));
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, [this]() { emit undoRequested(); });

    m_redoAction = new QAction(tr("重做(&R)"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setStatusTip(tr("重做被撤销的操作"));
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, [this]() { emit redoRequested(); });

    m_gridAction = new QAction(tr("显示网格(&G)"), this);
    m_gridAction->setCheckable(true);
    m_gridAction->setChecked(true);
    m_gridAction->setStatusTip(tr("显示或隐藏背景网格"));
    connect(m_gridAction, &QAction::toggled, this, [this](bool checked) {
        emit gridVisibilityToggled(checked);
    });

    m_axesAction = new QAction(tr("显示坐标轴(&A)"), this);
    m_axesAction->setCheckable(true);
    m_axesAction->setChecked(true);
    m_axesAction->setStatusTip(tr("显示或隐藏坐标轴"));
    connect(m_axesAction, &QAction::toggled, this, [this](bool checked) {
        emit axesVisibilityToggled(checked);
    });

    m_labelsAction = new QAction(tr("显示点名与标注(&L)"), this);
    m_labelsAction->setCheckable(true);
    m_labelsAction->setChecked(true);
    m_labelsAction->setStatusTip(tr("显示或隐藏点名、角度与长度标注"));
    connect(m_labelsAction, &QAction::toggled, this, [this](bool checked) {
        emit labelsVisibilityToggled(checked);
    });

    m_shapeListAction = new QAction(tr("对象列表(&O)"), this);
    m_shapeListAction->setCheckable(true);
    m_shapeListAction->setChecked(true);
    m_shapeListAction->setStatusTip(
        tr("显示或隐藏对象列表停靠窗口（列出全部对象，可选中、聚焦、删除）"));

    m_snapAction = new QAction(tr("吸附对齐(&S)"), this);
    m_snapAction->setCheckable(true);
    m_snapAction->setChecked(true);
    m_snapAction->setStatusTip(tr("作图输入自动吸附到栅格、已有点或曲线上（点落在曲线上会成为受约束的附着点）"));
    connect(m_snapAction, &QAction::toggled, this, [this](bool checked) {
        emit snapToggled(checked);
    });

    createGridPrecisionMenu();

    m_exportAction = new QAction(tr("导出为图片(&E)…"), this);
    m_exportAction->setStatusTip(tr("把当前作图导出为 PNG 或 SVG 文件"));
    connect(m_exportAction, &QAction::triggered, this, [this]() {
        emit exportRequested(QString());
    });

    m_zoomInAction = new QAction(tr("放大(&I)"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAction->setStatusTip(tr("放大视图"));
    connect(m_zoomInAction, &QAction::triggered, this, [this]() { emit zoomInRequested(); });

    m_zoomOutAction = new QAction(tr("缩小(&O)"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAction->setStatusTip(tr("缩小视图"));
    connect(m_zoomOutAction, &QAction::triggered, this, [this]() { emit zoomOutRequested(); });

    m_zoomResetAction = new QAction(tr("恢复默认视图(&R)"), this);
    m_zoomResetAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    m_zoomResetAction->setStatusTip(tr("回到原点居中、100% 缩放的视图"));
    connect(m_zoomResetAction, &QAction::triggered, this, [this]() { emit zoomResetRequested(); });

    m_fitContentAction = new QAction(tr("缩放到全部内容(&F)"), this);
    m_fitContentAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    m_fitContentAction->setStatusTip(tr("调整缩放与位置，使全部图形可见"));
    connect(m_fitContentAction, &QAction::triggered, this, [this]() { emit fitContentRequested(); });

    m_aboutAction = new QAction(tr("关于 GeoBoard(&A)"), this);
    m_aboutAction->setStatusTip(tr("显示版本与说明"));
    connect(m_aboutAction, &QAction::triggered, this, [this]() { emit aboutRequested(); });
}

void GeoBoardMainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_saveAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();

    // 删除动作刻意**不设**快捷键：画布已在自己的 keyPressEvent 里处理 Delete 键，
    // 两边都设会触发 Qt 的"快捷键歧义"（Ambiguous shortcut overload）并让按键失效
    QAction *deleteAction = new QAction(tr("删除选中图元(&D)"), this);
    deleteAction->setStatusTip(tr("删除当前选中的图元（依赖它们的图元会一并删除，可撤销）"));
    connect(deleteAction, &QAction::triggered, m_canvas, &CanvasWidget::deleteRequested);
    editMenu->addAction(deleteAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(m_gridAction);
    viewMenu->addAction(m_axesAction);
    viewMenu->addAction(m_labelsAction);
    viewMenu->addAction(m_snapAction);
    viewMenu->addMenu(m_gridPrecisionMenu);
    viewMenu->addSeparator();
    viewMenu->addAction(m_shapeListAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addAction(m_zoomResetAction);
    viewMenu->addAction(m_fitContentAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(m_aboutAction);
}

void GeoBoardMainWindow::createToolBar()
{
    m_toolBar = addToolBar(tr("作图工具"));
    m_toolBar->setObjectName(QStringLiteral("toolBarDraw"));
    m_toolBar->setMovable(false);
    // 用文字按钮而不是自绘图标：本 skill 不引入图标资源，文字按钮在中文界面下
    // 反而更易读，也天然满足"图标按钮必须有 toolTip 与 accessibleName"的要求
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // 第二行放测量工具。**这不是美观问题而是可用性问题**：22 个文字按钮在常见的
    // 1600px 窗口下就已经排不下，而 QToolBar 在 setMovable(false) 时不会显示溢出
    // 按钮，被挤到可见区之外的测量工具就此无法点击（实测截图发现的缺陷）。
    // 拆成两行后每个工具都恒定可见。
    // 注意 addToolBarBreak() 必须在 addToolBar() **之前**调用：它插入的是"下一个
    // 工具栏另起一行"的分隔，放在后面会把分隔加到测量栏之后，效果就没了。
    addToolBarBreak();
    QToolBar *measureBar = addToolBar(tr("测量工具"));
    measureBar->setObjectName(QStringLiteral("toolBarMeasure"));
    measureBar->setMovable(false);
    measureBar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    connect(m_toolGroup, &QActionGroup::triggered, this, &GeoBoardMainWindow::onToolActionTriggered);

    // 分组顺序即用户的学习顺序：选择 → 基础作图 → 派生约束 → 测量
    m_toolBar->addAction(createToolAction(ToolId::Select, QStringLiteral("Esc")));
    m_toolBar->addSeparator();
    m_toolBar->addAction(createToolAction(ToolId::Point, QStringLiteral("P")));
    m_toolBar->addAction(createToolAction(ToolId::PointOnObject, QString()));
    m_toolBar->addAction(createToolAction(ToolId::Segment, QStringLiteral("S")));
    m_toolBar->addAction(createToolAction(ToolId::Ray, QString()));
    m_toolBar->addAction(createToolAction(ToolId::InfiniteLine, QString()));
    m_toolBar->addAction(createToolAction(ToolId::Circle, QStringLiteral("C")));
    m_toolBar->addAction(createToolAction(ToolId::Arc, QString()));
    m_toolBar->addAction(createToolAction(ToolId::ArcCenter, QString()));
    m_toolBar->addAction(createToolAction(ToolId::Polygon, QString()));
    m_toolBar->addSeparator();
    m_toolBar->addAction(createToolAction(ToolId::MidPoint, QStringLiteral("M")));
    m_toolBar->addAction(createToolAction(ToolId::Perpendicular, QString()));
    m_toolBar->addAction(createToolAction(ToolId::Parallel, QString()));
    m_toolBar->addAction(createToolAction(ToolId::AngleBisector, QString()));
    m_toolBar->addAction(createToolAction(ToolId::Intersection, QStringLiteral("I")));

    measureBar->addAction(createToolAction(ToolId::MeasureDistance, QString()));
    measureBar->addAction(createToolAction(ToolId::MeasureLength, QString()));
    measureBar->addAction(createToolAction(ToolId::MeasureAngle, QString()));
    measureBar->addSeparator();
    measureBar->addAction(createToolAction(ToolId::MeasureRadius, QString()));
    measureBar->addAction(createToolAction(ToolId::MeasureCircumference, QString()));
    measureBar->addAction(createToolAction(ToolId::MeasureArea, QString()));
    measureBar->addAction(createToolAction(ToolId::MeasureCircleArea, QString()));
    measureBar->addAction(createToolAction(ToolId::MeasureCoordinates, QString()));

    // 画布的坐标信号驱动状态栏显示（视图内部的展示细节，不必经过控制器）
    const bool connected = connect(m_canvas, &CanvasWidget::mouseScenePosChanged,
                                  this, &GeoBoardMainWindow::onMouseScenePosChanged);
    Q_ASSERT(connected);
    Q_UNUSED(connected);
}

// ---------------------------------------------------------------------------
// 槽与对外方法
// ---------------------------------------------------------------------------

void GeoBoardMainWindow::onToolActionTriggered(QAction *action)
{
    if (action == nullptr) {
        return;
    }
    emit toolChangeRequested(static_cast<ToolId>(action->data().toInt()));
}

void GeoBoardMainWindow::onMouseScenePosChanged(const QPointF &scenePos)
{
    m_coordinateLabel->setText(
        tr("x = %1, y = %2").arg(scenePos.x(), 0, 'f', 2).arg(scenePos.y(), 0, 'f', 2));
}

void GeoBoardMainWindow::setActiveTool(ToolId tool)
{
    if (m_toolGroup == nullptr) {
        return;
    }
    const QList<QAction *> actions = m_toolGroup->actions();
    for (int i = 0; i < actions.size(); ++i) {
        QAction *action = actions.at(i);
        if (static_cast<ToolId>(action->data().toInt()) == tool) {
            action->setChecked(true);
            return;
        }
    }
}

void GeoBoardMainWindow::setPreview(const QList<QPointF> &points, const QPointF &hint, bool showCircle)
{
    m_canvas->setPreview(points, hint, showCircle);
}

void GeoBoardMainWindow::clearPreview()
{
    m_canvas->clearPreview();
}

void GeoBoardMainWindow::setStatusMessage(const QString &message, int timeoutMs)
{
    statusBar()->showMessage(message, timeoutMs);
}

QString GeoBoardMainWindow::statusMessage() const
{
    // 读控件里的**真实**文本而不是缓存"最后一次设置的字符串"：
    // showMessage 带超时，过期后状态栏里已经没有这条提示了，
    // 用缓存会让测试拿到用户其实看不到的陈旧文本（那样断言就没有意义）。
    return statusBar()->currentMessage();
}
void GeoBoardMainWindow::setMouseStatus(const QString &text)
{
    m_coordinateLabel->setText(text);
}

void GeoBoardMainWindow::setModified(bool modified)
{
    setWindowModified(modified);
}

void GeoBoardMainWindow::setFileName(const QString &fileName)
{
    m_fileName = fileName;
    const QString displayName = fileName.isEmpty() ? tr("未命名") : fileName;
    setWindowTitle(tr("[*]%1 — GeoBoard").arg(displayName));
}

void GeoBoardMainWindow::setUndoRedoEnabled(bool canUndo, bool canRedo)
{
    m_undoAction->setEnabled(canUndo);
    m_redoAction->setEnabled(canRedo);
}

void GeoBoardMainWindow::setZoomPercent(int percent)
{
    m_zoomLabel->setText(tr("%1%").arg(percent));
}

void GeoBoardMainWindow::setSelectionCount(int count)
{
    if (count <= 0) {
        m_selectionLabel->setText(tr("未选中"));
        return;
    }
    m_selectionLabel->setText(tr("已选中 %1 个").arg(count));
}

QString GeoBoardMainWindow::ensureExtension(const QString &path)
{
    if (path.isEmpty()) {
        return path;
    }
    // 只在用户没写扩展名时补全：不要擅自改掉用户明确写下的扩展名
    if (QFileInfo(path).suffix().isEmpty()) {
        return path + QLatin1Char('.') + QLatin1String(kFileExtension);
    }
    return path;
}

QString GeoBoardMainWindow::promptOpenPath()
{
    return QFileDialog::getOpenFileName(this, tr("打开图形"), QString(), fileDialogFilter());
}

QString GeoBoardMainWindow::promptSavePath(const QString &suggested)
{
    const QString defaultName = suggested.isEmpty()
                                    ? tr("未命名.%1").arg(QLatin1String(kFileExtension))
                                    : suggested;
    const QString chosen =
        QFileDialog::getSaveFileName(this, tr("保存图形"), defaultName, fileDialogFilter());
    return ensureExtension(chosen);
}

void GeoBoardMainWindow::showError(const QString &title, const QString &detail)
{
    QMessageBox::critical(this, title, detail);
}

QString GeoBoardMainWindow::promptExportPath(const QString &suggested, QString *selectedFilter)
{
    // 过滤器顺序决定默认格式：PNG 在前，因为它更通用（任何环境都能打开）
    const QString filter = tr("PNG 位图 (*.png);;SVG 矢量图 (*.svg)");
    const QString defaultName = suggested.isEmpty() ? tr("作图.png") : suggested;

    QString chosenFilter;
    const QString chosen =
        QFileDialog::getSaveFileName(this, tr("导出为图片"), defaultName, filter, &chosenFilter);
    if (chosen.isEmpty()) {
        return QString();
    }
    if (selectedFilter != nullptr) {
        *selectedFilter = chosenFilter;
    }

    // 按选中的过滤器补扩展名：用户没写扩展名时不能靠猜，否则会存出一个无扩展名的文件
    const QString lower = chosen.toLower();
    if (chosenFilter.contains(QStringLiteral("svg"))) {
        return lower.endsWith(QStringLiteral(".svg")) ? chosen : chosen + QStringLiteral(".svg");
    }
    return lower.endsWith(QStringLiteral(".png")) ? chosen : chosen + QStringLiteral(".png");
}

GeoBoardMainWindow::SaveChangesAnswer GeoBoardMainWindow::askSaveChanges()
{
    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this,
        tr("尚未保存"),
        tr("当前作图有未保存的改动。\n\n要保存这些改动吗？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    switch (answer) {
    case QMessageBox::Save:
        return SaveChanges;
    case QMessageBox::Discard:
        return DiscardChanges;
    default:
        return CancelOperation;
    }
}

void GeoBoardMainWindow::showAbout()
{
    QMessageBox::about(
        this,
        tr("关于 GeoBoard"),
        tr("<b>GeoBoard %1</b>"
           "<p>一个动态几何画板：用点、线、圆与多边形作图，拖动任意一个自由点，"
           "依赖它的图形都会按照几何关系自动重算。</p>"
           "<p>支持中点、垂线、平行线、角平分线、交点等派生约束，"
           "以及距离、角度、半径、周长、面积的实时测量。</p>"
           "<p>作图可保存为 .gboard 文件（JSON 文本，可用文本编辑器查看）。</p>"
           "<p>技术栈：Qt 5 / QWidget / qmake。</p>")
            .arg(QStringLiteral(GEOBOARD_VERSION)));
}
