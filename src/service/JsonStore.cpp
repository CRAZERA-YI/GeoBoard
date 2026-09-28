/**
 * @file    service/JsonStore.cpp
 * @brief   .gboard 文件（UTF-8 JSON 文本）读写与格式校验的实现。
 * @details 两条实现纪律，改动本文件时必须保持：
 *
 *          1. **保存是原子的**：整份文本先在内存里拼好，再用 QSaveFile 写临时文件，
 *             commit() 成功才替换目标文件。任何一步失败都不会把磁盘上的旧文件
 *             截断成半截（这是"保存失败→原文件也没了"这类事故的唯一防线）。
 *
 *          2. **加载不会破坏现有文档**：先把 shapes 数组里每个元素都解析、
 *             构造并校验完（此时一个字节都还没写进 document），全部通过后才
 *             clear() 并批量 addShape。中途失败只需 qDeleteAll 自己 new 出来的
 *             图元，document 保持原样，且不泄漏内存。
 *
 *          校验的重点是**引用完整性**：每个父图元 id 必须已经出现过且严格小于
 *          自身 id（否则文件损坏或存在循环依赖），每个点字段必须真的指向一个点。
 *          类型校验在"构造期"完成——此时图元还没进入 GeoDocument，无法用
 *          document->point(id) 之类的查询来回答"这个 id 是不是点"。
 *
 *          不负责：判断"存什么才算对"（父引用是否存在、几何是否退化由 model 层
 *          保证）；不弹任何对话框（路径选择、覆盖确认由 view 负责）；不记录
 *          当前文件路径与脏标记（见 MainController）；不写 qDebug 日志。
 */

#include "JsonStore.h"

#include "model/Shape.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QtAlgorithms> // qDeleteAll：失败路径上统一释放已 new 出来的图元

#include <cmath>
#include <limits>

namespace {

/** @brief 本程序写出的格式版本号；加载时 version 高于它的文件一律拒绝。 */
const int kFormatVersion = 1;

/** @brief 错误提示里对 JSON 取值的最大描述长度，避免把整段文本塞进对话框。 */
const int kMaxValueDescriptionLength = 32;

/** @brief 测量标注允许的最大输入点数（与 MeasurementShape 的约定一致）。 */
const int kMaxMeasurementInputPoints = 3;

/**
 * @brief 判断 JSON 取值是否为可用 int 表示的整数。
 * @param value 待判断的取值。
 * @param result 输出参数：整数结果；返回 false 时不被写入。
 * @return 是有限、无小数部分且落在 int 范围内的数时返回 true。
 * @note  不直接用 QJsonValue::toInt()：它对 1.5、1e30 这类取值的截断/回退行为
 *        不够直观，这里显式判"是否为整数值"，让损坏的文件得到明确的错误提示。
 */
bool isIntegralValue(const QJsonValue &value, int *result)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!GeoMath::isFiniteNumber(number)) {
        return false;
    }
    if (number < 0.0 || number > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    if (std::floor(number) != number) {
        return false;
    }
    *result = static_cast<int>(number);
    return true;
}

/**
 * @brief 把一个 JSON 取值渲染成便于写进错误提示的短文本。
 * @param value 待渲染的取值。
 * @return 描述文本；超长内容会被截断，避免提示框被撑爆。
 * @note  只产出 ASCII 片段（null/true/[]/<missing>），不含面向用户的措辞，
 *        因此这里用 QStringLiteral 而不是 tr()。
 */
QString describeValue(const QJsonValue &value)
{
    QString text;
    switch (value.type()) {
    case QJsonValue::Null:
        text = QStringLiteral("null");
        break;
    case QJsonValue::Bool:
        text = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        break;
    case QJsonValue::Double:
        text = QString::number(value.toDouble());
        break;
    case QJsonValue::String:
        text = value.toString();
        break;
    case QJsonValue::Array:
        text = QStringLiteral("[]");
        break;
    case QJsonValue::Object:
        text = QStringLiteral("{}");
        break;
    case QJsonValue::Undefined:
        text = QStringLiteral("<missing>");
        break;
    }

    if (text.length() > kMaxValueDescriptionLength) {
        text = text.left(kMaxValueDescriptionLength) + QStringLiteral("…");
    }
    return text;
}

/**
 * @brief 图元种类 → 文件里的 type 取值。
 * @param type 图元种类。
 * @return 固定标记；取值无法识别时返回空串。
 */
QString typeToToken(ShapeType type)
{
    switch (type) {
    case ShapeType::Point:
        return QStringLiteral("point");
    case ShapeType::Line:
        return QStringLiteral("line");
    case ShapeType::Circle:
        return QStringLiteral("circle");
    case ShapeType::Arc:
        return QStringLiteral("arc");
    case ShapeType::Polygon:
        return QStringLiteral("polygon");
    case ShapeType::PerpendicularLine:
        return QStringLiteral("perpendicular");
    case ShapeType::ParallelLine:
        return QStringLiteral("parallel");
    case ShapeType::AngleBisector:
        return QStringLiteral("angleBisector");
    case ShapeType::MidPoint:
        return QStringLiteral("midpoint");
    case ShapeType::IntersectionPoint:
        return QStringLiteral("intersection");
    case ShapeType::Measurement:
        return QStringLiteral("measurement");
    }

    // 枚举取值已全部覆盖；此处的返回只为消除"并非所有路径都有返回值"的告警。
    return QString();
}

/**
 * @brief 文件里的 type 取值 → 图元种类。
 * @param token 文件中的取值。
 * @param type 输出参数：解析出的种类；返回 false 时不被写入。
 * @return 取值可识别时返回 true。
 */
