# GCC module 验证

这是独立的轻量复现项目，不接入 RenderLab 的正常构建。

当前 MSYS2 UCRT64 GCC 16.2.0（Rev4）在两个翻译单元调用 module 导出的
`std::format` 模板时，链接失败：

```text
multiple definition of std::__format::__write_escaped_unicode_part<...>::__replace_rep
```

本例保留文本包含的标准库头文件，没有使用 `import std;`；
关闭 module 延迟加载（`-fno-module-lazy`）也未消除错误。
因此仅改为文本包含标准库不足以让当前工具链完成项目迁移。

复现（使用所选 GCC 16.2 工具链，CMake 4.3 和 Ninja）：

```powershell
cmake -S test/module -B test/module/build -G Ninja -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build test/module/build
```

相关上游报告：
[MSYS2 #25869](https://github.com/msys2/MINGW-packages/issues/25869)，
[GCC #125301](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=125301)。

未修改系统 GCC 标准库，也未添加忽略重复符号的链接选项。
