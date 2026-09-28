/**
 * @file    service/JsonStore.h
 * @brief   把 GeoDocument 与 .gboard 文件（UTF-8 的 JSON 文本）互相转换。
 * @details 文件结构固定为：
 *
 *              {
 *                "format": "GeoBoard",
 *                "version": 1,
 *                "shapes": [ { "id": 1, "type": "point", ... }, ... ]
 *              }
 *
 *          shapes 数组按 id 升序写出；元素的每个父图元 id 都必须已经出现过且更小，
 *          于是"id 升序"同时就是拓扑序 —— 与 GeoDocument 的拓扑序不变式一致。
 *          加载时若不满足该不变式，判为"文件损坏或存在循环依赖"并整体拒绝。
 *
 *          两条硬约束：
 *          1. 保存走 QSaveFile 原子写入：写到一半失败不会把原文件截断成半截；
 *          2. 加载要么整体成功，要么 document 原样保留（先把图元全部构造并校验完，
 *             最后才 clear() 并批量移交所有权）。
 *
 *          不负责：判断"存什么才算对"。图元的父引用是否存在、几何是否退化由 model 层
 *          保证，本类只校验文件格式本身与引用完整性；
 *          不负责弹任何对话框（选择路径、覆盖确认由 view 负责）；
 *          不负责记录"当前文件路径"与"文档是否已修改"（见 MainController）；
 *          不负责撤销/重做（加载是整体替换，由上层决定是否入栈）。
 */

#ifndef GEOBOARDJSONSTORE_H
#define GEOBOARDJSONSTORE_H

#include "model/GeoDocument.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

/**
 * @brief .gboard 文件（JSON 文本）的读写与格式校验。
 * @details 见文件头的格式说明。失败原因一律用 lastError() 给出面向用户的说明，
 *          不使用异常，也不用 qDebug 输出。
 */
class JsonStore : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造存储对象。
     * @param parent 父对象，Qt 对象树负责其生命周期（MainController 传 this）。
     */
    explicit JsonStore(QObject *parent = nullptr);

    /**
     * @brief 析构。
     * @note  无需释放资源：文件句柄都是函数内的局部 QFile / QSaveFile。
     */
    ~JsonStore() override;

    /**
     * @brief 把文档保存为 JSON 文本文件（默认扩展名 .gboard）。
     * @param document 待保存的文档（按 shapesInOrder() 的 id 升序写出）。
     * @param path 目标文件路径；为空视为调用方错误，直接失败。
     * @return 成功返回 true；失败返回 false 并设置 lastError()。
     * @note  全程使用 QSaveFile：先把整份文本写进临时文件，commit() 成功才替换
     *        目标文件，因此写入中途失败不会破坏磁盘上已有的旧文件。
     * @note  本函数不创建目录，也不判断图元本身是否合法（那是 model 层的职责）。
     */
    bool save(const GeoDocument &document, const QString &path);

    /**
     * @brief 从文件读取并**重建**文档。
     * @param path 源文件路径。
     * @param document 目标文档；成功后其原有内容被整体替换。
     * @return 成功返回 true；失败返回 false 并设置 lastError()。
     * @warning 失败时不得改动 document（要么整体成功，要么原样保留）：
     *          实现上先把全部图元构造并校验完，最后才 clear() 并批量 addShape。
     */
    bool load(const QString &path, GeoDocument *document);

    /**
     * @brief 返回最近一次失败的原因（面向用户）。
     * @return 说明"发生了什么、可以怎么办"的文本；上一次操作成功时返回空串。
     */
    QString lastError() const;

    /**
     * @brief 返回本程序写入的文件格式版本号。
     * @return 当前为 1；加载时 version 高于它的文件一律拒绝。
     */
    static int formatVersion();

    /**
     * @brief 返回默认的文件扩展名（不含点），用于文件对话框过滤器。
     * @return 固定为 "gboard"。
     */
    static QString fileExtension();

