# 渲染模块

[项目首页](../README.md) · [架构](Architecture.md) · [RenderGraph](RenderGraph.md) · [Descriptor / Pipeline Layout](RenderLayout.md)

渲染模块接收场景快照和编辑器输入。Python 定义 Pass 和资源连接，Renderer 准备 GPU 数据并交由 RenderGraph 调度。

`RenderLabCore` 提供资产、GPU 设备和 RenderGraph。应用将 [passes](../passes/) 里的实现编译为 `RenderLabPasses` 并注册。
每个 Pass 的 C++、专用 Slang 和构建声明放在以注册名命名的目录中。光栅化 Pass 位于 [passes/raster/](../passes/raster/)，后处理 Pass 位于 [passes/postprocess/](../passes/postprocess/)，其余 Pass 直接位于 `passes/` 下。
[shaders](../shaders/) 存放可独立复用的算法、跨 Pass 使用的数据布局和共享着色阶段。Pass 可以引用共享代码，共享代码只依赖核心接口及其他共享模块。
`material/` 包含材质求值、各具体散射模型、纹理与法线映射，`math/` 包含颜色计算、常量、随机数与采样，`scene/` 包含场景和光源数据。
`raster/` 与 `raytracing/` 分别提供多 Pass 共用的光栅和光追接口，`postprocess/` 提供后处理 Pass 之间的 GPU 数据布局。

各 Pass 的 CMake 文件用 `renderlab_shader()` 声明专用 Shader 的程序名、源文件、入口和宏变体。加载时以程序名选用构建目录生成的变体清单，或直接读取对应的 SPIR-V。
通用 Slang 可以用 `#include "math/random.slang"`，CPU / GPU 共享头文件可以用 `#include "render/raytracing/RayTracingBindings.h"`。

场景以 Submesh 粒度的 DrawItem 提交，光栅化直接绘制，光追用同一批条目关联 BLAS、生成 TLAS 实例与命中数据。Renderer 只读引用 DrawItem 列表，材质索引按条目顺序单独保存在数组中。

Renderer 持有 [SceneAccelerationStructures](../src/render/raytracing/SceneAccelerationStructures.cpp)。两条光追路线在执行渲染图前共用网格 BLAS 缓存，按当前帧槽位构建 TLAS，并在同一命令缓冲中完成构建与光追读取的同步。

## 四条路线

| 路线 | 主流程 | 用途 |
| --- | --- | --- |
| Forward | DepthPrepass → ScenePass → Skybox → Transparent | 基础 PBR、Clearcoat、Sheen 与透明物体 |
| Deferred | DepthPrepass → GBuffer → DeferredLighting → Skybox → Transparent | 统一材质缓冲与全屏光照 |
| Ray Tracing | RayTracingScene → RayTracing → 可选 SVGF | 发光三角形准备、分路累积、方差估计与多尺度滤波；末级直接合成 |
| Offline | OfflinePathTracing（PT / BDPT）→ Resolve | PBRT 场景的渐进式离线渲染 |

光栅路线在需要时加入 ShadowPass，物体拾取通过独立阶段使用场景深度。后处理读取场景颜色，光追关闭 HDR 时通过 Blit 输出。UI 在后处理之后绘制。

### 光栅化

不透明前向绘制和透明绘制共用 [forward.slang](../shaders/raster/forward.slang)。延迟路径的入口是 [gbuffer.slang](../passes/raster/GBufferPass/gbuffer.slang) 和 [deferred_lighting.slang](../passes/raster/DeferredLightingPass/deferred_lighting.slang)。网格顶点、表面和阴影的共享接口位于 [shaders/raster](../shaders/raster/)。

