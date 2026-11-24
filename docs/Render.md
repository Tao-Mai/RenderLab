# 渲染模块

[项目首页](../README.md) · [架构](Architecture.md) · [RenderGraph](RenderGraph.md) · [Shader 参数与管线](RenderLayout.md)

渲染模块接收场景快照和编辑器输入。Python 定义 Pass 和资源连接，Renderer 准备 GPU 数据并交由 RenderGraph 调度。

`RenderLabCore` 提供共享的资产系统、GPU 设备和 RenderGraph。[passes](../passes/) 中的实现各自编译成插件，由宿主按构建生成的清单加载。
Pass 的 C++、专用 Slang 和构建声明放在一起。光栅化、后处理、编辑器和降噪分别位于 [raster](../passes/raster/)、[postprocess](../passes/postprocess/)、[editor](../passes/editor/) 和 [denoise](../passes/denoise/)。[raytracing](../passes/raytracing/) 分为 [ReSTIR](../passes/raytracing/realtime/)、[MinimalPathTracer](../passes/raytracing/minimal/)、[离线 PT](../passes/raytracing/pt/) 和 [BDPT](../passes/raytracing/bdpt/)。
[shaders](../shaders/) 存放可独立复用的算法、跨 Pass 使用的数据布局和共享着色阶段。Pass 可以引用共享代码，共享代码只依赖核心接口及其他共享模块。
`material/` 包含材质求值、各具体散射模型、纹理与法线映射，`math/` 包含颜色计算、常量、随机数与采样，`scene/` 包含场景和光源数据。
`raster/` 与 `raytracing/` 分别提供多 Pass 共用的光栅和光追接口，`postprocess/` 提供后处理 Pass 之间的 GPU 数据布局。

各 Pass 的 CMake 文件用 `renderlab_shader()` 声明专用 Shader 的程序名、源文件、入口和宏变体。加载时以程序名选用构建目录生成的变体清单，加载同版编译器生成的 Slang IR 模块。
通用 Slang 可以用 `#include "math/random.slang"`，CPU / GPU 共享头文件可以用 `#include "render/resources/PbrtMaterialProgram.h"`。

场景以 Submesh 粒度的 DrawItem 提交，光栅化直接绘制，光追用同一批条目关联 BLAS、生成 TLAS 实例与命中数据。Renderer 只读引用 DrawItem 列表，材质索引按条目顺序单独保存在数组中。

Renderer 持有 [SceneAccelerationStructures](../src/render/raytracing/SceneAccelerationStructures.cpp)。光追路线在执行渲染图前共用网格 BLAS 缓存，按当前帧槽位构建 TLAS，并在同一命令缓冲中完成构建与光追读取的同步。

## 渲染路线

| 路线 | 主流程 | 用途 |
| --- | --- | --- |
| Forward | DepthPrepass → ScenePass → Skybox → Transparent | 基础 PBR、Clearcoat、Sheen 与透明物体 |
| Deferred | DepthPrepass → GBuffer → DeferredLighting → Skybox → Transparent | 统一材质缓冲与全屏光照 |
| ReSTIR | RayTracingPass → 可选 SVGF | 初始 RIS、时域与空间 reservoir 复用 |
| MinimalPathTracer | MinimalPathTracer → 后处理 | 普通光源采样与 BRDF 路径追踪 |
| PT | PathTracer → 后处理 | 相机路径与直接光采样 |
| BDPT | BDPT → 后处理 | 相机和光源子路径连接、分块累积 |

光栅路线通过 ShadowPass 提供阴影资源，物体拾取通过独立 Pass 使用场景深度。后处理读取场景颜色，光追关闭 HDR 时通过 Blit 输出。UI 在后处理之后绘制。

### 光栅化

不透明前向绘制和透明绘制共用 [forward.slang](../shaders/raster/forward.slang)。延迟路径的入口是 [gbuffer.slang](../passes/raster/GBufferPass/gbuffer.slang) 和 [deferred_lighting.slang](../passes/raster/DeferredLightingPass/deferred_lighting.slang)。网格顶点、表面和阴影的共享接口位于 [shaders/raster](../shaders/raster/)。

深度预通道、GBuffer 和前向绘制通过 [MeshDrawRecorder](../src/render/raster/MeshDraw.h) 录制网格。Pass 提供管线，透明 Pass 提供排好序的绘制项。前向 Pass 缓存每种材质变体和单双面组合对应的管线。
Pass 通过反射声明附件的访问方式，执行时直接构造 RHI 附件描述并调用 `ICommandEncoder::beginRenderPass()`。Graph 管理深度与阴影资源，并通过命名端口把它们交给消费 Pass。关闭阴影时，各阴影端口使用最小的占位资源，光照 shader 跳过阴影采样。

- 深度使用 D32Sfloat，主场景支持不透明、Alpha Mask、双面与排序透明绘制。
- 延迟 G-buffer 保存 Diffuse / AO、Normal / Roughness、Emissive / F90、F0 / Diffuse Fresnel Weight、Sheen / Roughness，并从深度重建位置。
- Forward / Deferred 复用基础 BRDF、灯光、阴影和 IBL；Clearcoat 当前仅在 Forward 计算。
- 透明材质采用前向混合。
- 阴影由统一 [ShadowPass](../passes/raster/ShadowPass/ShadowPass.cpp) 处理，算法见 [阴影文档](Shadow.md)。

