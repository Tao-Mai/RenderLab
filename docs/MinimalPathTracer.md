# MinimalPathTracer 实时光追

在编辑器的 Graph 下拉框选择 `MinimalPathTracer`。这张图加载独立的 `MinimalPathTracer` 插件，
由实例的 `Max bounces` 控制弹射上限，图把 `color` 直接接到后处理。[组图入口](../graphs/minimal_path_tracer.py) · [Falcor 的组图示例](https://github.com/NVIDIAGameWorks/Falcor/blob/759aad033ff610fb0d82c74f7e0a508d0096d5f2/scripts/MinimalPathTracer.py)

## 积分流程

MinimalPathTracer 每像素每帧生成一条主射线，并在 RayGen 中循环完成多次弹射：

1. 读取命中几何、材质与法线贴图，记录相机直接可见的自发光、矩形光源或环境。
2. 每个散射顶点普通采样一次光源，追踪可见性射线；支持点光、聚光、方向光、矩形光源、发光三角形及启用的环境。
3. 根据基础 PBR 的漫反射 / GGX 完整混合 PDF 采样一次 BRDF，并用其权重更新路径吞吐量。
4. 光源采样和 BRDF 命中光源采用 balance MIS，避免对同一直接光贡献重复计数。
5. 第 3 次散射起使用俄罗斯轮盘，最多支持 64 次表面散射。最后一次续射线仍评估命中的发光和环境。

`Max bounces = 1` 表示一次表面散射及其直接光，增大数值后加入更深层间接光。
主射线使用固定的像素中心，后续光源和 BRDF 随机数随像素与帧样本变化。
所有追踪都发生在 RayGen，ClosestHit 只返回命中信息，光追管线递归深度维持 1。

MinimalPathTracer 每帧只调度一次 RayGen。ReSTIR 的表面、reservoir 和时空复用由 ReSTIR 插件维护。

## 共用代码与输出

| 位置 | 职责 |
| --- | --- |
| [path_tracing.slang](../shaders/raytracing/path_tracing.slang) | Surface、材质求值、相机射线、命中 / Miss、BRDF 采样、路径续射线和俄罗斯轮盘 |
| [light_sampling.slang](../shaders/raytracing/light_sampling.slang) | 光源选择、解析光源 / 发光三角形 / 环境的求值、光源 PDF 和命中光源样本还原 |
| [minimal_path_tracer.slang](../passes/raytracing/minimal/minimal_path_tracer.slang) | MinimalPathTracer 的路径积分和颜色输出 |

两个插件各自缓存 Pipeline，并复用场景准备、BLAS / TLAS、具名场景参数和 shader table 构建逻辑。切换到 MinimalPathTracer 时重新编译图并重置样本索引。修改弹射上限复用当前 Pipeline。

`color` 是这个 Pass 唯一的图端口，格式为 RGBA16F。它合并直接光、间接光和相机直接可见的自发光。光源和材质通过渲染后端的共享 shader 参数访问。

## 验证入口

`SvgfToggleCheck <scene-id> [size]` 验证两张图之间切换、ReSTIR 的 SVGF 开关，以及 MinimalPathTracer 的单个颜色端口。检查也覆盖 1–3 次弹射。
`CaptureScene` 通过 `realtime` 或 `minimal_path_tracer` 选择 Graph，两条路线可用末尾的 `[max-bounces]` 调节弹射上限。
