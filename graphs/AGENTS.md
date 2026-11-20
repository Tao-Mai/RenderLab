# Graph Agent Rules

本文件适用于 `graphs/` 及其子目录。

## Python 类型标注

- 跨文件使用的共享组图函数和公开组图 API 必须标注所有参数和返回值的类型，包括 `build_graph()` 入口。
- 图使用 `renderlab_graph` 中的 `RenderGraph`，命名端口用 `str` 表达，形如 `"depth.depth"`。
- 可选端口标注为 `str | None`，无返回值的函数标注为 `-> None`。
