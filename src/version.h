/**
 * @file    version.h
 * @brief   程序版本号。
 * @details 版本号刻意**定义在这里**，而不是通过 qmake 的 DEFINES 传进来。
 *
 *          原因（实测踩过）：`.pro` 里写
 *              DEFINES += APP_VERSION=\"0.1.0\"
 *          时，引号要经过 qmake → 响应文件 → cl.exe 三层转义，最终展开成**裸的**
 *          0.1.0 而不是字符串字面量 "0.1.0"，于是
 *              QStringLiteral(APP_VERSION)   → 语法错误
 *              QString::fromLatin1(APP_VERSION) → 没有可转换的重载
 *          报错位置指向 QString 内部，完全看不出真正的原因是构建参数。
 *
 *          写在头文件里则处处是标准 C++ 字面量，不依赖任何命令行转义规则。
 *          文件名用小写下划线：这是本 skill 明确允许的版本号头文件约定
 *          （见 workflow/stages/05_verify/verification.md 的分级口径）。
 */

#ifndef GEOBOARDVERSION_H
#define GEOBOARDVERSION_H

/** @brief 程序版本号（与 docs/package.md、docs/delivery.md 保持一致）。 */
#define GEOBOARD_VERSION "0.2.1"

#endif // GEOBOARDVERSION_H