private:
    /**
     * @brief 加载过程中已构造图元的档案，供后续图元校验父引用时使用。
     * @details 类型校验只能在这里做：此刻图元还没交给 GeoDocument，
     *          无法用 document->point(id) 之类的查询回答"这个 id 是不是点"。
     */
    struct LoadedShape
    {
        ShapeType type = ShapeType::Point;   ///< 图元种类。
        bool isCurve = false;                ///< 是否为曲线；取自 Shape::isCurve()，与 model 层同源。
    };

    // ----- 写出 -----

    /**
     * @brief 把一个图元写成 JSON 对象（含公共字段 id / type）。
     * @param shape 源图元。
     * @param object 输出参数：写好的 JSON 对象。
     * @param detail 输出参数：失败原因（不含"哪个图元"，由调用方补齐）。
     * @return 成功返回 true。
     */
    static bool shapeToJson(const Shape &shape, QJsonObject *object, QString *detail);

    /**
     * @brief 拼装"无法写入文件"的用户提示（含可能原因与建议动作）。
     * @param path 目标文件路径。
     * @param reason 系统给出的原因（如 QSaveFile::errorString()）。
     * @return 面向用户的完整提示文本。
     */
    static QString writeFailureMessage(const QString &path, const QString &reason);

    // ----- 读入 -----

    /**
     * @brief 读取一个必填的图元 id 字段（正整数）。
     * @param object 所在对象。
     * @param field 字段名。
     * @param value 输出参数：读到的 id。
     * @param detail 输出参数：失败原因。
     * @return 成功返回 true。
     */
    static bool readShapeIdField(const QJsonObject &object,
                                 const QString &field,
                                 ShapeId *value,
                                 QString *detail);

    /**
     * @brief 读取一个必填的整数字段并校验取值范围。
     * @param object 所在对象。
     * @param field 字段名。
     * @param minimum 允许的最小值（含）。
     * @param maximum 允许的最大值（含）。
     * @param value 输出参数：读到的整数。
     * @param detail 输出参数：失败原因。
     * @return 成功返回 true。
     */
    static bool readIntegerField(const QJsonObject &object,
                                 const QString &field,
                                 int minimum,
                                 int maximum,
                                 int *value,
                                 QString *detail);

    /**
     * @brief 读取一个必填的数值字段（坐标、锚点），要求是有限数。
     * @param object 所在对象。
     * @param field 字段名。
     * @param value 输出参数：读到的数值。
     * @param detail 输出参数：失败原因。
     * @return 成功返回 true。
     */
    static bool readNumberField(const QJsonObject &object,
                                const QString &field,
                                double *value,
                                QString *detail);

    /**
     * @brief 读取一个必填的字符串字段（type / kind / measurement）。
     * @param object 所在对象。
     * @param field 字段名。
     * @param value 输出参数：读到的字符串。
     * @param detail 输出参数：失败原因。
     * @return 成功返回 true。
     */
    static bool readStringField(const QJsonObject &object,
                                const QString &field,
                                QString *value,
                                QString *detail);

    /**
     * @brief 读取一个必填的图元 id 数组字段（vertices / inputPoints）。
     * @param object 所在对象。
     * @param field 字段名。
     * @param values 输出参数：读到的 id 列表（允许为空数组）。
     * @param detail 输出参数：失败原因。
     * @return 成功返回 true。
     */
    static bool readShapeIdArrayField(const QJsonObject &object,
                                      const QString &field,
                                      QList<ShapeId> *values,
                                      QString *detail);

    /**
     * @brief 校验一个父图元引用：必须先于自身出现，且 id 严格小于自身。
     * @param loadedShapes 已构造图元的档案表。
     * @param ownId 引用者自身的 id。
     * @param field 字段名（仅用于错误提示）。
     * @param parentId 被引用的父图元 id。
     * @param detail 输出参数：失败原因。
     * @return 通过校验返回 true。
     */
    static bool checkParent(const QHash<ShapeId, LoadedShape> &loadedShapes,
                            ShapeId ownId,
                            const QString &field,
                            ShapeId parentId,
                            QString *detail);

    /**
     * @brief 校验一个父图元引用，并要求它确实是某个指定类型。
     * @param loadedShapes 已构造图元的档案表。
     * @param ownId 引用者自身的 id。
     * @param field 字段名（仅用于错误提示）。
     * @param parentId 被引用的父图元 id。
     * @param expected 期望的图元种类（如 ShapeType::Point）。
     * @param detail 输出参数：失败原因。
     * @return 通过校验返回 true。
     */
    static bool checkTypedParent(const QHash<ShapeId, LoadedShape> &loadedShapes,
                                 ShapeId ownId,
                                 const QString &field,
                                 ShapeId parentId,
                                 ShapeType expected,
                                 QString *detail);

    /**
     * @brief 校验一个父图元引用，并要求它确实是曲线（可作为宿主或基准）。
     * @param loadedShapes 已构造图元的档案表。
     * @param ownId 引用者自身的 id。
     * @param field 字段名（仅用于错误提示）。
     * @param parentId 被引用的父图元 id。
     * @param detail 输出参数：失败原因。
     * @return 通过校验返回 true。
     */
    static bool checkCurveParent(const QHash<ShapeId, LoadedShape> &loadedShapes,
                                 ShapeId ownId,
                                 const QString &field,
                                 ShapeId parentId,
                                 QString *detail);

    /**
     * @brief 读取公共字段并派发到各类型的构造。
     * @param object 图元对应的 JSON 对象。
     * @param loadedShapes 已构造图元的档案表。
     * @param detail 输出参数：失败原因（已带"图元 <id>（<type>）"前缀）。
     * @return 新构造的图元；失败返回 nullptr（此时不会分配任何对象）。
     */
    static Shape *buildShape(const QJsonObject &object,
                             const QHash<ShapeId, LoadedShape> &loadedShapes,
                             QString *detail);

    /**
     * @brief 按 type 读取各类型专有字段并构造图元（不接触文档）。
     * @param object 图元对应的 JSON 对象。
     * @param id 已校验的图元 id。
     * @param type 已校验的图元种类。
     * @param loadedShapes 已构造图元的档案表。
     * @param detail 输出参数：失败原因（不含图元前缀）。
     * @return 新构造的图元；失败返回 nullptr（此时不会分配任何对象）。
     */
    static Shape *buildTypedShape(const QJsonObject &object,
                                  ShapeId id,
                                  ShapeType type,
                                  const QHash<ShapeId, LoadedShape> &loadedShapes,
                                  QString *detail);

    QString m_lastError;   ///< 最近一次失败的说明；成功后为空。
};

#endif // GEOBOARDJSONSTORE_H
