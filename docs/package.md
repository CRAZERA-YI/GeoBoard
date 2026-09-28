# GeoBoard 打包说明

> 阶段 08 产物。记录 Release 构建、windeployqt 收集、交付目录构成与验证证据。
> 命令与结论均为本机实测（2026-09-27），不是模板照抄。

## 1. 构建环境

| 项 | 值 |
|---|---|
| Qt 版本 | 5.12.12，kit `msvc2017_64`（构建于 v141 ABI） |
| 编译器 | MSVC 工具集 14.44.35207（v143，VS 2022 Community） |
| Windows SDK | 10.0.26100.0 |
| 架构 | x64 |
| 构建类型 | Release（qmake 默认即 Release） |
| 构建机 | Windows x64，屏幕 2560×1600 @200% 缩放 |

`qt_deploy_check.py --exe dist\GeoBoard.exe` 结论：`OK=7 INFO=1 WARN=0 FAIL=0`，
**架构一致性校验通过**（`GeoBoard.exe 为 x64，与 kit 一致`）。唯一 INFO 是
"未安装安装包制作工具"，本期不打安装包，不影响交付。

## 2. 构建与打包命令（一条命令完成）

项目内已把 01 阶段验证过的命令固化成 `scripts\build.bat`（刻意写成纯 ASCII：
含中文的批处理会被 cmd.exe 按 GBK 代码页读取，在多字节字符处截断命令行）：

```bat
scripts\build.bat package
```

它等价于依次执行：

```bat
:: 1. Release 构建（vcvars 必须与 qmake/nmake 在同一个 cmd 进程里）
call "D:\Visual Studio\Visual Studio 2022\Community\VC\Auxiliary\Build\vcvars64.bat"
mkdir build-release && cd build-release
"D:\Qt\Qt5.12.12\5.12.12\msvc2017_64\bin\qmake.exe" ..\GeoBoard.pro
nmake

:: 2. 收集运行库
mkdir dist
copy build-release\release\GeoBoard.exe dist\
"D:\Qt\Qt5.12.12\5.12.12\msvc2017_64\bin\windeployqt.exe" ^
    --release --no-translations --no-system-d3d-compiler --no-opengl-sw dist\GeoBoard.exe
```

**为什么必须 Release**：Debug 版依赖 `Qt5Cored.dll` 等调试版 DLL，而 Qt 官方安装包
默认不包含它们，交付 Debug 版会导致目标机必然启动失败。

**为什么不交付单个 exe**：目标机没有 Qt 平台插件，单文件必然报
`This application failed to start because no Qt platform plugin could be initialized.`
交付物是**整个目录**。

## 3. 交付目录构成（实测清单）