bool tokenToType(const QString &token, ShapeType *type)
{
    if (token == QStringLiteral("point")) {
        *type = ShapeType::Point;
    } else if (token == QStringLiteral("line")) {
        *type = ShapeType::Line;
    } else if (token == QStringLiteral("circle")) {
        *type = ShapeType::Circle;
    } else if (token == QStringLiteral("arc")) {
        *type = ShapeType::Arc;
    } else if (token == QStringLiteral("polygon")) {
        *type = ShapeType::Polygon;
    } else if (token == QStringLiteral("perpendicular")) {
        *type = ShapeType::PerpendicularLine;
    } else if (token == QStringLiteral("parallel")) {
        *type = ShapeType::ParallelLine;
    } else if (token == QStringLiteral("angleBisector")) {
        *type = ShapeType::AngleBisector;
    } else if (token == QStringLiteral("midpoint")) {
        *type = ShapeType::MidPoint;
    } else if (token == QStringLiteral("intersection")) {
        *type = ShapeType::IntersectionPoint;
    } else if (token == QStringLiteral("measurement")) {
        *type = ShapeType::Measurement;
    } else {
        return false;
    }
    return true;
}

/**
 * @brief 点的构造方式 → 文件里的 kind 取值。
 * @param kind 构造方式。
 * @return "free" 或 "onObject"。
 */
QString pointKindToToken(PointKind kind)
{
    switch (kind) {
    case PointKind::Free:
        return QStringLiteral("free");
    case PointKind::OnObject:
        return QStringLiteral("onObject");
    }

    // 枚举取值已全部覆盖；此处的返回只为消除"并非所有路径都有返回值"的告警。
    return QString();
}

/**
 * @brief 文件里的 kind 取值 → 点的构造方式。
 * @param token 文件中的取值。
 * @param kind 输出参数：解析出的构造方式；返回 false 时不被写入。
 * @return 取值可识别时返回 true。
 */
bool tokenToPointKind(const QString &token, PointKind *kind)
{
    if (token == QStringLiteral("free")) {
        *kind = PointKind::Free;
    } else if (token == QStringLiteral("onObject")) {
        *kind = PointKind::OnObject;
    } else {
        return false;
    }
    return true;
}

/**
 * @brief 线的有界方式 → 文件里的 kind 取值。
 * @param kind 有界方式。
 * @return "segment"、"ray" 或 "infinite"。
 */
QString lineKindToToken(LineKind kind)
{
    switch (kind) {
    case LineKind::Segment:
        return QStringLiteral("segment");
    case LineKind::Ray:
        return QStringLiteral("ray");
    case LineKind::InfiniteLine:
        return QStringLiteral("infinite");
    }

    // 枚举取值已全部覆盖；此处的返回只为消除"并非所有路径都有返回值"的告警。
    return QString();
}

/**
 * @brief 文件里的 kind 取值 → 线的有界方式。
 * @param token 文件中的取值。
 * @param kind 输出参数：解析出的有界方式；返回 false 时不被写入。
 * @return 取值可识别时返回 true。
 */
bool tokenToLineKind(const QString &token, LineKind *kind)
{
    if (token == QStringLiteral("segment")) {
        *kind = LineKind::Segment;
    } else if (token == QStringLiteral("ray")) {
        *kind = LineKind::Ray;
    } else if (token == QStringLiteral("infinite")) {
        *kind = LineKind::InfiniteLine;
    } else {
        return false;
    }
    return true;
}

/**
 * @brief 测量类型 → 文件里的 measurement 取值。
 * @param kind 测量类型。
 * @return 固定标记；取值无法识别时返回空串。
 */
QString measurementKindToToken(MeasurementKind kind)
{
    switch (kind) {
    case MeasurementKind::Distance:
        return QStringLiteral("distance");
    case MeasurementKind::SegmentLength:
        return QStringLiteral("segmentLength");
    case MeasurementKind::Angle:
        return QStringLiteral("angle");
    case MeasurementKind::Radius:
        return QStringLiteral("radius");
    case MeasurementKind::Circumference:
        return QStringLiteral("circumference");
    case MeasurementKind::Area:
        return QStringLiteral("area");
    case MeasurementKind::PointCoordinates:
        return QStringLiteral("pointCoordinates");
    }

    // 枚举取值已全部覆盖；此处的返回只为消除"并非所有路径都有返回值"的告警。
    return QString();
}

/**
 * @brief 文件里的 measurement 取值 → 测量类型。
 * @param token 文件中的取值。
 * @param kind 输出参数：解析出的测量类型；返回 false 时不被写入。
 * @return 取值可识别时返回 true。
 */
bool tokenToMeasurementKind(const QString &token, MeasurementKind *kind)
{
    if (token == QStringLiteral("distance")) {
        *kind = MeasurementKind::Distance;
    } else if (token == QStringLiteral("segmentLength")) {
        *kind = MeasurementKind::SegmentLength;
    } else if (token == QStringLiteral("angle")) {
        *kind = MeasurementKind::Angle;
    } else if (token == QStringLiteral("radius")) {
        *kind = MeasurementKind::Radius;
    } else if (token == QStringLiteral("circumference")) {
        *kind = MeasurementKind::Circumference;
    } else if (token == QStringLiteral("area")) {
        *kind = MeasurementKind::Area;
    } else if (token == QStringLiteral("pointCoordinates")) {
        *kind = MeasurementKind::PointCoordinates;
    } else {
        return false;
    }
    return true;
}

} // namespace

JsonStore::JsonStore(QObject *parent)
    : QObject(parent)
    , m_lastError()
{
}

JsonStore::~JsonStore()
{
}

// ---------------------------------------------------------------------------
// 保存
// ---------------------------------------------------------------------------

