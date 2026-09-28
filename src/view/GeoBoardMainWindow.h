/**
 * @file    view/GeoBoardMainWindow.h
 * @brief   主窗口：工具栏、菜单、状态栏与画布的装配。
 * @details 本类只做两件事：
 *          1. **装配**界面（用 C++ 代码，不使用 `.ui`），并保持构造函数精简——
 *             每个部分拆成独立的 createXxx()；
 *          2. 把用户意图翻译成**语义化信号**（例如 toolChangeRequested），
 *             同时提供控制器需要的对话框与状态显示方法。
 *
 *          不负责：作图逻辑、文件读写、撤销栈（全部在 controller 与 service 层）。
 *          本类不持有 GeoDocument，也不创建任何 Model 或 Service 对象。
 */

#ifndef GEOBOARDMAINWINDOW_H
#define GEOBOARDMAINWINDOW_H

#include "controller/ToolId.h"
#include "model/Shape.h" // ShapeId：focusOnShape() 的形参需要它（只用到 id 类型）

#include <QMainWindow>
#include <QString>

class CanvasWidget;
class ShapeListPanel;
class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;
class QToolBar;

/**
 * @brief 应用主窗口。
 */
class GeoBoardMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    /** @brief "未保存改动"询问的三态结果。 */
    enum SaveChangesAnswer
    {
        SaveChanges,     ///< 保存后继续。
        DiscardChanges,  ///< 不保存，直接继续。
        CancelOperation  ///< 取消当前操作。
    };

    /**
     * @brief 构造主窗口。
     * @param parent 父对象，Qt 对象树负责其生命周期。
     */
    explicit GeoBoardMainWindow(QWidget *parent = nullptr);

    /** @brief 析构主窗口。 */
    ~GeoBoardMainWindow() override;

    /**
     * @brief 返回画布控件。
     * @return 画布指针，归本窗口所有。
     */
    CanvasWidget *canvas() const { return m_canvas; }

    /**
     * @brief 返回对象列表面板。
     * @return 面板指针，归本窗口所有；控制器据此绑定文档与接线。
     */
    ShapeListPanel *shapeListPanel() const { return m_shapeListPanel; }

    /**
     * @brief 显示或隐藏对象列表停靠窗口。
     * @param visible true 表示显示。
     */
    void setShapeListVisible(bool visible);

    /** @brief 查询对象列表停靠窗口是否可见。 */
    bool isShapeListVisible() const;

    /** @brief 按 id 把视图聚焦到某个图元（对象列表双击时使用）。 */
    void focusOnShape(ShapeId id);

    /**
     * @brief 同步工具栏的工具选中态。
     * @param tool 当前工具。
     */
    void setActiveTool(ToolId tool);

    /**
     * @brief 设置作图预览。
     * @param points 已确定的顶点（场景坐标）。
     * @param hint 当前鼠标位置（场景坐标）。
     * @param showCircle true 表示按"首点 → 活动点"为半径显示预览圆。
     */
    void setPreview(const QList<QPointF> &points, const QPointF &hint, bool showCircle = false);

    /** @brief 清除作图预览。 */
    void clearPreview();

    /**
     * @brief 在状态栏显示提示。
     * @param message 提示文本。
     * @param timeoutMs 自动清除的毫秒数；0 表示一直显示到下次替换。
     */
    void setStatusMessage(const QString &message, int timeoutMs);

    /**
     * @brief 返回状态栏当前显示的提示文本。
     * @return 提示文本；没有提示时为空串。
     * @note  这是**读取真实状态**而不是读取上一次写入的字符串：提示带超时，
     *        过期后控件里已经是空的了。测试据此断言"用户此刻看到什么"。
     */
    QString statusMessage() const;

    /**
     * @brief 设置状态栏的鼠标坐标文本。
     * @param text 坐标文本。
     */
    void setMouseStatus(const QString &text);

    /**
     * @brief 设置标题栏的未保存标记。
     * @param modified 是否有未保存改动。
     */
    void setModified(bool modified);

    /**
     * @brief 设置标题栏显示的文件名。
     * @param fileName 文件名；空串表示未命名文档。
     */
    void setFileName(const QString &fileName);

    /**
     * @brief 设置撤销/重做菜单项的可用性。
     * @param canUndo 是否可撤销。
     * @param canRedo 是否可重做。
     */
    void setUndoRedoEnabled(bool canUndo, bool canRedo);

    /**
     * @brief 设置状态栏的缩放百分比。
     * @param percent 百分比（100 为默认缩放）。
     */
    void setZoomPercent(int percent);

    /**
     * @brief 设置状态栏显示的选中图元数量。
     * @param count 选中的图元个数；0 表示当前没有选中任何图元。
     */
    void setSelectionCount(int count);

    /**
     * @brief 弹出文件打开对话框。
     * @return 选择的路径；用户取消时返回空串。
     */
    QString promptOpenPath();

    /**
     * @brief 弹出文件保存对话框。
     * @param suggested 建议的文件名；为空时使用默认名。
     * @return 选择的路径；用户取消时返回空串。
     */
    QString promptSavePath(const QString &suggested);

    /**
     * @brief 弹出"导出为图片"的保存对话框。
     * @param suggested 建议的文件名；为空时使用默认名。
     * @param selectedFilter 输出参数：用户选中的过滤器文本，供调用方判断导出 PNG 还是 SVG。
     * @return 选择的路径；用户取消时返回空串。
     */
    QString promptExportPath(const QString &suggested, QString *selectedFilter);

    /**
     * @brief 显示错误对话框。
     * @param title 标题。
     * @param detail 详细说明（面向用户，需说明原因与可行操作）。
     */
    void showError(const QString &title, const QString &detail);

    /**
     * @brief 询问是否保存未保存的改动。
     * @return 用户的选择。
     */
    SaveChangesAnswer askSaveChanges();

    /** @brief 显示"关于"对话框。 */
    void showAbout();

