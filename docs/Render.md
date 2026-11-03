# 渲染模块

[项目首页](../README.md) · [架构](Architecture.md) · [RenderGraph](RenderGraph.md) · [Descriptor / Pipeline Layout](RenderLayout.md)

渲染模块接收场景快照和编辑器输入，准备 GPU 数据，再通过 RenderGraph 调度完整的渲染 Pass。Renderer 负责流程，Pass 负责 Vulkan 绘制细节。

Shader 的 [common 目录](../shaders/common/) 保存通用计算、采样与数据接口，[raster 目录](../shaders/raster/) 保存光栅化实现。光追按实时和离线分组，后处理与编辑器各自独立。

Shader 编译统一将 `shaders` 和 `src` 加入 include 搜索目录。Shader 文件按 `shaders` 根目录引用，例如 `#include "common/random.slang"`；CPU / shader 共享头文件按 `src` 根目录引用，例如 `#include "render/raytracing/RayTracingBindings.h"`。

场景以 Submesh 粒度的 DrawItem 提交，光栅化直接绘制，光追用同一批条目关联 BLAS、生成 TLAS 实例与命中数据。Renderer 只读引用 DrawItem 列表，材质索引按条目顺序单独保存在数组中。

## 四条路线

| 路线 | 主流程 | 用途 |
| --- | --- | --- |
| Forward | DepthPrepass → ScenePass → Skybox → Transparent | 基础 PBR、Clearcoat、Sheen 与透明物体 |
| Deferred | DepthPrepass → GBuffer → DeferredLighting → Skybox → Transparent | 统一材质缓冲与全屏光照 |
| Ray Tracing | BLAS / TLAS 构建、RayTracingScene → RayTracing → 可选 SVGFReprojection → 五级 SVGFFilter | 发光三角形准备、分路累积、方差估计与多尺度滤波；末级直接合成 |
| Offline | BLAS / TLAS 构建 → OfflinePathTracing（PT / BDPT）→ Resolve | PBRT 场景的渐进式离线渲染 |

光栅路线在需要时加入 ShadowPass。光源标记在线性场景颜色上绘制，随后一起经过 Bloom（可选）与 ToneMapping；光追关闭 HDR 时通过 Blit 输出。UI 在后处理之后绘制，拾取仍使用独立阶段。

### 光栅化

Shader 入口为 [forward.slang](../shaders/raster/forward.slang)、[gbuffer.slang](../shaders/raster/gbuffer.slang) 和 [deferred_lighting.slang](../shaders/raster/deferred_lighting.slang)。深度、阴影、天空盒与网格顶点处理也位于 `shaders/raster`。

- 深度使用 D32Sfloat，主场景支持不透明、Alpha Mask、双面与排序透明绘制。
- 延迟 G-buffer 保存 Diffuse / AO、Normal / Roughness、Emissive / F90、F0 / Diffuse Fresnel Weight、Sheen / Roughness，并从深度重建位置。
- Forward / Deferred 复用基础 BRDF、灯光、阴影和 IBL；Clearcoat 当前仅在 Forward 计算。
- 透明材质采用前向混合。
- 阴影由统一 [ShadowPass](../src/render/raster/ShadowPass.cpp) 处理，算法见 [阴影文档](Shadow.md)。

### 实时光追

Shader 入口：[realtime.slang](../shaders/raytracing/realtime/realtime.slang)。实时路径循环与 SVGF 位于 `raytracing/realtime`，光源采样、BRDF 计算与采样、随机数等通用代码位于 `shaders/common`。

- 基础 PBR 复用漫反射与 GGX 函数，使用 VNDF 和完整混合 PDF 生成反弹方向。
- 点光源与聚光灯共用一次加权选择，合计只选一个；权重为最大 RGB 强度乘锥角衰减再除以距离平方，超出 range 或聚光锥的光源跳过，并补偿选择概率。矩形光源独立加权选择，发光网格的三角形独立均匀选择，两类面积光均与 BRDF 命中使用 MIS。
- 方向光通过可见性射线计算直接照明；矩形发光面以三角形参与求交。
- 实时光追的 [RayGen 路径循环](../shaders/raytracing/realtime/path_tracing.slang) 负责续射线和阴影射线，ClosestHit 返回命中与材质信息。Vulkan 管线递归深度固定为 1。
- `maxBounces` 控制最多 1–64 次表面散射，结束后仍计算终点发光；1 对应直接照明与终点发光。使用俄罗斯轮盘终止。修改反弹次数后复用渲染图和 Pipeline，并重置时域历史。BLAS 按几何缓存，TLAS 按提交帧更新。
- 关闭 SVGF 时输出 RGBA16F 合并颜色；开启时分别输出直接光、间接光、`emission` 与主命中材质的反射率 `saturate(diffuseColor + F0)`。两种模式均输出原始线性深度与法线，主射线使用固定的像素内采样位置。
- `emission` 保存相机直接可见的发光表面、面光源与环境。直接光包含主表面的光源采样、一次 BRDF 采样命中发光面 / 环境的贡献；更深层路径归入间接光。直接光保留面光源 MIS 的两部分，合并颜色为三路之和。