bool JsonStore::save(const GeoDocument &document, const QString &path)
{
    m_lastError.clear();

    if (path.isEmpty()) {
        m_lastError = tr("无法保存：没有指定文件名。\n\n请使用「文件 → 另存为」选择一个保存位置后重试。");
        return false;
    }

    // 先在内存里拼出整份文本：任何图元写不出来都在这里失败，此时磁盘上的旧文件还没被碰过
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("GeoBoard"));
    root.insert(QStringLiteral("version"), formatVersion());

    QJsonArray shapes;
    const QList<Shape *> ordered = document.shapesInOrder(); // 已按 id 升序，即拓扑序
    for (int i = 0; i < ordered.size(); ++i) {
        const Shape *shape = ordered.at(i);
        QJsonObject object;
        QString detail;
        if (!shapeToJson(*shape, &object, &detail)) {
            m_lastError = tr("无法保存：图元 %1 无法写入文件（%2）。\n\n这属于程序内部错误，"
                             "请把当前图形截图后反馈给开发者。")
                              .arg(shape->id())
                              .arg(detail);
            return false;
        }
        shapes.append(object);
    }
    root.insert(QStringLiteral("shapes"), shapes);

    const QByteArray text = QJsonDocument(root).toJson(QJsonDocument::Indented);

    // 原子写入：QSaveFile 先写临时文件，commit() 成功才替换目标文件，
    // 因此"写到一半失败"不会把原文件截成半截。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = writeFailureMessage(path, file.errorString());
        return false;
    }

    const qint64 written = file.write(text);
    if (written != static_cast<qint64>(text.size())) {
        const QString reason = file.errorString();
        file.cancelWriting(); // 丢弃临时文件，保持目标文件不变
        m_lastError = writeFailureMessage(path, reason);
        return false;
    }

    if (!file.commit()) {
        m_lastError = writeFailureMessage(path, file.errorString());
        return false;
    }
    return true;
}

QString JsonStore::writeFailureMessage(const QString &path, const QString &reason)
{
    return tr("无法写入文件：%1\n\n可能原因：\n"
              "· 文件被其他程序占用（请关闭后重试）\n"
              "· 目标目录没有写入权限（请另存到桌面）\n"
              "· 磁盘空间不足\n\n"
              "系统提示：%2")
        .arg(path, reason);
}

// ---------------------------------------------------------------------------
// 加载
// ---------------------------------------------------------------------------

bool JsonStore::load(const QString &path, GeoDocument *document)
{
    m_lastError.clear();

    if (document == nullptr) {
        m_lastError = tr("内部错误：没有可写入的目标文档，无法打开文件。");
        return false;
    }
    if (path.isEmpty()) {
        m_lastError = tr("无法打开：没有指定文件名。\n\n请使用「文件 → 打开」选择一个 .gboard 文件后重试。");
        return false;
    }

    // ----- 1. 读入整份文本 -----
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = tr("无法读取文件：%1\n\n可能原因：\n"
                         "· 文件不存在或已被移动（请确认路径）\n"
                         "· 文件被其他程序占用（请关闭后重试）\n"
                         "· 没有读取该文件的权限（请把它复制到桌面后重试）\n\n"
                         "系统提示：%2")
                          .arg(path, file.errorString());
        return false;
    }

    const QByteArray text = file.readAll();
    if (file.error() != QFile::NoError) {
        const QString reason = file.errorString();
        file.close();
        m_lastError = tr("读取文件时出错：%1\n\n可能原因：\n"
                         "· 文件被其他程序占用，或读取过程中被删除\n"
                         "· 磁盘或网络驱动器读取失败（请复制到本机磁盘后重试）\n\n"
                         "系统提示：%2")
                          .arg(path, reason);
        return false;
    }
    file.close();

    // ----- 2. 解析 JSON -----
    QJsonParseError parseError;
    parseError.error = QJsonParseError::NoError;
    const QJsonDocument json = QJsonDocument::fromJson(text, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        m_lastError = tr("文件不是合法的 JSON 文本：%1\n\n可能原因：\n"
                         "· 文件被截断，或上次保存没有写完\n"
                         "· 该文件不是 GeoBoard 的 .gboard 文件\n"
                         "· 在文本编辑器中修改时破坏了 JSON 语法（多写/漏写逗号、引号）\n\n"
                         "系统提示：%2（第 %3 个字符附近）")
                          .arg(path)
                          .arg(parseError.errorString())
                          .arg(parseError.offset);
        return false;
    }
    if (!json.isObject()) {
        m_lastError = tr("文件结构不正确：%1\n\n顶层必须是一个 JSON 对象（以 { 开头、以 } 结尾）。"
                         "请确认打开的是 GeoBoard 保存的 .gboard 文件。")
                          .arg(path);
        return false;
    }

    const QJsonObject root = json.object();

    // ----- 3. 校验头部字段：format / version -----
    const QJsonValue formatValue = root.value(QStringLiteral("format"));
    if (!formatValue.isString() || formatValue.toString() != QStringLiteral("GeoBoard")) {
        m_lastError = tr("这不是 GeoBoard 的图形文件：%1\n\n"
                         "文件的 format 字段应为 \"GeoBoard\"，当前为 %2。\n"
                         "请使用「文件 → 打开」重新选择 .gboard 文件。")
                          .arg(path, describeValue(formatValue));
        return false;
    }

    int version = 0;
    QString headerDetail;
    if (!readIntegerField(root,
                          QStringLiteral("version"),
                          1,
                          std::numeric_limits<int>::max(),
                          &version,
                          &headerDetail)) {
        m_lastError = tr("无法确定文件的格式版本：%1\n\n%2"
                         "文件可能不是 GeoBoard 写出的 .gboard 文件，或已被改坏。")
                          .arg(path, headerDetail);
        return false;
    }
    if (version > formatVersion()) {
        m_lastError = tr("文件版本过高：%1\n\n文件版本 %2 高于本程序支持的版本 %3，"
                         "请升级 GeoBoard 后再打开。")
                          .arg(path)
                          .arg(version)
                          .arg(formatVersion());
        return false;
    }
    // 本程序只写出过版本 1（formatVersion() 恒为 1）；将来若出现需要迁移的旧版本，
    // 在此处按 version 分支补齐即可，当前无需任何转换。

    const QJsonValue shapesValue = root.value(QStringLiteral("shapes"));
    if (!shapesValue.isArray()) {
        m_lastError = tr("文件结构不完整：%1\n\n缺少 shapes 数组（图元列表），当前为 %2。\n"
                         "文件可能被截断，或在编辑时被删改了字段。")
                          .arg(path, describeValue(shapesValue));
        return false;
    }

    // ----- 4. 构造阶段：全部图元都构造并校验完之前，一个字节都不写进 document -----
    const QJsonArray shapeArray = shapesValue.toArray();
    QList<Shape *> pending;
    QHash<ShapeId, LoadedShape> loadedShapes;

    for (int i = 0; i < shapeArray.size(); ++i) {
        const QJsonValue element = shapeArray.at(i);
        if (!element.isObject()) {
            qDeleteAll(pending);
            pending.clear();
            m_lastError = tr("文件内容损坏：%1\n\n"
                             "shapes 数组的第 %2 个元素不是图元对象，当前为 %3。\n"
                             "可尝试：用文本编辑器打开该文件，核对该处的花括号与逗号。")
                              .arg(path)
                              .arg(i + 1)
                              .arg(describeValue(element));
            return false;
        }

        QString detail;
        Shape *shape = buildShape(element.toObject(), loadedShapes, &detail);
        if (shape == nullptr) {
            qDeleteAll(pending);
            pending.clear();
            m_lastError = tr("文件内容损坏：%1\n\n"
                             "shapes 数组的第 %2 个元素无法还原：%3\n\n"
                             "可尝试：用文本编辑器打开该文件，按上面的提示核对该处的字段与取值；"
                             "或用旧版本 GeoBoard 打开并另存。")
                              .arg(path)
                              .arg(i + 1)
                              .arg(detail);
            return false;
        }

        pending.append(shape);
        LoadedShape record;
        record.type = shape->type();
        record.isCurve = shape->isCurve(); // 取自 model 层，避免在这里重复一套"哪些类型是曲线"
        loadedShapes.insert(shape->id(), record);
    }

    // ----- 5. 提交阶段：把构造好的图元整体移交给文档 -----
    // 批量更新：加载 1000 个图元也只发一次 documentChanged，视图不必重绘 1000 次。
    document->beginUpdate();
    document->clear();
    for (int i = 0; i < pending.size(); ++i) {
        Shape *shape = pending.at(i);
        if (document->addShape(shape) == kInvalidShapeId) {
            // 上面已校验 id 为正整数且互不重复，addShape 不该拒绝；
            // 真发生了就删掉尚未接管的图元（含本体）并让整次加载失败。
            const ShapeId rejectedId = shape->id();
            delete shape;
            for (int j = i + 1; j < pending.size(); ++j) {
                delete pending.at(j);
            }
            pending.clear();
            document->endUpdate();
            m_lastError = tr("内部错误：重建文档时图元 %1 被拒绝（id 冲突）。\n\n"
                             "请重新打开该文件；若反复出现，请把该文件反馈给开发者。")
                              .arg(rejectedId);
            return false;
        }
    }
    pending.clear(); // 所有权已全部移交，避免留下悬空指针

    document->endUpdate();
    return true;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

