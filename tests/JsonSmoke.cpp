/**
 * @file    tests/JsonSmoke.cpp
 * @brief   文件读写往返自测：把全部图元类型存盘再读回，逐项核对。
 * @details 存在的理由：`docs/test.md` 的自动化部分此前只覆盖几何引擎，
 *          而"保存后能否正确打开"是**用户数据安全**的关键路径。这个测试专门守住它，
 *          且必须覆盖每一种测量类型——实测中正是"圆面积标注"因为加载校验写死了
 *          Polygon 而存得下、打不开，这类缺陷只有真正走一遍 save → load 才会暴露。
 *
 *          运行方式见 docs/test.md；全部通过时退出码为 0。
 */

#include "model/GeoDocument.h"
#include "model/GeometryMath.h"
#include "model/Shape.h"
#include "service/JsonStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

namespace {

/** @brief 输出一行文本（控制台已切到 UTF-8，见 docs/test.md）。 */
void print(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    std::fputs(utf8.constData(), stdout);
    std::fputs("\n", stdout);
}

/** @brief 断言两数在容差内相等。 */
bool nearlyEqual(double a, double b, double tolerance = 1e-6)
{
    return std::fabs(a - b) <= tolerance;
}

/** @brief 极简断言计数器。 */
class Checker
{
public:
    Checker() : m_passed(0), m_failed(0) {}

    /**
     * @brief 记录一条断言。
     * @param condition 是否成立。
     * @param description 描述。
     * @param detail 失败补充信息。
     */
    void check(bool condition, const QString &description, const QString &detail = QString())
    {
        if (condition) {
            ++m_passed;
            return;
        }
        ++m_failed;
        print(QStringLiteral("[FAIL] %1").arg(description));
        if (!detail.isEmpty()) {
            print(QStringLiteral("       %1").arg(detail));
        }
    }

    /** @brief 返回失败数。 */
    int failures() const { return m_failed; }

