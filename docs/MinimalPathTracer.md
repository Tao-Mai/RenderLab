# MinimalPathTracer 实时光追

MinimalPathTracer 是场景渲染入口。编辑器中的 Max bounces 控制弹射上限。内置渲染图把 HDR 颜色接入后处理。 [组图实现](../src/render/impl/graphs/minimal/MinimalPathTracerGraph.cpp)

Graph 面板中的 **Enable accumulation** 默认开启。它在后处理前对线性颜色逐帧取平均。相机、场景、渲染尺寸或路径追踪设置变化时，从下一帧重新累积。关闭后直接使用当前帧的路径追踪结果。 [累积实现](../src/render/impl/passes/accumulate/AccumulatePass.cpp)

## 积分流程

VBufferPass 每像素追踪一条主射线，保存首个通过 alpha test 的实例、图元和重心坐标。MinimalPathTracer 从 `vbuffer` 重建首撞，并在 RayGen 中循环完成后续弹射：

1. 读取命中几何、材质与法线贴图，记录相机直接可见的自发光、矩形光源或环境。
2. 每个散射顶点普通采样一次光源，追踪可见性射线；支持点光、聚光、方向光、矩形光源、发光三角形及启用的环境。
3. 根据基础 PBR 的漫反射 / GGX 完整混合 PDF 采样一次 BRDF，并用其权重更新路径吞吐量。
4. 光源采样和 BRDF 命中光源采用 balance MIS，避免对同一直接光贡献重复计数。
5. 第 3 次散射起使用俄罗斯轮盘，最多支持 64 次表面散射。最后一次续射线仍评估命中的发光和环境。

`Max bounces = 1` 表示一次表面散射及其直接光，增大数值后加入更深层间接光。
主射线使用固定的像素中心，后续光源和 BRDF 随机数随像素与帧样本变化。
所有追踪都发生在 RayGen，ClosestHit 只返回命中信息，光追管线递归深度维持 1。
散射和可见性射线分别使用槽位 0 和 1。C++ 的 Shader Table 和 Slang 的追踪入口共享 `RayType` 索引。 [射线类型](../src/render/resources/RayType.h)


## 共用代码与输出

| 位置 | 职责 |
| --- | --- |
| [path_tracing.slang](../src/render/impl/raytracing/path_tracing.slang) | Surface、材质求值、相机射线、命中 / Miss、BRDF 采样、路径续射线和俄罗斯轮盘 |
| [Scene.slang](../src/render/impl/Scene/Scene.slang) · [LightCollection.slang](../src/render/impl/Scene/Lights/LightCollection.slang) | 公共场景光源数据与 `scene.getLightCount()`、`emissiveLights.getTriangleCount()` 数量查询 |
| [LightHelpers.slang](../src/render/impl/Rendering/Lights/LightHelpers.slang) | 单个解析光源的入射辐射、方向、距离和条件 PDF |
| [SampleGeneratorInterface.slang](../src/render/impl/Utils/Sampling/SampleGeneratorInterface.slang) · [UniformSampleGenerator.slang](../src/render/impl/Utils/Sampling/UniformSampleGenerator.slang) | `ISampleGenerator`、取样辅助函数与当前的均匀随机数生成器 |
| [EmissiveLightSamplerInterface.slang](../src/render/impl/Rendering/Lights/EmissiveLightSamplerInterface.slang) · [EmissiveUniformSampler.slang](../src/render/impl/Rendering/Lights/EmissiveUniformSampler.slang) | 发光网格采样接口与均匀三角形采样策略，提供采样、三角形选择概率和命中点 PDF |
| [EmissiveLightSamplerHelpers.slang](../src/render/impl/Rendering/Lights/EmissiveLightSamplerHelpers.slang) | 发光三角形的辐射求值、单双面判断和三角形位置采样辅助函数 |
| [EnvMapSampler.slang](../src/render/impl/Rendering/Lights/EnvMapSampler.slang) | 环境光的方向采样、辐射求值与方向 PDF；当前使用球面均匀采样 |
| [path_tracer_lighting.slang](../src/render/impl/passes/shared/path_tracer_lighting.slang) | Pass 共用的自由辅助函数，完成光源类别选择、直接光估计和发光命中 MIS 权重计算 |
| [minimal_path_tracer.slang](../src/render/impl/passes/minimal/minimal_path_tracer.slang) | MinimalPathTracer 的路径积分和颜色输出 |


MinimalPathTracer 读取 `vbuffer`，输出 RGBA32F `color`。它合并直接光、间接光和相机直接可见的自发光。光源和材质通过渲染后端的共享 shader 参数访问。

公共光源接口按 [Falcor](https://github.com/NVIDIAGameWorks/Falcor/tree/master/Source/Falcor/Rendering/Lights) 的职责边界拆分：解析光源使用独立辅助函数，环境光由 `EnvMapSampler` 提供 `sample`、`eval` 和 `evalPdf`，发光网格策略实现 `IEmissiveLightSampler`。三个接口都不依赖 `Surface`，返回的辐射尚未除以 PDF。类别选择和 MIS 属于 Pass 内部的积分策略；Minimal 和 ReSTIR 在调用 `passes/shared` 的自由辅助函数时显式传入环境光和发光网格采样器，当前发光网格策略为 `EmissiveUniformSampler`。

解析光源的统一入口为 `sampleLight<S : ISampleGenerator>(origin, light, inout generator, out sample)`。调用方传入光源数据和随机数生成器，入口按当前支持的类型分发到 `samplePointLight`、`sampleDirectionalLight` 或 `sampleRectAreaLight`：点光源和方向光不取随机数，矩形面光源在入口内通过 `sampleNext2D` 生成采样位置。

`ISampleGenerator` 只约定 `[mutating] uint next()`；`sampleNext1D/2D/3D` 将随机整数转换成 `[0, 1)` 中的样本。目前只有 `UniformSampleGenerator` 实现该接口，保留 SplitMix64 初始化和 xoshiro128** 随机序列。`Utils/Sampling/SampleGenerator.slang` 根据 `SAMPLE_GENERATOR_TYPE` 为 Pass 提供 `SampleGenerator` 类型。公共解析光源和发光网格采样接口通过 `S : ISampleGenerator` 接收生成器，不依赖该具体类型。

数量通过场景和光源集合对象的成员方法查询，分别返回解析光源数和发光三角形数，不读取 Pass 的采样开关。`USE_ANALYTIC_LIGHTS`、`USE_EMISSIVE_LIGHTS` 以及参与采样的类别数量由 `passes/shared` 中的积分策略处理。

公共模块使用与 Falcor 对应的目录和文件名：场景入口为 `Scene/Scene.slang`，相机数据位于 `Scene/Camera/`，光源数据与三角形更新 shader 位于 `Scene/Lights/`，光源采样接口、策略和辅助函数位于 `Rendering/Lights/`，随机数生成器位于 `Utils/Sampling/`。

编辑器 **debug → Show analytic lights** 开启灯光标记。DebugPass 从 VBuffer 重建标记像素的几何深度，挡在墙后的标记会被遮挡。整张图输出 `debug.color`。 [DebugPass](../src/render/impl/passes/debug/DebugPass.cpp)

## 验证

`CaptureScene` 通过 `realtime` 或 `minimal_path_tracer` 选择 Graph，两条路线可用末尾的 `[max-bounces]` 调节弹射上限。

启动 RenderLab 后，可在预览场景的列表中选择光源和对象，再编辑其属性或变换。