QString JsonStore::lastError() const
{
    return m_lastError;
}

int JsonStore::formatVersion()
{
    return kFormatVersion;
}

QString JsonStore::fileExtension()
{
    return QStringLiteral("gboard");
}

// ---------------------------------------------------------------------------
// 写出：单个图元 → JSON 对象
// ---------------------------------------------------------------------------

bool JsonStore::shapeToJson(const Shape &shape, QJsonObject *object, QString *detail)
{
    const QString typeToken = typeToToken(shape.type());
    if (typeToken.isEmpty()) {
        *detail = tr("图元类型 %1 没有对应的文件取值。").arg(static_cast<int>(shape.type()));
        return false;
    }

    object->insert(QStringLiteral("id"), shape.id());
    object->insert(QStringLiteral("type"), typeToken);

    switch (shape.type()) {
    case ShapeType::Point: {
        const PointShape &point = static_cast<const PointShape &>(shape);
        object->insert(QStringLiteral("x"), point.position().x);
        object->insert(QStringLiteral("y"), point.position().y);
        if (point.kind() == PointKind::OnObject && point.hostId() != kInvalidShapeId) {
            object->insert(QStringLiteral("kind"), pointKindToToken(point.kind()));
            object->insert(QStringLiteral("host"), point.hostId());
        } else {
            // 宿主缺失的"附着点"在 model 层与自由点完全等价（recompute 直接采用自身坐标），
            // 因此按自由点写出：host 只对 onObject 有意义，写 0 反而会造出非法文件。
            object->insert(QStringLiteral("kind"), pointKindToToken(PointKind::Free));
        }
        return true;
    }
    case ShapeType::Line: {
        const LineShape &line = static_cast<const LineShape &>(shape);
        object->insert(QStringLiteral("kind"), lineKindToToken(line.lineKind()));
        object->insert(QStringLiteral("p1"), line.firstPointId());
        object->insert(QStringLiteral("p2"), line.secondPointId());
        return true;
    }
    case ShapeType::Circle: {
        const CircleShape &circle = static_cast<const CircleShape &>(shape);
        object->insert(QStringLiteral("center"), circle.centerPointId());
        object->insert(QStringLiteral("rim"), circle.rimPointId());
        return true;
    }
    case ShapeType::Arc: {
        const ArcShape &arc = static_cast<const ArcShape &>(shape);
        // "kind" 区分两种构造方式；三点弧是缺省值，因此省略它也不会读错
        // （旧文件里没有这个字段，读回时按三点弧处理，格式向后兼容）。
        object->insert(QStringLiteral("kind"),
                       arc.kind() == ArcKind::CenterTwoPoint ? QStringLiteral("centerTwoPoint")
                                                              : QStringLiteral("threePoint"));
        object->insert(QStringLiteral("start"), arc.startPointId());
        object->insert(QStringLiteral("middle"), arc.middlePointId());
        object->insert(QStringLiteral("end"), arc.endPointId());
        return true;
    }
    case ShapeType::Polygon: {
        const PolygonShape &polygon = static_cast<const PolygonShape &>(shape);
        const QList<ShapeId> &vertexIds = polygon.vertexPointIds();
        QJsonArray vertices;
        for (int i = 0; i < vertexIds.size(); ++i) {
            vertices.append(vertexIds.at(i));
        }
        object->insert(QStringLiteral("vertices"), vertices);
        return true;
    }
    case ShapeType::PerpendicularLine:
    case ShapeType::ParallelLine: {
        const LineShape &line = static_cast<const LineShape &>(shape);
        object->insert(QStringLiteral("base"), line.baseCurveId());
        object->insert(QStringLiteral("through"), line.secondPointId());
        return true;
    }
    case ShapeType::AngleBisector: {
        const LineShape &line = static_cast<const LineShape &>(shape);
        // 角平分线的"第二边上的点"没有公开访问器，构造时登记的父列表是它的唯一来源
        // （顺序见 LineShape(id, vertexPointId, firstRayPointId, secondRayPointId)）。
        const QList<ShapeId> parents = line.parents();
        if (parents.size() < 3) {
            *detail = tr("角平分线的定义点不足 3 个。");
            return false;
        }
        object->insert(QStringLiteral("vertex"), parents.at(0));
        object->insert(QStringLiteral("firstRay"), parents.at(1));
        object->insert(QStringLiteral("secondRay"), parents.at(2));
        return true;
    }
    case ShapeType::MidPoint: {
        const MidPointShape &midPoint = static_cast<const MidPointShape &>(shape);
        object->insert(QStringLiteral("p1"), midPoint.firstPointId());
        object->insert(QStringLiteral("p2"), midPoint.secondPointId());
        return true;
    }
    case ShapeType::IntersectionPoint: {
        const IntersectionPointShape &intersection =
            static_cast<const IntersectionPointShape &>(shape);
        object->insert(QStringLiteral("curve1"), intersection.firstCurveId());
        object->insert(QStringLiteral("curve2"), intersection.secondCurveId());
        object->insert(QStringLiteral("index"), intersection.index());
        return true;
    }
    case ShapeType::Measurement: {
        const MeasurementShape &measurement = static_cast<const MeasurementShape &>(shape);
        object->insert(QStringLiteral("measurement"), measurementKindToToken(measurement.kind()));

        const QList<ShapeId> &inputPointIds = measurement.inputPointIds();
        QJsonArray inputPoints;
        for (int i = 0; i < inputPointIds.size(); ++i) {
            inputPoints.append(inputPointIds.at(i));
        }
        object->insert(QStringLiteral("inputPoints"), inputPoints);
        object->insert(QStringLiteral("anchorX"), measurement.anchor().x);
        object->insert(QStringLiteral("anchorY"), measurement.anchor().y);

        switch (measurement.kind()) {
        case MeasurementKind::SegmentLength:
        case MeasurementKind::Radius:
        case MeasurementKind::Circumference:
        case MeasurementKind::Area:
            // 这四种测量必须带关联图元。关联图元缺失说明图元本身已不完整，
            // 此时不写 target —— 读入时会因缺少该字段而明确报错，问题不会被掩盖。
            if (measurement.referenceShapeId() != kInvalidShapeId) {
                object->insert(QStringLiteral("target"), measurement.referenceShapeId());
            }
            break;
        case MeasurementKind::Distance:
        case MeasurementKind::Angle:
        case MeasurementKind::PointCoordinates:
            // 只依赖输入点，文件中不写 target
            break;
        }
        return true;
    }
    }

    // 枚举取值已在上面的 switch 中全部覆盖；此处的兜底只为消除"并非所有路径都有返回值"的告警。
    *detail = tr("图元类型 %1 尚未支持写出。").arg(static_cast<int>(shape.type()));
    return false;
}