### 实时光追

编辑器的 Graph 下拉框可选择 ReSTIR 或 [MinimalPathTracer](MinimalPathTracer.md)。ReSTIR 可接入 [SVGF](../passes/denoise/SVGF/)，MinimalPathTracer 的 `color` 直接接入后处理。Shader 入口分别位于 [ReSTIR](../passes/raytracing/realtime/restir_path_tracer.slang) 和 [MinimalPathTracer](../passes/raytracing/minimal/minimal_path_tracer.slang)。[公共光追 Shader](../shaders/raytracing/) 提供命中处理、路径续射线和光源求值。

各 Pass 选择 shader 入口和输出，并推进自己的采样状态。ReSTIR 的 [reservoir 资源和历史](../passes/raytracing/realtime/RestirResources.cpp) 由插件维护，三个 raygen 的提交顺序和同步写在 Pass 中。实时 Pass 直接创建原生光追管线和 shader table，使用 [RealtimeScene](../src/render/raytracing/RealtimeScene.cpp) 准备并绑定场景。离线 PT 与 BDPT 使用 [OfflineTracePipelineCache](../src/render/raytracing/offline/OfflineTracePipelineCache.cpp) 异步缓存原生管线和 shader table。

- 基础 PBR 复用漫反射与 GGX 函数，使用 VNDF 和完整混合 PDF 生成反弹方向。
- ReSTIR 主命中的直接光使用原版 ReSTIR DI：完整光源样本的初始 RIS → 时域复用 → 一轮空间复用 → 可见性着色。点光、聚光、方向光、矩形、发光三角形和启用的环境共同参与采样。
- ReSTIR 光源候选先确定光源和面内位置，再用实际位置的发光、BRDF 与几何项生成权重；一个 BRDF 方向候选通过 balance MIS 一起进入初始 reservoir。
- 两条实时路线都在 RayGen 中循环追踪续射线和阴影射线，ClosestHit 共用 [path_tracing.slang](../shaders/raytracing/path_tracing.slang) 返回命中与材质信息。光追管线递归深度固定为 1。
- `maxBounces` 控制最多 1–64 次表面散射。ReSTIR 更深层顶点使用初始 RIS 估计直接光，MinimalPathTracer 每个顶点使用一次普通光源采样及 BRDF 续射线。两者均计入间接光，并使用俄罗斯轮盘终止。修改反弹次数后复用渲染图和 Pipeline，ReSTIR 路线会重置时域历史。BLAS 按几何缓存，TLAS 按提交帧更新。
- ReSTIR 关闭 SVGF 时由单层 RGBA16F 的 `color` 输出合并颜色。开启时，`color` 的两层分别保存直接光和间接光，并通过 `emission` 和 `albedo` 输出自发光及主命中材质的反射率 `saturate(diffuseColor + F0)`。两种模式均输出线性深度和法线。
- MinimalPathTracer 的 `color` 合并直接光、间接光与相机直接可见的发光。两条路线均使用固定的主射线像素内采样位置。

