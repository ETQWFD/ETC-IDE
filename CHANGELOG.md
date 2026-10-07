# 更新说明 · CHANGELOG

官网：https://etqwfd.github.io/ETC-IDE/ ｜ GitHub：https://github.com/ETQWFD/ETC-IDE

## v1.3.0（本版 · 首个正式发布）

### ETC Lang 编译器（come v1.3）

- 完整 8 步编译流水线：扫描源文件 → 词法分析 → 语法分析 → 语义分析（类型 / 权限 / 所有权）→ 代码生成 → 元数据生成 → mingw 编译 EXE → UPX 二次压缩
- 支持中英文标识符、7 种源文件后缀（`.ce .ve .xo .ca .io .ru .ec`）
- 泛型函数 / 结构体 / 类 / 枚举 / trait + impl 注入 / match / try-catch-finally / unsafe / asm 块
- 所有权体系：`own<T>` / `share<T>` / `move` / `ptr<T>` / `ref<T>`，编译期检测已 move 变量
- SIMD 向量类型：`vec2<f32>`、`vec4<i32>`、`vec8<f32>`、`vec16<i32>` 等，原生映射到结构体运算
- `@grant(root)` 生成 requireAdministrator 提权清单，程序生来 root
- `@protect` 注入进程保护（反调试 / 完整性自检）
- `config.ec` 元数据：版本 / 版权 / 公司 / 图标 / UPX / Android 清单
- 示例产物极小：hello 9 KB、demo 13 KB（UPX 压缩后），零运行库依赖
- 报错规范：E1001 作用域 / E1002 类型 / E1003 所有权 / E3001 权限 / W2001 未使用警告

### ETC-IDE（Windows 图形界面）

- Win32 原生界面，约 2200 行源码
- 新建项目向导（Windows EXE / Android APP，可选模板文件）
- 多标签语法高亮编辑器 + 实时语法检查
- 项目树浏览、项目属性面板（产品名 / 公司 / 版权 / 版本 / 图标 / UPX 开关）
- 一键调用内置 `come.exe` 编译，输出窗口实时显示 8 步进度

### 发布内容

- `ETC-IDE-setup-v1.3.0.exe`：NSIS 一键安装程序（IDE + 编译器 + 模板 + 文档）
- `ETC-IDE-portable-v1.3.0.zip`：便携版，解压即用
- 源码：compiler / ide / samples / templates / assets / 文档

---
© 2026 ET ｜ MIT License ｜ https://etqwfd.github.io/ETC-IDE/