signals:
    /** @brief 请求切换工具。 @param tool 目标工具。 */
    void toolChangeRequested(ToolId tool);

    /** @brief 切换网格显示。 @param visible 是否显示。 */
    void gridVisibilityToggled(bool visible);

    /** @brief 切换坐标轴显示。 @param visible 是否显示。 */
    void axesVisibilityToggled(bool visible);

    /** @brief 切换点名与标注显示。 @param visible 是否显示。 */
    void labelsVisibilityToggled(bool visible);

    /** @brief 请求新建文档。 */
    void newRequested();

    /** @brief 请求打开文件。 @param path 文件路径；为空表示需要询问。 */
    void openRequested(const QString &path);

    /** @brief 请求保存文件。 @param path 目标路径；为空表示需要询问。 */
    void saveRequested(const QString &path);

    /** @brief 请求撤销。 */
    void undoRequested();

    /** @brief 请求重做。 */
    void redoRequested();

    /** @brief 请求放大视图。 */
    void zoomInRequested();

    /** @brief 请求缩小视图。 */
    void zoomOutRequested();

    /** @brief 请求恢复默认视图。 */
    void zoomResetRequested();

    /** @brief 请求把视图缩放到全部内容。 */
    void fitContentRequested();

    /** @brief 请求把当前作图导出为图片。 @param path 目标路径；为空表示需要询问。 */
    void exportRequested(const QString &path);

    /** @brief 切换吸附对齐（栅格 / 已有点 / 曲线）。 @param enabled 是否启用。 */
    void snapToggled(bool enabled);

    /**
     * @brief 请求调整坐标轴网格的最小精度。
     * @param minimumStep 每格不少于多少场景单位。
     */
    void gridPrecisionRequested(double minimumStep);

    /** @brief 请求显示"关于"对话框。 */
    void aboutRequested();