两个实时 Pass 都使用 [RealtimeScene](../src/render/raytracing/RealtimeScene.cpp) 上传命中记录，并为发光子网格实例创建三角形任务。Compute 生成每个三角形的 80 B 记录，保存世界空间顶点与边、面积、顶点 UV 和材质索引。缓冲开头的 16 B header 保存有效三角形数量。GpuResources 管理容量增长，RHI 跟踪 Compute 到光追的资源同步；绑定在操作录制时取得当前缓冲。[RTXDI 的发光网格展开方式](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md#mesh-light-processing)

ReSTIR 可通过 `RayTracingPass` 的 `restir` 参数调节采样。默认生成 32 个光源候选和一个 BRDF 候选，启用时域复用及 3 邻居、30 像素半径的一轮空间复用。历史计数限制为当前初始计数的 20 倍。合并时在当前表面重算目标值，按原论文 Algorithm 6 检查来源表面的支持域与可见性并归一化。各阶段使用独立随机流和明确的缓冲读写顺序。[ReSTIR](ReSTIR.md)

相机运动通过上一帧矩阵重投影，检查对象、法线和深度匹配；场景几何、变换、材质、光源、环境或相关配置变化时重置 reservoir 历史。ReSTIR 的采样历史与 SVGF 的颜色历史各自维护，关闭 SVGF 时仍可复用光源样本。

仅在开启 SVGF 时执行 [SVGFReprojection](../passes/denoise/SVGF/SVGFReprojection.h) 和 [SVGFFilter](../passes/denoise/SVGF/SVGFFilter.h)。直接光与间接光先除以各自像素的 albedo，在解调后的空间中累积颜色、亮度矩并估计方差。有效样本数不超过 `historyLength` 时使用算术平均，超过后使用 `Alpha`。首个样本或历史失效时权重为 1。历史不足 4 帧时使用 7×7 空间方差，五级 à-trous 按深度、法线、亮度与方差滤波。第一级回写解调后的颜色历史，最后一级乘回当前像素的 albedo 并添加当前帧 `emission`。除法和乘回统一使用 `max(albedo, 0.01)`，避免黑色通道除零。算法与限制见 [SVGF 文档](SVGF.md)。

### 离线路径追踪

在离线场景的 Graph 下拉框选择 PT 或 BDPT。两个插件各有自己的 RayGen 和 Resolve，通过核心共用场景上传、相机采样、命中处理和累积分配。PBRT 材质、纹理与散射由公共 shader 提供。[PT 入口](../passes/raytracing/pt/main.slang) · [BDPT 入口](../passes/raytracing/bdpt/main.slang) · [PBRT 转换](PbrtConversion.md)

- 使用独立 Pass 和 Shader，活动 Frames-in-flight 为 1。
- 每个样本始终使用 Owen 扰乱 Sobol 序列进行像素内抖动，FP32 缓冲保存颜色总和并记录各像素样本数。BDPT 分块推进，按实际完成的相机样本与发射路径数量分别归一化，持续刷新预览。
- 几何、相机、材质、光源、反弹次数或图重建变化时重新累积，曝光等显示参数调整时保留累积。
- PT 默认两次表面散射，point / spot、distant、area 分别执行蓄水池采样，area 与 BRDF 做 MIS。BDPT 默认六次散射，光源分布、分块进度、splat 和路径 MIS 由 BDPT 插件管理。[BDPT](pbrt/BDPT.md)
- 散射实现见 [PBRT 材质](pbrt/材质.md)，光源和限制见 [PBRT 光源](pbrt/光源.md)。两种算法均通过 Compute Resolve 输出颜色，再接入 HDR 后处理。

## 帧与资源

下述曝光流程对应 `Post process = RenderLab`；`Falcor` 选项使用独立的 [Falcor 后处理](PostProcess.md)。

`ExposurePass` 逐帧使用 `editorCamera.exposure` 或自身的曝光参数。相机设置优先，两处均支持手动和自动模式。色调映射算法由 `ToneMappingPass` 的 `mode` 参数选择。

[ExposurePass](../passes/postprocess/ExposurePass/ExposurePass.h) 在 Bloom 之前读取场景 HDR 颜色。自动模式每隔两个像素测光，将线性 HDR 亮度映射到 256 档对数直方图，按配置裁掉两端百分位后计算代表亮度，并换算为目标 EV100。目标值限制在 EV 范围内，依据帧时间和明暗适应速度进行指数平滑；首次有效测光、切换场景和图重建时直接初始化。全黑画面保留已有曝光，等待首次有效测光。手动模式直接使用配置值。

曝光状态保存在 GPU 缓冲中，由 ToneMappingPass 直接读取并作用于线性 HDR 结果。两个 Pass 共用 [ExposureState](../shaders/postprocess/exposure_state.slang) 的布局。曝光补偿同时适用于两种模式，曝光调整保留离线光照累计。

- 默认两帧在途，离线光追使用一个槽位；FrameContext 以 RHI Fence 等待槽位完成。
- FrameData 保存 Scene / Light / Shadow 常量和环境绑定，MaterialTable 管理材质数据及纹理、采样器数组。
- GpuResources 保留图像、缓冲的稳定 handle、容量与历史版本，Pass 通过 getImage / getBuffer 访问。
- CPU 上传数据由 RHI encoder 复制，原生 staging 分配、资源状态和读写同步由 slang-rhi 管理。
- Renderer 直接获取和呈现 Surface 返回的原生纹理；拾取在提交完成后回读。
- Pass 使用原生 ShaderCursor 按名称设置资源与常量，RHI 在录制时保存 shader object 的参数快照。
- Pass 直接持有原生 GPU 管线；材质变体和离线光追管线使用业务缓存，底层布局由 Slang 反射和 RHI 创建。

## 代码导航

| 位置 | 内容 |
| --- | --- |
| [Renderer.cpp](../src/render/Renderer.cpp) | 图重建、全局帧准备和提交 |
| [graphs/](../graphs/) | 渲染路线与共用组图模块 |
| [graph/](../src/render/graph/) | RenderGraph、Pass 注册与最终出口 |
| [passes/](../passes/) | 各 Pass 的实现与注册 |
| [render/raytracing/](../src/render/raytracing/) | 场景 BLAS / TLAS、底层构建和离线场景准备 |
| [RayTracingPass](../passes/raytracing/realtime/) · [MinimalPathTracer](../passes/raytracing/minimal/) · [PathTracer](../passes/raytracing/pt/) · [BDPT](../passes/raytracing/bdpt/) · [SVGF](../passes/denoise/SVGF/) | 实时与离线光追、时域降噪 |
| [resources/](../src/render/resources/) | GPU 资源、缓存、材质表与 Shader 管理 |
| [device/](../src/render/device/) | 帧提交与同步、资源登记、上传和 GPU 计时 |

CPU Scope、GPU Timestamp、Draw / 三角形计数用于观察各阶段成本。
