# MinimalPathTracer 实时光追

在编辑器的 Graph 下拉框选择 `MinimalPathTracer`。这张图加载独立的 `MinimalPathTracer` 插件，
由实例的 `Max bounces` 控制弹射上限，图参数控制可选 SVGF 和后处理。[组图入口](../graphs/minimal_path_tracer.py) · [Falcor 的组图示例](https://github.com/NVIDIAGameWorks/Falcor/blob/759aad033ff610fb0d82c74f7e0a508d0096d5f2/scripts/MinimalPathTracer.py)

## 积分流程

MinimalPathTracer 每像素每帧生成一条主射线，并在 RayGen 中循环完成多次弹射：

1. 读取命中几何、材质与法线贴图，记录相机直接可见的自发光、矩形光源或环境。
2. 每个散射顶点普通采样一次光源，追踪可见性射线；支持点光、聚光、方向光、矩形光源、发光三角形及启用的环境。
3. 根据基础 PBR 的漫反射 / GGX 完整混合 PDF 采样一次 BRDF，并用其权重更新路径吞吐量。
4. 光源采样和 BRDF 命中光源采用 balance MIS，避免对同一直接光贡献重复计数。
5. 第 3 次散射起使用俄罗斯轮盘，最多支持 64 次表面散射。最后一次续射线仍评估命中的发光和环境。

`Max bounces = 1` 表示一次表面散射及其直接光，增大数值后加入更深层间接光。
主射线使用固定的像素中心，后续光源和 BRDF 随机数随像素与帧样本变化。
所有追踪都发生在 RayGen，ClosestHit 只返回命中信息，Vulkan 管线递归深度维持 1。

MinimalPathTracer 每帧只调度一次 RayGen。ReSTIR 的表面、reservoir 和时空复用由 ReSTIR 插件维护。

## 共用代码与输出

| 位置 | 职责 |
| --- | --- |
| [path_tracing.slang](../shaders/raytracing/path_tracing.slang) | Surface、材质求值、相机射线、命中 / Miss、BRDF 采样、路径续射线和俄罗斯轮盘 |
| [light_sampling.slang](../shaders/raytracing/light_sampling.slang) | 光源选择、解析光源 / 发光三角形 / 环境的求值、光源 PDF 和命中光源样本还原 |
| [path_output.slang](../shaders/raytracing/path_output.slang) | 合并 HDR 或 SVGF 分离输出、主命中深度和法线 |
| [minimal_path_tracer.slang](../passes/raytracing/minimal/minimal_path_tracer.slang) | MinimalPathTracer 的路径积分和 RayGen |

两个插件各自缓存 Pipeline，并复用场景准备、BLAS / TLAS、基础描述符和 SBT 构建逻辑。切换 Graph
时重新编译图，样本索引和时域历史重新开始。修改弹射上限复用当前 Pipeline，并重置 SVGF 历史。

MinimalPathTracer 关闭 SVGF 时输出合并的 RGBA16F；开启时输出 direct、indirect、emission、albedo，
随后复用现有 SVGF 重投影与五级 à-trous 滤波。直接光包括主顶点光源采样和一次散射后命中光源的贡献，
更深层贡献归入间接光，相机直接可见的 emission 单独输出。

## 验证入口

`SvgfToggleCheck <scene-id> [size]` 覆盖两张图与 SVGF 开关的反复切换，并切换 1–3 次弹射。
`CaptureScene` 通过 `realtime` 或 `minimal_path_tracer` 选择 Graph，两条路线可用末尾的 `[max-bounces]` 调节弹射上限。
