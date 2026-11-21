# Graph Agent Rules

本文件适用于 `graphs/` 及其子目录。

## Python 类型标注

- 跨文件使用的共享组图函数和公开组图 API 必须标注所有参数和返回值的类型，包括 `render_graph_*()` 入口。
- 图直接从 `falcor` 导入 `RenderGraph`，命名端口用 `str` 表达，形如 `"depth.depth"`。
- 每个配置脚本定义一个 `render_graph_*()` 工厂，用带默认值的关键字参数声明选项，并将最终场景颜色标记为首个输出。需要物体拾取的脚本通过执行依赖将拾取 Pass 接在颜色产出之后。
- 可选端口标注为 `str | None`，无返回值的函数标注为 `-> None`。