private slots:
    /**
     * @brief 响应工具栏动作的勾选。
     * @param action 被触发的动作。
     */
    void onToolActionTriggered(QAction *action);

    /**
     * @brief 响应画布报告的鼠标坐标。
     * @param scenePos 场景坐标。
     */
    void onMouseScenePosChanged(const QPointF &scenePos);

private:
    /** @brief 创建全部 QAction（含快捷键）。 */
    void createActions();

    /** @brief 创建菜单栏。 */
    void createMenus();

    /** @brief 创建工具栏。 */
    void createToolBar();

    /** @brief 创建状态栏（含坐标与缩放显示）。 */
    void createShapeListDock();

    /**
     * @brief 创建「坐标轴最小精度」子菜单（互斥单选项）。
     * @note  选项值经 gridPrecisionRequested() 交给控制器，由控制器写进 Camera；
     *        菜单本身不碰模型，保持 view 只发意图的分工。
     */
    void createGridPrecisionMenu();

    void createStatusBar();

    /** @brief 装配中央的画画布。 */
    void createCentralWidget();

    /**
     * @brief 创建一个工具动作并加入工具动作组。
     * @param tool 工具枚举。
     * @param shortcut 快捷键（可为空）。
     * @return 新建的动作。
     */
    QAction *createToolAction(ToolId tool, const QString &shortcut);

    /**
     * @brief 若给定路径没有扩展名，补上本程序的默认扩展名。
     * @param path 用户选择的路径。
     * @return 补全后的路径。
     */
    static QString ensureExtension(const QString &path);

    CanvasWidget *m_canvas;        ///< 画布。
    QToolBar *m_toolBar;           ///< 工具栏。
    QActionGroup *m_toolGroup;     ///< 工具动作组（互斥选中）。
    ShapeListPanel *m_shapeListPanel; ///< 对象列表面板（放在停靠窗口里）。
    QDockWidget *m_shapeListDock;  ///< 对象列表停靠窗口。
    QLabel *m_coordinateLabel;     ///< 状态栏右侧：鼠标坐标。
    QLabel *m_selectionLabel;      ///< 状态栏右侧：选中的图元数量。
    QLabel *m_zoomLabel;           ///< 状态栏右侧：缩放百分比。
    QString m_fileName;            ///< 当前文件名（用于标题栏）。

    QAction *m_newAction;          ///< 文件：新建。
    QAction *m_openAction;         ///< 文件：打开。
    QAction *m_saveAction;         ///< 文件：保存。
    QAction *m_saveAsAction;       ///< 文件：另存为。
    QAction *m_exitAction;         ///< 文件：退出。
    QAction *m_undoAction;         ///< 编辑：撤销。
    QAction *m_redoAction;         ///< 编辑：重做。
    QAction *m_gridAction;         ///< 视图：显示网格。
    QAction *m_axesAction;         ///< 视图：显示坐标轴。
    QAction *m_labelsAction;       ///< 视图：显示点名与标注。
    QAction *m_zoomInAction;       ///< 视图：放大。
    QAction *m_zoomOutAction;      ///< 视图：缩小。
    QAction *m_zoomResetAction;    ///< 视图：恢复默认缩放。
    QAction *m_fitContentAction;   ///< 视图：缩放到全部内容。
    QAction *m_snapAction;         ///< 视图：吸附对齐。
    QAction *m_shapeListAction;    ///< 视图：对象列表停靠窗口显隐。
    QActionGroup *m_gridPrecisionGroup; ///< 视图：坐标轴最小精度（互斥单选）。
    QMenu *m_gridPrecisionMenu;    ///< 视图：坐标轴最小精度子菜单。
    QAction *m_exportAction;       ///< 文件：导出为图片。
    QAction *m_aboutAction;        ///< 帮助：关于。
};

#endif // GEOBOARDMAINWINDOW_H