// ---------------------------------------------------------------------------
// 读入：字段级读取
// ---------------------------------------------------------------------------

bool JsonStore::readShapeIdField(const QJsonObject &object,
                                 const QString &field,
                                 ShapeId *value,
                                 QString *detail)
{
    const QJsonValue jsonValue = object.value(field);
    if (jsonValue.isUndefined()) {
        *detail = tr("缺少 \"%1\" 字段。").arg(field);
        return false;
    }

    int number = 0;
    if (!isIntegralValue(jsonValue, &number) || number < 1) {
        *detail = tr("\"%1\" 必须是正整数（图元 id），当前为 %2。")
                      .arg(field, describeValue(jsonValue));
        return false;
    }

    *value = static_cast<ShapeId>(number);
    return true;
}

bool JsonStore::readIntegerField(const QJsonObject &object,
                                 const QString &field,
                                 int minimum,
                                 int maximum,
                                 int *value,
                                 QString *detail)
{
    const QJsonValue jsonValue = object.value(field);
    if (jsonValue.isUndefined()) {
        *detail = tr("缺少 \"%1\" 字段。").arg(field);
        return false;
    }

    int number = 0;
    if (!isIntegralValue(jsonValue, &number) || number < minimum || number > maximum) {
        *detail = tr("\"%1\" 必须是 %2 到 %3 之间的整数，当前为 %4。")
                      .arg(field)
                      .arg(minimum)
                      .arg(maximum)
                      .arg(describeValue(jsonValue));
        return false;
    }

    *value = number;
    return true;
}

bool JsonStore::readNumberField(const QJsonObject &object,
                                const QString &field,
                                double *value,
                                QString *detail)
{
    const QJsonValue jsonValue = object.value(field);
    if (jsonValue.isUndefined()) {
        *detail = tr("缺少 \"%1\" 字段。").arg(field);
        return false;
    }
    if (!jsonValue.isDouble()) {
        *detail = tr("\"%1\" 必须是数字，当前为 %2。").arg(field, describeValue(jsonValue));
        return false;
    }

    const double number = jsonValue.toDouble();
    if (!GeoMath::isFiniteNumber(number)) {
        *detail = tr("\"%1\" 必须是有限的数值（不能是 NaN 或无穷大），当前为 %2。")
                      .arg(field, describeValue(jsonValue));
        return false;
    }

    *value = number;
    return true;
}

bool JsonStore::readStringField(const QJsonObject &object,
                                const QString &field,
                                QString *value,
                                QString *detail)
{
    const QJsonValue jsonValue = object.value(field);
    if (jsonValue.isUndefined()) {
        *detail = tr("缺少 \"%1\" 字段。").arg(field);
        return false;
    }
    if (!jsonValue.isString()) {
        *detail = tr("\"%1\" 必须是字符串，当前为 %2。").arg(field, describeValue(jsonValue));
        return false;
    }

    *value = jsonValue.toString();
    return true;
}