[RayTracingScenePass](../src/render/raytracing/realtime/RayTracingScenePass.cpp) 按有效材质的非零发光系数筛选子网格实例，准备命中记录及每个实例的三角形任务。GPU 为每个发光三角形生成一条 80 B 记录，保存世界空间顶点与边、实际面积、三个顶点的 UV 和共享材质索引；非均匀缩放、剪切及实例变换均在此应用。缓冲开头的 16 B header 保存有效三角形数量，容量增长由 Graph 管理，描述符按资源版本更新。该组织沿用 [RTXDI 的发光网格展开方式](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md#mesh-light-processing)。

每次表面散射从全部 N 个三角形中等概率选一个，再在其内部均匀面积采样，通过 UV 从现有材质与纹理数组求发光。完整立体角 PDF 为 `距离平方 / (N × 三角形面积 × 发光面余弦)`，双面材质使用余弦绝对值，单面材质只向正面发光。NEE 和 BRDF 命中使用同一个 PDF 做 balance MIS，相机直接可见的发光保持完整贡献。零面积或采样位置发光为零时本次贡献为零，三角形编号保持稳定。选择分布后续可用于 ReSTIR；当前每个着色点只访问一个候选三角形。

仅在开启 SVGF 时执行 [SVGFReprojection](../src/render/raytracing/realtime/SVGFReprojection.h) 和 [SVGFFilter](../src/render/raytracing/realtime/SVGFFilter.h)。直接光与间接光先除以各自像素的 albedo，在解调后的空间中累积颜色、亮度矩并估计方差。有效样本数不超过 `historyLength` 时使用算术平均，超过后使用 `emaAlpha`；首个样本或历史失效时权重为 1。历史不足 4 帧时使用 7×7 空间方差，五级 à-trous 按深度、法线、亮度与方差滤波。第一级回写解调后的颜色历史，最后一级乘回当前像素的 albedo 并添加当前帧 `emission`。除法和乘回统一使用 `max(albedo, 0.01)`，避免黑色通道除零。算法与限制见 [SVGF 文档](SVGF.md)。

### 离线路径追踪

Shader 入口：[main.slang](../shaders/raytracing/offline/main.slang)。材质预览和纹理求值位于 `raytracing/offline`，Sobol 采样位于 `shaders/common`，转换规则见 [PBRT 转换文档](PbrtConversion.md)。

入口通过条件编译选择 [pt.slang](../shaders/raytracing/offline/pt.slang) 或 [bdpt.slang](../shaders/raytracing/offline/bdpt.slang)，两者提供相同的 `tracePath` 调用接口。入口负责相机主射线、交点回传和累计写入，具体路径算法由各自头文件实现。

- 使用独立 Pass 和 Shader，活动 Frames-in-flight 为 1。
- 每个样本始终使用 Owen 扰乱 Sobol 序列进行像素内抖动，FP32 缓冲保存颜色总和并记录各像素样本数。BDPT 分块推进，按实际完成的相机样本与发射路径数量分别归一化，持续刷新预览。
- 几何、相机、材质、光源、反弹次数或图重建变化时重新累积，曝光等显示参数调整时保留累积。
- 编辑器通过互斥的 Algorithm 选择 PT / BDPT，并显示各自参数。PT 默认两次表面散射，point / spot、distant、area 分别执行蓄水池采样，area 与 BRDF 做 MIS。BDPT 默认六次散射，连接相机和光源子路径，包含光路投影到相机的 splat 与完整路径 MIS，见 [BDPT](pbrt/BDPT.md)。
- 散射实现见 [PBRT 材质](pbrt/材质.md)，光源和限制见 [PBRT 光源](pbrt/光源.md)。两种算法均通过 Compute Resolve 输出颜色，再接入 HDR 后处理。

## 帧与资源

曝光在逐帧准备时选择配置：`editorCamera.exposure` 优先，否则使用全局 `toneMapping.exposure`。两处均支持手动和自动模式，色调映射算法由全局配置选择。

[ExposurePass](../src/render/postprocess/ExposurePass.h) 在灯光标记和 Bloom 之前处理曝光。自动模式每隔两个像素测光，将线性 HDR 亮度映射到 256 档对数直方图，按配置裁掉两端百分位后计算代表亮度，并换算为目标 EV100。目标值限制在 EV 范围内，依据帧时间和明暗适应速度进行指数平滑；首次有效测光、切换场景和图重建时直接初始化。全黑画面保留已有曝光，等待首次有效测光。手动模式直接使用配置值。

曝光状态保存在 GPU 缓冲中，由 ToneMappingPass 直接读取并作用于线性 HDR 结果。曝光补偿同时适用于两种模式，曝光调整保留离线光照累计。

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
| [Renderer.cpp](../src/render/Renderer.cpp) | 路线调度、全局帧准备、光栅化主视图剔除 |
| [graph/](../src/render/graph/) | RenderGraph、Pass 输入接口与最终出口 |
| [raster/](../src/render/raster/) | 前向、延迟、透明、阴影与共享场景绘制 |
| [raytracing/](../src/render/raytracing/) | 共享加速结构、BLAS / TLAS 构建与输出 Blit |
| [raytracing/realtime/](../src/render/raytracing/realtime/) | 实时光追与 SVGF |
| [raytracing/offline/](../src/render/raytracing/offline/) | 离线光追、PBRT GPU 数据准备与材质预览 |
| [postprocess/](../src/render/postprocess/) | Bloom 与 Tone Mapping |
| [editor/](../src/render/editor/) | 光源标记、拾取、编辑器深度与 UI 绘制 |
| [resources/](../src/render/resources/) | GPU 资源、缓存、材质表与 Shader 管理 |
| [device/](../src/render/device/) | Vulkan、Pipeline 与 Layout、帧同步、计时与交换链 |

CPU Scope、GPU Timestamp、Draw / 三角形计数用于观察各阶段成本。