    /** @brief 返回通过数。 */
    int passes() const { return m_passed; }

private:
    int m_passed;
    int m_failed;
};

/** @brief 加一个自由点。 */
ShapeId addPoint(GeoDocument *document, double x, double y)
{
    const ShapeId id = document->allocateShapeId();
    document->addShape(new PointShape(id, PointKind::Free, Vec2(x, y)));
    return id;
}

/** @brief 加一条线。 */
ShapeId addLine(GeoDocument *document, LineKind kind, ShapeId a, ShapeId b)
{
    const ShapeId id = document->allocateShapeId();
    document->addShape(new LineShape(id, kind, a, b));
    return id;
}

/** @brief 加一个圆。 */
ShapeId addCircle(GeoDocument *document, ShapeId center, ShapeId rim)
{
    const ShapeId id = document->allocateShapeId();
    document->addShape(new CircleShape(id, center, rim));
    return id;
}

/** @brief 加一个测量标注。 */
ShapeId addMeasurement(GeoDocument *document,
                       MeasurementKind kind,
                       const QList<ShapeId> &inputs,
                       ShapeId target,
                       const Vec2 &anchor)
{
    const ShapeId id = document->allocateShapeId();
    document->addShape(new MeasurementShape(id, kind, inputs, target, anchor));
    return id;
}

/** @brief 取某个测量标注的数值（不存在时返回 NaN）。 */
double measurementValue(GeoDocument *document, ShapeId id)
{
    Shape *shape = document->shape(id);
    if (shape == nullptr || shape->type() != ShapeType::Measurement) {
        return std::nan("");
    }
    return static_cast<const MeasurementShape *>(shape)->value();
}

/**
 * @brief 构造一份包含**全部图元类型与全部测量类型**的文档。
 * @param document 目标文档。
 * @return 各个关键图元的 id（用于往返后比对）。
 */
struct Fixture
{
    ShapeId pointA;      ///< 点 A。
    ShapeId pointB;      ///< 点 B。
    ShapeId segment;     ///< 线段 AB。
    ShapeId circle;      ///< 以 A 为圆心、B 为圆上点的圆。
    ShapeId circleArea;  ///< 圆面积标注（回归用：必须能往返）。
    ShapeId circumference; ///< 圆周长标注。
    ShapeId radius;      ///< 半径标注。
    ShapeId distance;    ///< 两点距离标注。
    ShapeId length;      ///< 线段长度标注。
    ShapeId coordinates; ///< 点坐标标注。
};

Fixture buildFixture(GeoDocument *document)
{
    Fixture fixture;

    fixture.pointA = addPoint(document, 0.0, 0.0);
    fixture.pointB = addPoint(document, 3.0, 0.0);
    fixture.segment = addLine(document, LineKind::Segment, fixture.pointA, fixture.pointB);
    fixture.circle = addCircle(document, fixture.pointA, fixture.pointB);

    // 圆族测量（三种都要存得下、打得开）
    fixture.radius = addMeasurement(document, MeasurementKind::Radius, QList<ShapeId>(),
                                    fixture.circle, Vec2(0.0, 3.2));
    fixture.circumference = addMeasurement(document, MeasurementKind::Circumference,
                                           QList<ShapeId>(), fixture.circle, Vec2(0.0, 3.8));
    fixture.circleArea = addMeasurement(document, MeasurementKind::Area, QList<ShapeId>(),
                                        fixture.circle, Vec2(0.0, 4.4));

    // 三角形与它的边、中点、度量的多边形面积
    const ShapeId pointC = addPoint(document, 0.0, 4.0);
    const ShapeId sideOne = addLine(document, LineKind::Segment, fixture.pointA, pointC);
    const ShapeId sideTwo = addLine(document, LineKind::Segment, pointC, fixture.pointB);
    const ShapeId sideThree = addLine(document, LineKind::Segment, fixture.pointB, fixture.pointA);

    QList<ShapeId> triangleVertices;
    triangleVertices << fixture.pointA << pointC << fixture.pointB;
    const ShapeId triangle = document->allocateShapeId();
    document->addShape(new PolygonShape(triangle, triangleVertices));

    const ShapeId polygonArea = addMeasurement(document, MeasurementKind::Area, QList<ShapeId>(),
                                               triangle, Vec2(1.2, 1.4));
    Q_UNUSED(polygonArea);

    // 三点圆弧
    const ShapeId arcId = document->allocateShapeId();
    document->addShape(new ArcShape(arcId, fixture.pointA, pointC, fixture.pointB));

    // 派生线：垂线、平行线、角平分线（都以线段/点为基准）
    const ShapeId footPoint = addPoint(document, 1.0, 2.0);
    const ShapeId perpendicularId = document->allocateShapeId();
    document->addShape(new LineShape(perpendicularId, ShapeType::PerpendicularLine,
                                     fixture.segment, footPoint));
    const ShapeId parallelId = document->allocateShapeId();
    document->addShape(new LineShape(parallelId, ShapeType::ParallelLine,
                                     fixture.segment, pointC));
    const ShapeId bisectorId = document->allocateShapeId();
    document->addShape(new LineShape(bisectorId, fixture.pointA, fixture.pointB, pointC));

    // 派生点：中点与两曲线交点
    const ShapeId midId = document->allocateShapeId();
    document->addShape(new MidPointShape(midId, fixture.pointA, fixture.pointB));
    for (int i = 0; i < 2; ++i) {
        const ShapeId intersectionId = document->allocateShapeId();
        document->addShape(new IntersectionPointShape(intersectionId, fixture.circle, sideOne, i));
    }

    // 附着点：贴在线段上
    const ShapeId attachedId = document->allocateShapeId();
    PointShape *attached = new PointShape(attachedId, PointKind::OnObject, Vec2(1.0, 0.0));
    attached->setHostId(fixture.segment);
    document->addShape(attached);

    // 其余测量：距离、线段长度、角度、点坐标
    QList<ShapeId> distanceInputs;
    distanceInputs << fixture.pointA << pointC;
    fixture.distance = addMeasurement(document, MeasurementKind::Distance, distanceInputs,
                                      kInvalidShapeId, Vec2(-1.0, 2.0));

    fixture.length = addMeasurement(document, MeasurementKind::SegmentLength, QList<ShapeId>(),
                                    sideTwo, Vec2(1.8, 2.6));

    QList<ShapeId> angleInputs;
    angleInputs << fixture.pointB << fixture.pointA << pointC;
    const ShapeId angleId = addMeasurement(document, MeasurementKind::Angle, angleInputs,
                                           kInvalidShapeId, Vec2(-1.0, -1.0));
    Q_UNUSED(angleId);

    QList<ShapeId> coordinateInputs;
    coordinateInputs << pointC;
    fixture.coordinates = addMeasurement(document, MeasurementKind::PointCoordinates,
                                         coordinateInputs, kInvalidShapeId, Vec2(-1.0, 4.6));

    Q_UNUSED(sideOne);
    Q_UNUSED(sideTwo);
    Q_UNUSED(sideThree);

    return fixture;
}

/**
 * @brief 比对两份文档的图元数量与关键测量值。
 * @param original 原文档。
 * @param reloaded 读回的文档。
 * @param fixture 关键图元 id。
 * @param checker 断言计数器。
 */
void compareDocuments(const GeoDocument &original,
                      const GeoDocument &reloaded,
                      const Fixture &fixture,
                      Checker *checker)
{
    checker->check(original.shapeCount() == reloaded.shapeCount(),
                   QStringLiteral("往返后图元总数应一致"),
                   QStringLiteral("原 %1 个，读回 %2 个")
                       .arg(original.shapeCount())
                       .arg(reloaded.shapeCount()));

    GeoDocument *mutableReloaded = const_cast<GeoDocument *>(&reloaded);

    // 圆面积标注：这是"存得下、打不开"缺陷的回归点，必须单独核对
    checker->check(reloaded.shape(fixture.circleArea) != nullptr,
                   QStringLiteral("圆面积标注必须能读回（Area 关联图元允许是圆）"));
    checker->check(nearlyEqual(measurementValue(mutableReloaded, fixture.circleArea),
                               GeoMath::kPi * 9.0),
                   QStringLiteral("读回后圆面积应为 πr² = 9π"),
                   QStringLiteral("实际 %1").arg(measurementValue(mutableReloaded, fixture.circleArea)));

    checker->check(nearlyEqual(measurementValue(mutableReloaded, fixture.radius), 3.0),
                   QStringLiteral("读回后半径应为 3"));
    checker->check(nearlyEqual(measurementValue(mutableReloaded, fixture.circumference),
                               2.0 * GeoMath::kPi * 3.0),
                   QStringLiteral("读回后周长应为 2πr"));
    checker->check(nearlyEqual(measurementValue(mutableReloaded, fixture.distance), 4.0),
                   QStringLiteral("读回后两点距离应为 4"),
                   QStringLiteral("实际 %1").arg(measurementValue(mutableReloaded, fixture.distance)));
    checker->check(nearlyEqual(measurementValue(mutableReloaded, fixture.length), 5.0),
                   QStringLiteral("读回后线段长度应为 5"),
                   QStringLiteral("实际 %1").arg(measurementValue(mutableReloaded, fixture.length)));

    // 附着点：宿主关系必须保留，否则它会变成可以乱跑的自由点
    bool foundAttached = false;
    const QList<Shape *> shapes = reloaded.shapesInOrder();
    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes.at(i)->type() != ShapeType::Point) {
            continue;
        }
        const PointShape *point = static_cast<const PointShape *>(shapes.at(i));
        if (point->kind() == PointKind::OnObject) {
            foundAttached = true;
            checker->check(point->hostId() != kInvalidShapeId,
                           QStringLiteral("读回后附着点必须保留宿主曲线"));
        }
    }
    checker->check(foundAttached, QStringLiteral("读回后应存在附着点"));

    // 拓扑序不变式：读回后依然必须满足"父 id < 自身 id"
    bool orderOk = true;
    for (int i = 0; i < shapes.size(); ++i) {
        const QList<ShapeId> parents = shapes.at(i)->parents();
        for (int k = 0; k < parents.size(); ++k) {
            if (parents.at(k) >= shapes.at(i)->id()) {
                orderOk = false;
            }
        }
    }
    checker->check(orderOk, QStringLiteral("读回后必须满足「父 id < 自身 id」的拓扑序不变式"));
}

} // namespace