bool JsonStore::readShapeIdArrayField(const QJsonObject &object,
                                      const QString &field,
                                      QList<ShapeId> *values,
                                      QString *detail)
{
    const QJsonValue jsonValue = object.value(field);
    if (jsonValue.isUndefined()) {
        *detail = tr("缺少 \"%1\" 字段。").arg(field);
        return false;
    }
    if (!jsonValue.isArray()) {
        *detail = tr("\"%1\" 必须是图元 id 的数组，当前为 %2。")
                      .arg(field, describeValue(jsonValue));
        return false;
    }

    const QJsonArray array = jsonValue.toArray();
    QList<ShapeId> ids;
    for (int i = 0; i < array.size(); ++i) {
        int number = 0;
        if (!isIntegralValue(array.at(i), &number) || number < 1) {
            *detail = tr("\"%1\" 的第 %2 个元素必须是正整数（图元 id），当前为 %3。")
                          .arg(field)
                          .arg(i + 1)
                          .arg(describeValue(array.at(i)));
            return false;
        }
        ids.append(static_cast<ShapeId>(number));
    }

    *values = ids;
    return true;
}

// ---------------------------------------------------------------------------
// 读入：父引用校验
// ---------------------------------------------------------------------------

bool JsonStore::checkParent(const QHash<ShapeId, LoadedShape> &loadedShapes,
                            ShapeId ownId,
                            const QString &field,
                            ShapeId parentId,
                            QString *detail)
{
    if (!loadedShapes.contains(parentId)) {
        *detail = tr("\"%1\" 引用的图元 id %2 在它之前没有出现过。\n"
                     "每个父图元都必须先于子图元出现且 id 更小；"
                     "文件可能被截断、被改坏，或存在循环依赖。")
                      .arg(field)
                      .arg(parentId);
        return false;
    }
    if (parentId >= ownId) {
        *detail = tr("\"%1\" 引用的图元 id %2 必须小于自身 id %3（id 升序即拓扑序）。\n"
                     "文件可能存在循环依赖，或已被改坏。")
                      .arg(field)
                      .arg(parentId)
                      .arg(ownId);
        return false;
    }
    return true;
}

bool JsonStore::checkTypedParent(const QHash<ShapeId, LoadedShape> &loadedShapes,
                                 ShapeId ownId,
                                 const QString &field,
                                 ShapeId parentId,
                                 ShapeType expected,
                                 QString *detail)
{
    if (!checkParent(loadedShapes, ownId, field, parentId, detail)) {
        return false;
    }

    const ShapeType actual = loadedShapes.value(parentId).type;
    if (actual != expected) {
        *detail = tr("\"%1\" 引用的 id %2 是 %3，但这里需要一个 %4。\n"
                     "图元之间的引用关系不正确。")
                      .arg(field)
                      .arg(parentId)
                      .arg(typeToToken(actual))
                      .arg(typeToToken(expected));
        return false;
    }
    return true;
}

