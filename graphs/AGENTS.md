# Graph Agent Rules

本文件适用于 `graphs/` 及其子目录。

## Python 类型标注

- 跨文件使用的共享组图函数和公开组图 API 必须标注所有参数和返回值的类型，包括 `build_graph()` 入口。
- 图和资源句柄使用 `renderlab_graph` 中的 `RenderGraph` 和 `ResourceReference`，让 IDE 能解析类型、补全和跳转。
- 可选资源标注为 `ResourceReference | None`，无返回值的函数标注为 `-> None`。
