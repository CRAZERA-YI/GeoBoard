/**
 * @file    main.cpp
 * @brief   应用程序入口。
 * @details 只负责初始化 QApplication、组装依赖（视图 + 控制器）并进入事件循环。
 *          业务逻辑一律不写在这里（见 workflow/stages/04_code/code_rule.md）。
 *
 *          组装关系：MainController 持有 Model 与 Service，并只通过公开方法操作 View；
 *          View 不持有任何 Model/Service，也不知道控制器的存在（只发信号）。
 */

#include <QApplication>
#include <QCoreApplication>

#include "controller/MainController.h"
#include "version.h"
#include "view/GeoBoardMainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("FunctionLab"));
    QCoreApplication::setApplicationName(QStringLiteral("GeoBoard"));
    // 版本号来自 src/version.h：不要改成 .pro 的 DEFINES，理由见该文件头的说明
    QCoreApplication::setApplicationVersion(QStringLiteral(GEOBOARD_VERSION));

    GeoBoardMainWindow window;
    MainController controller(&window);

    window.show();
    controller.start();

    // --demo：在空画布上放一个内置演示图形，便于验收与排查
    // （正常启动不带该参数，行为完全不受影响）
    if (QCoreApplication::arguments().contains(QStringLiteral("--demo"))) {
        controller.createDemoConstruction();
    }

    return app.exec();
}