深度预通道、GBuffer 和前向绘制通过 [MeshDrawRecorder](../src/render/raster/MeshDraw.h) 录制网格。Pass 提供管线，透明 Pass 提供排好序的绘制项。前向 Pass 缓存每种材质变体和单双面组合对应的管线。
每个 Pass 使用同一份 [RasterAttachments](../src/render/raster/RasterAttachments.h) 同步声明 Graph 访问和配置 Vulkan 动态渲染。Graph 直接连接深度与阴影资源的生产者和消费者，关闭阴影的图共享一份 [ShadowResources](../src/render/raster/ShadowResources.h) 占位资源。

- 深度使用 D32Sfloat，主场景支持不透明、Alpha Mask、双面与排序透明绘制。
- 延迟 G-buffer 保存 Diffuse / AO、Normal / Roughness、Emissive / F90、F0 / Diffuse Fresnel Weight、Sheen / Roughness，并从深度重建位置。
- Forward / Deferred 复用基础 BRDF、灯光、阴影和 IBL；Clearcoat 当前仅在 Forward 计算。
- 透明材质采用前向混合。
- 阴影由统一 [ShadowPass](../passes/raster/ShadowPass/ShadowPass.cpp) 处理，算法见 [阴影文档](Shadow.md)。

### 实时光追

实时光追下可通过 `Tracing algorithm` 选择 `ReSTIR` 或 [MinimalPathTracer](MinimalPathTracer.md)，默认保留 ReSTIR。
Shader 入口分别是 [restir_path_tracer.slang](../passes/RayTracingPass/restir_path_tracer.slang) 与
[minimal_path_tracer.slang](../passes/RayTracingPass/minimal_path_tracer.slang)。两条子路线共用
[RayTracingPass](../passes/RayTracingPass/) 中的命中处理、路径续射线和光源求值，以及 [SVGF](../passes/SVGF/) 中的滤波流程。

- 基础 PBR 复用漫反射与 GGX 函数，使用 VNDF 和完整混合 PDF 生成反弹方向。
- ReSTIR 子路线主命中的直接光使用原版 ReSTIR DI：完整光源样本的初始 RIS → 时域复用 → 一轮空间复用 → 可见性着色。点光、聚光、方向光、矩形、发光三角形和启用的环境共同参与采样。
- ReSTIR 光源候选先确定光源和面内位置，再用实际位置的发光、BRDF 与几何项生成权重；一个 BRDF 方向候选通过 balance MIS 一起进入初始 reservoir。
- 两条子路线都在 RayGen 中循环追踪续射线和阴影射线，ClosestHit 共用 [ray_tracing.slang](../passes/RayTracingPass/ray_tracing.slang) 返回命中与材质信息。Vulkan 管线递归深度固定为 1。
- `maxBounces` 控制最多 1–64 次表面散射。ReSTIR 更深层顶点使用初始 RIS 估计直接光，MinimalPathTracer 每个顶点使用一次普通光源采样及 BRDF 续射线；两者均计入间接光，并使用俄罗斯轮盘终止。修改反弹次数后复用渲染图和 Pipeline，并重置时域历史。BLAS 按几何缓存，TLAS 按提交帧更新。
- 关闭 SVGF 时输出 RGBA16F 合并颜色；开启时分别输出直接光、间接光、`emission` 与主命中材质的反射率 `saturate(diffuseColor + F0)`。两种模式均输出原始线性深度与法线，主射线使用固定的像素内采样位置。
- `emission` 保存相机直接可见的发光表面、面光源与环境。ReSTIR 主表面的直接光由最后的复用阶段写入；MinimalPathTracer 将主表面的光源采样与一次散射后命中光源的贡献归入直接光。更深层路径归入间接光，合并颜色为三路之和。