bool JsonStore::checkCurveParent(const QHash<ShapeId, LoadedShape> &loadedShapes,
                                 ShapeId ownId,
                                 const QString &field,
                                 ShapeId parentId,
                                 QString *detail)
{
    if (!checkParent(loadedShapes, ownId, field, parentId, detail)) {
        return false;
    }

    const LoadedShape parent = loadedShapes.value(parentId);
    if (!parent.isCurve) {
        *detail = tr("\"%1\" 引用的 id %2 是 %3，它不是曲线，不能作为父曲线。\n"
                     "图元之间的引用关系不正确。")
                      .arg(field)
                      .arg(parentId)
                      .arg(typeToToken(parent.type));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 读入：图元构造
// ---------------------------------------------------------------------------

Shape *JsonStore::buildShape(const QJsonObject &object,
                             const QHash<ShapeId, LoadedShape> &loadedShapes,
                             QString *detail)
{
    ShapeId id = kInvalidShapeId;
    if (!readShapeIdField(object, QStringLiteral("id"), &id, detail)) {
        return nullptr;
    }
    if (loadedShapes.contains(id)) {
        *detail = tr("图元 id %1 重复出现：同一个 id 在文件中只能出现一次。").arg(id);
        return nullptr;
    }

    QString typeToken;
    if (!readStringField(object, QStringLiteral("type"), &typeToken, detail)) {
        *detail = tr("图元 %1：%2").arg(id).arg(*detail);
        return nullptr;
    }

    ShapeType type = ShapeType::Point;
    if (!tokenToType(typeToken, &type)) {
        *detail = tr("图元 %1 的 type 取值无法识别：\"%2\"。\n"
                     "可用取值：point、line、circle、arc、polygon、perpendicular、parallel、"
                     "angleBisector、midpoint、intersection、measurement。")
                      .arg(id)
                      .arg(typeToken);
        return nullptr;
    }

    QString typeDetail;
    Shape *shape = buildTypedShape(object, id, type, loadedShapes, &typeDetail);
    if (shape == nullptr) {
        *detail = tr("图元 %1（%2）：%3").arg(id).arg(typeToken, typeDetail);
        return nullptr;
    }
    return shape;
}

Shape *JsonStore::buildTypedShape(const QJsonObject &object,
                                  ShapeId id,
                                  ShapeType type,
                                  const QHash<ShapeId, LoadedShape> &loadedShapes,
                                  QString *detail)
{
    switch (type) {
    case ShapeType::Point: {
        double x = 0.0;
        double y = 0.0;
        if (!readNumberField(object, QStringLiteral("x"), &x, detail)) {
            return nullptr;
        }
        if (!readNumberField(object, QStringLiteral("y"), &y, detail)) {
            return nullptr;
        }

        QString kindToken;
        if (!readStringField(object, QStringLiteral("kind"), &kindToken, detail)) {
            return nullptr;
        }
        PointKind kind = PointKind::Free;
        if (!tokenToPointKind(kindToken, &kind)) {
            *detail = tr("kind 取值无法识别：\"%1\"（点只支持 \"free\" 与 \"onObject\"）。")
                          .arg(kindToken);
            return nullptr;
        }

        if (kind == PointKind::Free) {
            return new PointShape(id, PointKind::Free, Vec2(x, y));
        }

        // 附着点必须给出宿主曲线：没有宿主的"附着点"在 model 层等同自由点，
        // 出现在文件里说明字段被删改过，宁可拒绝也不静默降级。
        ShapeId host = kInvalidShapeId;
        if (!readShapeIdField(object, QStringLiteral("host"), &host, detail)) {
            return nullptr;
        }
        if (!checkCurveParent(loadedShapes, id, QStringLiteral("host"), host, detail)) {
            return nullptr;
        }

        PointShape *point = new PointShape(id, PointKind::OnObject, Vec2(x, y));
        // 宿主 id 不在构造参数里：必须在 addShape 之前登记，否则 GeoDocument
        // 建立依赖索引时看不到这条边，拖动宿主曲线时该点不会跟随。
        point->setHostId(host);
        return point;
    }
    case ShapeType::Line: {
        QString kindToken;
        if (!readStringField(object, QStringLiteral("kind"), &kindToken, detail)) {
            return nullptr;
        }
        LineKind lineKind = LineKind::Segment;
        if (!tokenToLineKind(kindToken, &lineKind)) {
            *detail = tr("kind 取值无法识别：\"%1\"（线只支持 \"segment\"、\"ray\" 与 \"infinite\"）。")
                          .arg(kindToken);
            return nullptr;
        }

        ShapeId firstPointId = kInvalidShapeId;
        ShapeId secondPointId = kInvalidShapeId;
        if (!readShapeIdField(object, QStringLiteral("p1"), &firstPointId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("p2"), &secondPointId, detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("p1"),
                              firstPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("p2"),
                              secondPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }

        return new LineShape(id, lineKind, firstPointId, secondPointId);
    }
    case ShapeType::Circle: {
        ShapeId centerPointId = kInvalidShapeId;
        ShapeId rimPointId = kInvalidShapeId;
        if (!readShapeIdField(object, QStringLiteral("center"), &centerPointId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("rim"), &rimPointId, detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("center"),
                              centerPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("rim"),
                              rimPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }

        return new CircleShape(id, centerPointId, rimPointId);
    }
    case ShapeType::Arc: {
        ShapeId startPointId = kInvalidShapeId;
        ShapeId middlePointId = kInvalidShapeId;
        ShapeId endPointId = kInvalidShapeId;
        if (!readShapeIdField(object, QStringLiteral("start"), &startPointId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("middle"), &middlePointId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("end"), &endPointId, detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("start"),
                              startPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("middle"),
                              middlePointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("end"),
                              endPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }

        // kind 字段是可选的：缺失或为 "threePoint" 时按三点弧处理（兼容旧文件）；
        // 取值非法时明确报错，而不是悄悄按三点弧读入——否则用户会得到一条
        // 位置看似随机、实际语义错误的弧。
        ArcKind kind = ArcKind::ThreePoint;
        const QJsonValue kindValue = object.value(QStringLiteral("kind"));
        if (!kindValue.isUndefined()) {
            if (!kindValue.isString()) {
                *detail = tr("图元 %1（arc）的 \"kind\" 字段必须是字符串。").arg(id);
                return nullptr;
            }
            const QString kindToken = kindValue.toString();
            if (kindToken == QStringLiteral("threePoint")) {
                kind = ArcKind::ThreePoint;
            } else if (kindToken == QStringLiteral("centerTwoPoint")) {
                kind = ArcKind::CenterTwoPoint;
            } else {
                *detail = tr("图元 %1（arc）的 \"kind\" 取值 \"%2\" 无法识别；"
                             "可用取值：threePoint、centerTwoPoint。")
                              .arg(id)
                              .arg(kindToken);
                return nullptr;
            }
        }

        if (kind == ArcKind::CenterTwoPoint) {
            // 圆心 + 两点：圆心、半径点、弧上点
            return new ArcShape(id, startPointId, endPointId, middlePointId, kind);
        }
        return new ArcShape(id, startPointId, middlePointId, endPointId);
    }
    case ShapeType::Polygon: {
        QList<ShapeId> vertexPointIds;
        if (!readShapeIdArrayField(object, QStringLiteral("vertices"), &vertexPointIds, detail)) {
            return nullptr;
        }
        if (vertexPointIds.size() < 3) {
            *detail = tr("多边形至少需要 3 个顶点，当前只有 %1 个。").arg(vertexPointIds.size());
            return nullptr;
        }

        for (int i = 0; i < vertexPointIds.size(); ++i) {
            if (!checkTypedParent(loadedShapes,
                                  id,
                                  QStringLiteral("vertices"),
                                  vertexPointIds.at(i),
                                  ShapeType::Point,
                                  detail)) {
                return nullptr;
            }
        }

        return new PolygonShape(id, vertexPointIds);
    }
    case ShapeType::PerpendicularLine:
    case ShapeType::ParallelLine: {
        ShapeId baseCurveId = kInvalidShapeId;
        ShapeId throughPointId = kInvalidShapeId;
        if (!readShapeIdField(object, QStringLiteral("base"), &baseCurveId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("through"), &throughPointId, detail)) {
            return nullptr;
        }
        if (!checkCurveParent(loadedShapes, id, QStringLiteral("base"), baseCurveId, detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("through"),
                              throughPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }

        return new LineShape(id, type, baseCurveId, throughPointId);
    }
    case ShapeType::AngleBisector: {
        ShapeId vertexPointId = kInvalidShapeId;
        ShapeId firstRayPointId = kInvalidShapeId;
        ShapeId secondRayPointId = kInvalidShapeId;
        if (!readShapeIdField(object, QStringLiteral("vertex"), &vertexPointId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("firstRay"), &firstRayPointId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("secondRay"), &secondRayPointId, detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("vertex"),
                              vertexPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("firstRay"),
                              firstRayPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("secondRay"),
                              secondRayPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }

        return new LineShape(id, vertexPointId, firstRayPointId, secondRayPointId);
    }
    case ShapeType::MidPoint: {
        ShapeId firstPointId = kInvalidShapeId;
        ShapeId secondPointId = kInvalidShapeId;
        if (!readShapeIdField(object, QStringLiteral("p1"), &firstPointId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("p2"), &secondPointId, detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("p1"),
                              firstPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }
        if (!checkTypedParent(loadedShapes,
                              id,
                              QStringLiteral("p2"),
                              secondPointId,
                              ShapeType::Point,
                              detail)) {
            return nullptr;
        }

        return new MidPointShape(id, firstPointId, secondPointId);
    }
    case ShapeType::IntersectionPoint: {
        ShapeId firstCurveId = kInvalidShapeId;
        ShapeId secondCurveId = kInvalidShapeId;
        if (!readShapeIdField(object, QStringLiteral("curve1"), &firstCurveId, detail)) {
            return nullptr;
        }
        if (!readShapeIdField(object, QStringLiteral("curve2"), &secondCurveId, detail)) {
            return nullptr;
        }
        int index = 0;
        if (!readIntegerField(object, QStringLiteral("index"), 0, 1, &index, detail)) {
            return nullptr;
        }
        if (!checkCurveParent(loadedShapes, id, QStringLiteral("curve1"), firstCurveId, detail)) {
            return nullptr;
        }
        if (!checkCurveParent(loadedShapes, id, QStringLiteral("curve2"), secondCurveId, detail)) {
            return nullptr;
        }

        return new IntersectionPointShape(id, firstCurveId, secondCurveId, index);
    }
    case ShapeType::Measurement: {
        QString kindToken;
        if (!readStringField(object, QStringLiteral("measurement"), &kindToken, detail)) {
            return nullptr;
        }
        MeasurementKind kind = MeasurementKind::Distance;
        if (!tokenToMeasurementKind(kindToken, &kind)) {
            *detail = tr("measurement 取值无法识别：\"%1\"。\n"
                         "可用取值：distance、segmentLength、angle、radius、circumference、"
                         "area、pointCoordinates。")
                          .arg(kindToken);
            return nullptr;
        }

        QList<ShapeId> inputPointIds;
        if (!readShapeIdArrayField(object, QStringLiteral("inputPoints"), &inputPointIds, detail)) {
            return nullptr;
        }
        if (inputPointIds.size() > kMaxMeasurementInputPoints) {
            *detail = tr("\"inputPoints\" 最多 %1 个点，当前有 %2 个。")
                          .arg(kMaxMeasurementInputPoints)
                          .arg(inputPointIds.size());
            return nullptr;
        }

        // 输入点个数的下限与 MeasurementShape::recompute 的要求一致：
        // 少了就永远是无效标注，属于文件被改坏的信号。
        int minimumInputPointCount = 0;
        switch (kind) {
        case MeasurementKind::Distance:
            minimumInputPointCount = 2;
            break;
        case MeasurementKind::Angle:
            minimumInputPointCount = 3;
            break;
        case MeasurementKind::PointCoordinates:
            minimumInputPointCount = 1;
            break;
        case MeasurementKind::SegmentLength:
        case MeasurementKind::Radius:
        case MeasurementKind::Circumference:
        case MeasurementKind::Area:
            minimumInputPointCount = 0; // 只依赖关联图元
            break;
        }
        if (inputPointIds.size() < minimumInputPointCount) {
            *detail = tr("\"%1\" 这种测量至少需要 %2 个输入点，当前只有 %3 个。")
                          .arg(kindToken)
                          .arg(minimumInputPointCount)
                          .arg(inputPointIds.size());
            return nullptr;
        }

        for (int i = 0; i < inputPointIds.size(); ++i) {
            if (!checkTypedParent(loadedShapes,
                                  id,
                                  QStringLiteral("inputPoints"),
                                  inputPointIds.at(i),
                                  ShapeType::Point,
                                  detail)) {
                return nullptr;
            }
        }

        double anchorX = 0.0;
        double anchorY = 0.0;
        if (!readNumberField(object, QStringLiteral("anchorX"), &anchorX, detail)) {
            return nullptr;
        }
        if (!readNumberField(object, QStringLiteral("anchorY"), &anchorY, detail)) {
            return nullptr;
        }

        ShapeId target = kInvalidShapeId;
        switch (kind) {
        case MeasurementKind::SegmentLength:
        case MeasurementKind::Radius:
        case MeasurementKind::Circumference:
        case MeasurementKind::Area: {
            if (!readShapeIdField(object, QStringLiteral("target"), &target, detail)) {
                return nullptr;
            }
            // 关联图元的类型必须与 model 层重算时的期望一致，否则加载后永远是无效标注。
            // 注意 Area 有**两种**合法来源：多边形的鞋带面积与圆的 πr²，
            // 因此它接受 Polygon 或 Circle；写死成 Polygon 会让"圆面积标注"
            // 存得下、打不开（这是实测发现的往返缺陷）。
            const bool targetTypeOk =
                (kind == MeasurementKind::SegmentLength)
                    ? checkTypedParent(loadedShapes, id, QStringLiteral("target"), target,
                                       ShapeType::Line, detail)
                : (kind == MeasurementKind::Radius || kind == MeasurementKind::Circumference)
                    ? checkTypedParent(loadedShapes, id, QStringLiteral("target"), target,
                                       ShapeType::Circle, detail)
                    : (checkTypedParent(loadedShapes, id, QStringLiteral("target"), target,
                                        ShapeType::Polygon, detail)
                       || checkTypedParent(loadedShapes, id, QStringLiteral("target"), target,
                                           ShapeType::Circle, detail));
            if (!targetTypeOk) {
                return nullptr;
            }
            break;
        }
        case MeasurementKind::Distance:
        case MeasurementKind::Angle:
        case MeasurementKind::PointCoordinates:
            // 只依赖输入点，文件中不写 target
            break;
        }

        return new MeasurementShape(id, kind, inputPointIds, target, Vec2(anchorX, anchorY));
    }
    }

    // 枚举取值已在上面的 switch 中全部覆盖；此处的兜底只为消除"并非所有路径都有返回值"的告警。
    *detail = tr("图元类型 %1 无法还原。").arg(static_cast<int>(type));
    return nullptr;
}