目录名 `GeoBoard-0.1.0-win64\`，共 30 个条目：

| 文件/目录 | 说明 | 是否必需 |
|---|---|---|
| `GeoBoard.exe` | 主程序（217,600 字节） | 必需 |
| `Qt5Core.dll` / `Qt5Gui.dll` / `Qt5Widgets.dll` | Qt 核心模块 | 必需 |
| `Qt5Svg.dll` | SVG 支持（windeployqt 按依赖自动收集） | 随包 |
| `platforms\qwindows.dll` | **平台插件，缺它必然启动失败** | **必需** |
| `styles\qwindowsvistastyle.dll` | 经典 Windows 原生样式，缺它会失去系统主题外观 | 必需 |
| `imageformats\*.dll` | 图片格式插件（gif/ico/jpeg/svg/tga/tiff/wbmp/webp） | 随包 |
| `iconengines\qsvgicon.dll` | 图标引擎 | 随包 |
| `libEGL.dll` / `libGLESV2.dll` | ANGLE（Qt GUI 的 OpenGL ES 后备实现） | 随包 |
| `msvcp140*.dll`、`vcruntime140*.dll`、`concrt140.dll`、`vccorlib140.dll` | MSVC v143 运行库 | 必需（已随包） |
| `vc_runtime\vcredist_msvc2019_x64.exe` | 官方运行时安装包，供"不用随包 DLL"的场合 | 可选 |
| `README-运行说明.txt` | 给最终用户的运行说明（与开发者 README 不同） | 建议（已提供） |

**关于 MSVC 运行时（实测发现的问题）**：打包后启动程序并查询已加载模块，
发现 `VCRUNTIME140.dll` / `MSVCP140.dll` / `VCRUNTIME140_1.dll` 是从
`C:\Windows\System32` 加载的（本机 System32 版本为 14.51，比可再分发的 14.44 更新），
也就是说**开发机是靠系统已安装的运行库才跑起来的**。目标机未必有，因此从 VS 的
`VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT\` 复制全部运行时 DLL 随包分发；
再次启动并核对，三者都改为从 `dist\` 加载：

```
VCRUNTIME140_1.dll  <项目>\dist\VCRUNTIME140_1.dll
MSVCP140.dll        <项目>\dist\MSVCP140.DLL
VCRUNTIME140.dll    <项目>\dist\VCRUNTIME140.DLL
```

同时把 Qt 安装目录自带的 `vcredist_msvc2019_x64.exe` 放进 `vc_runtime\` 作为备选方案。

## 4. 压缩包

```powershell
Compress-Archive -Path GeoBoard-0.1.0-win64 -DestinationPath GeoBoard-0.1.0-win64.zip
```

实测：`GeoBoard-0.1.0-win64.zip`，**24.95 MB**，30 个条目，
解压后根目录为 `GeoBoard-0.1.0-win64\`，双击其中的 `GeoBoard.exe` 即可运行。

## 5. 目标机要求

- Windows 10 / 11 x64（Qt 5.12 亦支持 Windows 7 SP1，但本项目未在该系统验证）
- **无需预装 Qt**
- MSVC 运行时：随包已带 DLL；若目标机策略禁止使用本地 DLL，可运行
  `vc_runtime\vcredist_msvc2019_x64.exe` 安装官方运行库（需管理员权限）
- 不需要管理员权限即可运行

**用户数据位置**：本版本**不写任何配置文件**（不使用 `QSettings`），
所有文件路径都由用户在"打开/保存"对话框中选择，因此不存在"写入 `Program Files`
导致权限失败"的问题。

## 6. 验证记录（**如实记录，含未完成项**）

| 项 | 结果 | 证据 |
|---|---|---|
| Release 构建成功 | 通过 | `build-release\release\GeoBoard.exe`，217,600 字节 |
| windeployqt 收集成功 | 通过 | 退出码 0，生成 30 个条目 |
| `platforms\qwindows.dll` 存在 | 通过 | 实测存在 |
| `styles\qwindowsvistastyle.dll` 存在 | 通过 | 实测存在 |
| 包内无调试版 DLL | 通过 | 无 `Qt5Cored.dll` 之类文件 |
| 包内无 `.pdb` | 通过 | 无 `.pdb` 文件 |
| 架构一致性 | 通过 | `qt_deploy_check.py --exe` 判定 x64 与 kit 一致 |
| 打包产物能启动 | 通过 | 从 `dist\` 启动 `GeoBoard.exe --demo`，进程存活、窗口标题为 `未命名 — GeoBoard` |
| 运行库从包内加载 | 通过 | 已加载模块路径全部指向 `dist\` |
| 界面为经典 Windows 风格 | 通过 | 截图 `docs/screenshot-demo.png`；未设置 Fusion、未硬编码颜色 |
| 中文显示正常 | 通过 | 截图中菜单、工具栏、标注均为正常中文 |
| 200% 系统缩放下界面正常 | 通过 | 构建机即为 200%（logicalDpi=192），截图无裁剪错位 |
| **干净环境（沙盒/虚拟机）验证启动** | **未执行** | 见下方说明 |
| 150% 缩放 | 未执行 | 仅在 200% 下验证过 |
| 非管理员账户运行 | 未执行 | 需在受限账户下验证 |

> **诚实说明**：上表最后三项是 08 阶段完成判据里要求的"干净环境验证"，
> 本次**没有完成**。在开发机上验证只能证明"包内依赖齐全"，不能证明
> "在没有 Qt 的机器上一定能跑"。请在 Windows Sandbox 中解压本 zip 后双击做一次最终确认。

## 7. 调试符号（可选）

Release 构建默认不产出 pdb。若希望目标机崩溃时能定位到行号，可归档
`<构建目录>\release\GeoBoard.pdb`，或用 `CONFIG+=force_debug_info` 重新构建。
本项目当前**未随包提供 pdb**。

## 8. 已知问题

| 问题 | 说明 |
|---|---|
| 首次启动可能略慢 | 杀毒软件扫描新 exe 导致，非程序问题 |
| 圆弧只支持三点弧 | 见 `docs/requirements.md` 未决问题；不影响打包 |
| 图像导出未实现 | 需求中列为"明确不做"，故包内不含相关功能 |
