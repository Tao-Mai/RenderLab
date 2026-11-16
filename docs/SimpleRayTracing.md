# Simple 实时光追

在 `Real-time ray tracing` 渲染路线下，通过 `Tracing algorithm` 下拉框切换 `ReSTIR / Simple`。
选择保存在 `feature.rayTracing.algorithm`，默认仍为 `ReSTIR`。两条子路线共用 `Max bounces` 和
SVGF 参数，ReSTIR 的候选与时空复用设置仅在选择 ReSTIR 时显示。

## 积分流程

Simple 每像素每帧生成一条主射线，并在 RayGen 中循环完成多次弹射：

1. 读取命中几何、材质与法线贴图，记录相机直接可见的自发光、矩形光源或环境。
2. 每个散射顶点普通采样一次光源，追踪可见性射线；支持点光、聚光、方向光、矩形光源、发光三角形及启用的环境。
3. 根据基础 PBR 的漫反射 / GGX 完整混合 PDF 采样一次 BRDF，并用其权重更新路径吞吐量。
4. 光源采样和 BRDF 命中光源采用 balance MIS，避免对同一直接光贡献重复计数。
5. 第 3 次散射起使用俄罗斯轮盘，最多支持 64 次表面散射。最后一次续射线仍评估命中的发光和环境。

`Max bounces = 1` 表示一次表面散射及其直接光，增大数值后加入更深层间接光。
主射线使用固定的像素中心，后续光源和 BRDF 随机数随像素与帧样本变化。
所有追踪都发生在 RayGen，ClosestHit 只返回命中信息，Vulkan 管线递归深度维持 1。

Simple 不分配、上传或复用 ReSTIR 的表面、初始 reservoir、历史 reservoir 和帧参数缓冲，
也不执行 Temporal / Spatial RayGen。每帧只调度一次 Simple RayGen。

## 共用代码与输出

| 位置 | 职责 |
| --- | --- |
| `shaders/raytracing/realtime/ray_tracing.slang` | Surface、材质求值、相机射线、命中 / Miss、BRDF 采样、路径续射线、俄罗斯轮盘 |
| `shaders/raytracing/realtime/light_sampling.slang` | 光源选择、解析光源 / 发光三角形 / 环境的求值、光源 PDF、命中光源样本还原 |
| `shaders/raytracing/realtime/output.slang` | 合并 HDR 或 SVGF 分离输出、主命中深度和法线 |
| `shaders/raytracing/realtime/simple/` | Simple 路径积分与入口 |
| `shaders/raytracing/realtime/restir/` | ReSTIR 初始 RIS、路径循环、时空复用与专用数据 |

两条子路线复用 `RayTracingScenePass`、BLAS / TLAS、`RayTracingPass` 的描述符和 SBT 构建逻辑，
分别缓存算法 × SVGF 的 Pipeline 变体。切换算法重建渲染图，重置样本索引和 SVGF / ReSTIR 历史；
修改反弹上限复用现有 Pipeline，并使 SVGF 历史失效。

Simple 关闭 SVGF 时输出合并的 RGBA16F；开启时输出 direct、indirect、emission、albedo，
随后复用现有 SVGF 重投影与五级 à-trous 滤波。直接光包括主顶点光源采样和一次散射后命中光源的贡献，
更深层贡献归入间接光，相机直接可见的 emission 保持独立。Simple 不使用 ReSTIR 配置来判断 SVGF 历史。

## 验证入口

`SvgfToggleCheck <scene-id> [size]` 覆盖两条子路线 × SVGF 开关的反复切换，并切换 1–3 次弹射。
`CaptureScene` 的实时路线支持末尾两个参数 `[restir|simple] [max-bounces]`，可读取 HDR 结果作数值对照。