/**
 * @brief 入口。
 * @param argc 参数个数。
 * @param argv 参数数组。
 * @return 全部通过返回 0，否则返回 1。
 * @note  只用 QCoreApplication：文件读写属于 service/model 层，
 *        不需要也不应该依赖 QApplication。
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    print(QStringLiteral("==== GeoBoard 文件往返自测 ===="));

    Checker checker;
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        print(QStringLiteral("[FAIL] 无法创建临时目录"));
        return 1;
    }
    const QString path = QDir(tempDir.path()).filePath(QStringLiteral("roundtrip.gboard"));

    GeoDocument original;
    const Fixture fixture = buildFixture(&original);
    const int originalCount = original.shapeCount();

    JsonStore store;
    checker.check(store.save(original, path), QStringLiteral("保存应成功"),
                  QStringLiteral("lastError: %1").arg(store.lastError()));
    checker.check(QFileInfo::exists(path), QStringLiteral("保存后文件应存在"));

    GeoDocument reloaded;
    const bool loaded = store.load(path, &reloaded);
    checker.check(loaded, QStringLiteral("打开应成功"),
                  QStringLiteral("lastError: %1").arg(store.lastError()));

    if (loaded) {
        compareDocuments(original, reloaded, fixture, &checker);

        // 二次往返：读回后再存一次，内容应稳定（防止"每次打开都漂移"）
        const QString secondPath = QDir(tempDir.path()).filePath(QStringLiteral("roundtrip2.gboard"));
        const bool savedAgain = store.save(reloaded, secondPath);
        checker.check(savedAgain, QStringLiteral("读回后再保存应成功"));
        if (savedAgain) {
            QFile first(path);
            QFile second(secondPath);
            const bool readOk = first.open(QIODevice::ReadOnly) && second.open(QIODevice::ReadOnly);
            checker.check(readOk, QStringLiteral("两次保存的文件都应可读"));
            if (readOk) {
                checker.check(first.readAll() == second.readAll(),
                              QStringLiteral("再次保存的文本应与首次完全一致（定点收敛）"));
            }
        }
    }

    // 损坏文件必须被拒绝，且不得破坏已有文档
    {
        const QString brokenPath = QDir(tempDir.path()).filePath(QStringLiteral("broken.gboard"));
        QFile broken(brokenPath);
        if (broken.open(QIODevice::WriteOnly)) {
            broken.write("{\"format\":\"GeoBoard\",\"version\":1,\"shapes\":[{\"id\":1,\"type\":\"point\"}]}");
            broken.close();
        }
        GeoDocument victim;
        buildFixture(&victim);
        const int before = victim.shapeCount();
        const bool rejected = !store.load(brokenPath, &victim);
        checker.check(rejected, QStringLiteral("缺字段的文件应被拒绝"));
        checker.check(victim.shapeCount() == before,
                      QStringLiteral("加载失败时不得破坏原有文档"),
                      QStringLiteral("原 %1 个，之后 %2 个").arg(before).arg(victim.shapeCount()));
        checker.check(!store.lastError().isEmpty(), QStringLiteral("失败时必须给出可读的错误原因"));
    }

    print(QStringLiteral("图元总数（含全部 11 种类型与 8 种测量）：%1").arg(originalCount));
    print(QStringLiteral("---- 往返自测：通过 %1 / 失败 %2 ----")
              .arg(checker.passes())
              .arg(checker.failures()));
    print(QStringLiteral("================================"));
    if (checker.failures() == 0) {
        print(QStringLiteral("结论：全部用例通过"));
        return 0;
    }
    print(QStringLiteral("结论：存在失败项"));
    return 1;
}
