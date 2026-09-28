# GeoBoard 命名规范

本项目遵循 QTSkill 的通用命名规范
（`workflow/stages/03_architecture/naming.md`），关键约定速查：

| 种类 | 规范 | 示例 |
|---|---|---|
| 类 / 结构体 / 枚举 | 大驼峰 | `TaskModel`、`Priority` |
| 函数 / 局部变量 | 小驼峰 | `loadFromFile`、`taskCount` |
| 类成员变量 | `m_` + 小驼峰 | `m_taskModel` |
| 静态成员 | `s_` + 小驼峰 | `s_instanceCount` |
| 常量 | `k` + 大驼峰 | `kMaxRetryCount` |
| 取值函数 | `xxx()`，**不加** `get` | `title()` |
| 判断函数 | `isXxx` / `hasXxx` / `canXxx` | `isEmpty()` |
| 槽函数 | `onXxx` 或动词短语 | `onSaveRequested()` |
| 信号 | `xxxChanged` / `xxxRequested` / `xxxFinished` | `saveRequested` |
| 文件名 | 与主类同名的大驼峰 | `MainWindow.h`（`main.cpp` 例外） |

**禁止**：匈牙利前缀（`strName`、`iCount`、`pWidget`）、
自造缩写（`mgr`、`cnt`、`idx`）、
含糊命名（`data`、`temp`、`flag`、`manager`、`util`）。

---

## 本项目专有术语

| 术语 | 含义 | 对应类名 |
|---|---|---|
| （待填写） | | |

## 本项目专有缩写

| 缩写 | 全称 | 说明 |
|---|---|---|
| （如无自造缩写，请明确写"无"，不要留空） | | |

## 与 skill 通用规范的差异

（若确实需要偏离通用规范，必须在此说明原因；没有则写"无"）
