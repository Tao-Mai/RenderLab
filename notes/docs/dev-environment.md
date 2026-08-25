# 开发环境（clangd / CMake）

## clangd 为什么不自动更新？

`compile_commands.json` **只在 CMake configure 时生成**，普通 `cmake --build` 不会改它。  
新增/删除 `.cpp`、改 `CMakeLists.txt` 后必须 **先 configure**，clangd 才能拿到新编译参数。

## 本项目已做的自动化

| 机制 | 作用 |
|------|------|
| `cmake.configureOnEdit` | 保存 `CMakeLists.txt` 后自动 configure |
| `cmake.copyCompileCommands` | configure 成功后复制 `compile_commands.json` 到项目根 |
| CMakeLists `file(COPY ...)` | 同上，双保险（Windows 不用符号链接） |
| `.clangd` → `CompilationDatabase: .` | clangd 固定读根目录数据库 |
| 默认构建任务 `dependsOn: configure` | Ctrl+Shift+B 会先 configure 再 build |

## 日常操作

1. **改头文件 / 实现**（不动 CMake）：保存即可，clangd 会重新解析当前 TU
2. **新增源文件 / 改 CMakeLists**：保存 CMakeLists → 等状态栏 configure 完成（或手动运行 **CMake: Configure**）
3. **仍显示旧红线**：命令面板 → `clangd: Restart language server`

## 检查是否生效

项目根应有 `compile_commands.json`（已在 `.gitignore`，本地生成）。  
修改 `CMakeLists.txt` 并 configure 后，该文件时间戳应更新。

## 避免冲突

- 已禁用 Microsoft C/C++ IntelliSense（`C_Cpp.intelliSenseEngine: disabled`）
- 不要同时用 `cmake-build-default` 和 `build` 两套目录；本项目 preset 固定 `build/`
