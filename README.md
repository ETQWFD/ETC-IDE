# ETC Lang · ETC IDE

> 一门用 C++ 编写、自带编译器的极限难度系统级编程语言，程序天生 root，极速编译、完整进程保护。
> 作者：**etc** ｜ 版权：**ET**

---

## 简介

**ETC Lang** 是一套完整的编程语言 + 编译器 + 集成开发环境：

| 组件 | 说明 |
| --- | --- |
| `come` | ETC Lang 编译器命令行（Linux / Windows 双平台），一键把 `.ce` 项目编译成 Windows EXE / Android APK |
| `ETC-IDE` | Windows 图形化集成开发环境：语法高亮编辑器、实时语法检查、项目树、属性面板、一键编译 |
| 运行时 | 编译产物自动注入进程保护（`@protect`）、提权清单（`@grant`）、版本/版权元数据（`version.rc`），并用 UPX 二次压缩 |

### 特性

- **中英文标识符**：`let 难度值 = 3;`、`fn 最大值<T>(...)` 均可直接书写
- **7 种源文件后缀**：`.ce .ve .xo .ca .io .ru .ec`，按模块职责拆分工程
- **完整语法体系**：泛型、`match`、`unsafe`/`asm` 块、`impl Trait for Type`、`try/catch/finally`、C 风格 `for`、`for x in` 迭代
- **所有权系统**：`own<T>` / `share<T>` / `move` / `ptr<T>` / `ref<T>`，编译期检查已 move 变量
- **SIMD 向量**：`vec2<f32>` `vec4<i32>` `vec8<f32>` … 原生映射到 SSE/NEON 结构体运算
- **权限声明**：`@grant(root)` 生成 `requireAdministrator` 提权清单，程序**生来 root**
- **进程保护**：`@protect` 注入反调试/完整性自检
- **元数据**：`config.ec` 一键生成 `version.rc` / `app.manifest` / `AndroidManifest.xml`
- **零依赖产物**：编译出的 EXE 极小（示例 9~13 KB，UPX 后），无运行库要求

---

## 快速开始

### 1. 从源码构建编译器（Linux）

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)        # 产出 build/come
```

### 2. 编译一个 ETC 项目（Windows EXE）

```bash
./come demo.exe --proj samples/demo
```

输出 8 步流水线：扫描源文件 → 词法 → 语法 → 语义（类型/权限/所有权）→ 代码生成 → 元数据（version.rc / manifest）→ mingw 编译 EXE → UPX 二次压缩。

### 3. 使用 ETC-IDE（Windows）

运行 `ETC-IDE.exe`：

- **新建项目**：向导创建 Windows EXE / Android APP 工程（`main.ce` + `config.ec` + `ui.ca` + `utils.ru`）
- **编辑**：多标签语法高亮编辑器，实时语法检查
- **编译**：一键调用内置 `come.exe` 产出 EXE/APK
- **属性**：修改产品名、公司、版权、版本、图标、UPX 等

---

## 项目结构

```
ETC-IDE/
├── compiler/            # ETC Lang 编译器源码（Lexer / Parser / Semantic / CodeGen）
│   ├── lexer.h/.cpp     # 词法分析：中英文标识符、全量关键字、@注解
│   ├── ast.h            # AST 节点定义
│   ├── parser.h/.cpp    # 递归下降语法分析
│   ├── semantic.h/.cpp  # 语义分析：类型 E1002 / 权限 E3001 / 所有权 E1003
│   ├── codegen.h/.cpp   # 代码生成：ETC→C++、SIMD、@protect、@grant、元数据
│   └── main.cpp         # come 命令行入口（8 步编译流水线）
├── ide/                 # ETC-IDE Windows 图形界面源码（Win32）
│   ├── ide_main.cpp     # 主程序（约 2200 行）
│   └── ide.rc           # 对话框资源（新建项目向导 / 项目属性）
├── samples/             # 示例工程
│   ├── hello/           # 最小 Hello World
│   ├── demo/            # 全特性演示（泛型/所有权/SIMD/异常/trait）
│   └── errors/          # 报错演示（编译期错误格式）
├── templates/           # 项目模板
│   ├── windows_project/ # Windows EXE 模板（main.ce / config.ec / ui.ca / utils.ru）
│   └── android_project/ # Android APP 模板
├── assets/              # 品牌图标（ETC-IDE.ico / PNG）
├── build/               # 构建产物（come / ETC-IDE.exe）
├── CMakeLists.txt       # CMake 构建脚本（C++17）
└── LICENSE              # MIT License © ET
```

---

## 文档约定

- 文件版本 / 产品版本、公司名（ET）、版权（Copyright (c) ET 2026）统一由 `config.ec` 控制
- 编译错误码：`E1001` 作用域 / `E1002` 类型 / `E1003` 所有权 / `E3001` 权限 / `E9003` 链接失败

## 许可

本项目以 **MIT License** 发布，版权归 **ET** 所有。详见 [LICENSE](LICENSE)。

---
**ETC Lang · 生来 root · 极限性能 · 完整进程保护**