[RayTracingScenePass](../passes/RayTracingScenePass/RayTracingScenePass.cpp) 按有效材质的非零发光系数筛选子网格实例，准备命中记录及每个实例的三角形任务。GPU 为每个发光三角形生成一条 80 B 记录，保存世界空间顶点与边、实际面积、三个顶点的 UV 和共享材质索引；非均匀缩放、剪切及实例变换均在此应用。缓冲开头的 16 B header 保存有效三角形数量，容量增长由 Graph 管理，描述符按资源版本更新。该组织沿用 [RTXDI 的发光网格展开方式](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md#mesh-light-processing)。

ReSTIR 子路线使用 `RayTracingPass` 的 `restir` 参数，MinimalPathTracer 使用独立的光源采样。ReSTIR 默认生成 32 个光源候选和一个 BRDF 候选，启用时域复用及 3 邻居、30 像素半径的一轮空间复用。历史计数限制为当前初始计数的 20 倍。合并时在当前表面重算目标值，按原论文 Algorithm 6 检查来源表面的支持域与可见性并归一化；各阶段使用独立随机流和明确的缓冲读写顺序。公式、数据与资源生命周期见 [ReSTIR](ReSTIR.md)。

相机运动通过上一帧矩阵重投影，检查对象、法线和深度匹配；场景几何、变换、材质、光源、环境或相关配置变化时重置 reservoir 历史。ReSTIR 的采样历史与 SVGF 的颜色历史各自维护，关闭 SVGF 时仍可复用光源样本。

仅在开启 SVGF 时执行 [SVGFReprojection](../passes/SVGF/SVGFReprojection.h) 和 [SVGFFilter](../passes/SVGF/SVGFFilter.h)。直接光与间接光先除以各自像素的 albedo，在解调后的空间中累积颜色、亮度矩并估计方差。有效样本数不超过 `historyLength` 时使用算术平均，超过后使用 `emaAlpha`；首个样本或历史失效时权重为 1。历史不足 4 帧时使用 7×7 空间方差，五级 à-trous 按深度、法线、亮度与方差滤波。第一级回写解调后的颜色历史，最后一级乘回当前像素的 albedo 并添加当前帧 `emission`。除法和乘回统一使用 `max(albedo, 0.01)`，避免黑色通道除零。算法与限制见 [SVGF 文档](SVGF.md)。

### 离线路径追踪

入口 shader 是 [main.slang](../passes/OfflinePathTracingPass/main.slang)。材质预览和纹理程序求值位于 [OfflinePathTracingPass](../passes/OfflinePathTracingPass/)，
PBRT 散射模型位于 [shaders/material/pbrt](../shaders/material/pbrt/)，Sobol 采样位于 [shaders/math](../shaders/math/)。
转换规则见 [PBRT 转换文档](PbrtConversion.md)。

入口通过条件编译选择 [pt.slang](../passes/OfflinePathTracingPass/pt.slang) 或 [bdpt.slang](../passes/OfflinePathTracingPass/bdpt.slang)，两者提供相同的 `tracePath` 调用接口。入口负责相机主射线、交点回传和累计写入，具体路径算法由各自头文件实现。

- 使用独立 Pass 和 Shader，活动 Frames-in-flight 为 1。
- 每个样本始终使用 Owen 扰乱 Sobol 序列进行像素内抖动，FP32 缓冲保存颜色总和并记录各像素样本数。BDPT 分块推进，按实际完成的相机样本与发射路径数量分别归一化，持续刷新预览。
- 几何、相机、材质、光源、反弹次数或图重建变化时重新累积，曝光等显示参数调整时保留累积。
- 编辑器通过互斥的 Algorithm 选择 PT / BDPT，并显示各自参数。PT 默认两次表面散射，point / spot、distant、area 分别执行蓄水池采样，area 与 BRDF 做 MIS。BDPT 默认六次散射，连接相机和光源子路径，包含光路投影到相机的 splat 与完整路径 MIS，见 [BDPT](pbrt/BDPT.md)。
- 散射实现见 [PBRT 材质](pbrt/材质.md)，光源和限制见 [PBRT 光源](pbrt/光源.md)。两种算法均通过 Compute Resolve 输出颜色，再接入 HDR 后处理。

## 帧与资源

下述曝光流程对应 `Post process = RenderLab`；`Falcor` 选项使用独立的 [Falcor 后处理](PostProcess.md)。

`ExposurePass` 逐帧使用 `editorCamera.exposure` 或自身的曝光参数。相机设置优先，两处均支持手动和自动模式。色调映射算法由 `ToneMappingPass` 的 `mode` 参数选择。

[ExposurePass](../passes/postprocess/ExposurePass/ExposurePass.h) 在 Bloom 之前读取场景 HDR 颜色。自动模式每隔两个像素测光，将线性 HDR 亮度映射到 256 档对数直方图，按配置裁掉两端百分位后计算代表亮度，并换算为目标 EV100。目标值限制在 EV 范围内，依据帧时间和明暗适应速度进行指数平滑；首次有效测光、切换场景和图重建时直接初始化。全黑画面保留已有曝光，等待首次有效测光。手动模式直接使用配置值。

曝光状态保存在 GPU 缓冲中，由 ToneMappingPass 直接读取并作用于线性 HDR 结果。两个 Pass 共用 [ExposureState](../shaders/postprocess/exposure_state.slang) 的布局。曝光补偿同时适用于两种模式，曝光调整保留离线光照累计。

- 默认两帧在途，每个槽位有独立 FrameContext、FrameData；复用前等待对应 Fence。
- FrameData 持有逐帧 Scene / Light / Shadow UBO、独立的公共 Set 和绑定版本；共享光源 Buffer handle 归 Renderer，阴影 CPU 数据与 Buffer handle 归 ShadowPass。
- 光栅与实时光追共用 glTF 材质 SSBO，离线光追使用独立的 PBRT 材质预览与纹理指令缓冲；三者共用纹理和采样器数组。SSBO 由 Graph 持有，上传与读取通过 Barrier 排序。
- 每个在途槽位有一个持久映射的 staging pool；Fence 完成后重置分配游标，prepare 取得 offset 后写入数据，Graph 执行时复制到 SSBO。
- Graph 创建的图像和缓冲跨帧共享，通过 `getImage(handle)` 和 `getBuffer(handle)` 访问。
- 同一队列上追踪资源最后访问，下一帧首次使用前通过 Barrier 同步。
- 拾取等待提交完成后立即回读。Renderer 根据 acquire 结果选择交换链 Image 并绑定外部目标，ImageView 缓存由 Swapchain 中的 Image 持有。
- SVGF 历史资源由 Graph 创建并持有，Pass 保存 handle；两路颜色与亮度矩各自使用双层 Ping-pong，深度 / 法线历史共享，图重建时重置历史。
- Device、ShaderManager、RenderResourceManager、MaterialTable 通过 RenderContext 聚合；Scene、Light、Shadow 和 Material 的共享 Layout 在初始化时预创建。Device 缓存并持有全部 Pipeline Layout 和光栅 Pipeline，Pass 只保存 Pipeline Layout 句柄；私有 Descriptor Set Layout 仍由各 Pass 持有。布局匹配交给 Vulkan 验证层检查。

## 代码导航

| 位置 | 内容 |
| --- | --- |
| [Renderer.cpp](../src/render/Renderer.cpp) | 图重建、全局帧准备和提交 |
| [graphs/](../graphs/) | 四张独立 Graph 和共用组图模块 |
| [graph/](../src/render/graph/) | RenderGraph、Pass 注册与最终出口 |
| [passes/](../passes/) | 各 Pass 的实现与注册 |
| [render/raytracing/](../src/render/raytracing/) | 场景 BLAS / TLAS、底层构建和光追绑定 |
| [RayTracingPass](../passes/RayTracingPass/) · [OfflinePathTracingPass](../passes/OfflinePathTracingPass/) · [SVGF](../passes/SVGF/) | 实时与离线光追、时域降噪 |
| [resources/](../src/render/resources/) | GPU 资源、缓存、材质表与 Shader 管理 |
| [device/](../src/render/device/) | Vulkan、Pipeline 与 Layout、帧同步、计时与交换链 |

CPU Scope、GPU Timestamp、Draw / 三角形计数用于观察各阶段成本。
