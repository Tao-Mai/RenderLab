# Renderer

`Renderer` 组织 Vulkan 初始化、按配置组图和逐帧提交。场景由 SceneManager 加载，Engine 和 Editor 准备绘制数据。长期 device 对象、资产 GPU 资源、图资源和 frame-in-flight 数据分别管理生命周期。

## 模块与所有权

| 模块 | 职责 | 生命周期 |
| --- | --- | --- |
| `VulkanContext` | Instance、PhysicalDevice、Device、Queues、queue family、features/properties | 整个 Renderer |
| `Swapchain` | 交换链、图像和视图、每张图像的 renderFinished Semaphore、acquire/present | 每次交换链创建至重建 |
| `ShaderManager` | SPIR-V 与反射加载、VkShaderModule 缓存、ShaderHandle | device 级 |
| `DescriptorManager` | Layout cache、Pool、Set allocation | Layout/Pool 为 device 级；Set 随使用者 |
| `PipelineManager` | PipelineLayout、显式 PipelineState、PipelineKey cache | device 级 |
| `RenderResourceManager` | Mesh/Texture/Material/Sampler 资产到 GPU 对象的映射和缓存 | 资产级；当前缓存至 Renderer 关闭 |
| `RenderGraph` | 登记节点、合并 usage、编译依赖、分配内部资源、barrier 和命令录制 | Graph 长期存在；内部资源在重新组图并 compile 时重建 |
| `FrameContext` | CommandPool/Buffer、Fence、imageAvailable、wait/submit | 每个 frame-in-flight 一份 |
| `FrameData` | View/Light CPU 数据与 UBO、灯光/点阴影 SSBO、Scene Set、数据上传与扩容 | 由 Renderer 按在途帧持有 |

Renderer 持有上述模块和各 Pass 对象，负责按配置组图。`RenderSystem` 由 Engine 持有和调用，Renderer 接收其生成的 CPU `SceneRenderData`，等待当前帧 fence 后由 RenderResourceManager 解析为 GPU `RenderData`。Graph 持有对应节点，不依赖具体 Pass 类型。终点依赖最终颜色和 readback buffer，声明 Present 和 HostRead 状态；Renderer 保存 PickingCopy 的 readback handle，在 fence 完成后通过 Graph 的 getBuffer 下载 selection ID，不再单设 Readback Pass。ScenePass 只绘制不透明和 MASK 物体；SkyboxPass 管理 radiance descriptor、Pipeline/Layout 和天空盒绘制；TransparentPass 加载已有颜色和深度，绘制排序后的透明物体。SceneDraw 共用附件、视口设置和材质网格绘制逻辑，不增加新的资源管理层。LightMarkerPass 随后独立绘制灯光标记。光栅 Pipeline 由 PipelineManager 缓存，光追 Pipeline/SBT/BLAS/TLAS 由 RayTracingPass 持有；Pass 直接接收类型化句柄或前序 Pass 的 Output，通过 Image 对象取得缓存 View。

## 光栅化与光追路线

编辑器 Render path 提供 Forward、Deferred 和 Ray tracing，选择在关闭时持久化到 renderer.json。Forward 保留现有逐物体光照；Deferred 使用下述 G-buffer 链路，两条光栅路线共用透明排序、天空盒、HDR、编辑器绘制和拾取。

### 延迟渲染

DepthPrepass → GBufferPass → DeferredLightingPass → Skybox → Transparent → ToneMapping → 编辑器。仍使用 dynamic rendering 和现有 Graph，不引入 subpass、光源体积或新的同步模型。

| G-buffer | 格式 | 内容 |
| --- | --- | --- |
| Albedo/Occlusion | RGBA8 sRGB | 线性 Base Color 经硬件编码存储，alpha 为 AO |
| Normal/Roughness | RGBA16F | 法线贴图修正后的世界法线、roughness |
| Emissive/Metallic | RGBA16F | 线性 emissive、metallic |
| Depth | D32 | 复用预通道深度，通过 inverse VP 重建位置 |

GBufferPass 只绘制不透明和 MASK 物体，保持双面和 alpha-test。DeferredLightingPass 按像素读取 G-buffer，复用 scene_lighting.slang 的直接光 BRDF、点光源 PCF、矩形光源 PCSS、方向光 CSM、IBL 和环境光系数；AO 仅影响环境光。透明材质继续前向混合。延迟路线中的 clearcoat 材质仅使用基础 PBR 数据，没有额外材质分类或扩展 G-buffer。

SceneBindings 复用场景 UBO/SSBO 上传、阴影占位资源和环境 descriptor；ScenePass 与 DeferredLightingPass 各自负责绘制。PipelineKey 使用颜色格式数组支持 MRT，原有单附件 Pass 使用单元素数组，深度 Pass 使用空数组。

G-buffer 与深度重建参考 [Khronos 延迟渲染示例](https://docs.vulkan.org/samples/latest/samples/performance/subpasses/README.html)；此项目保留独立 Pass 和 dynamic rendering，不采用该示例的 subpass 优化。

### 前向渲染

光栅化链路为 DepthPrepass → Scene → Skybox → Transparent → ToneMapping → LightMarker → Picking / EditorUi → Present。DepthPrepass 创建并清空 D32 深度图，绘制 Opaque 和 MASK 物体，沿用材质的剔除设置和 alpha cutoff，BLEND 物体不参与预绘制。Scene 清空颜色附件，Skybox 和 Transparent 加载前序颜色，各自通过 write 生成新版本的颜色 handle；三个 Pass 都加载只读深度附件，不再写深度。不透明和 MASK 物体使用 Equal 比较，透明物体保持原来的深度比较和排序，天空盒仍使用 LessOrEqual。深度与场景 shader 共用 mesh_vertex.slang 的 precise 顶点位置计算，以及 material_data.slang 的材质布局和 Alpha Test。Graph 根据依赖排序并插入 barrier；编辑器 LightMarkerPass 在 Tone Mapping 之后叠加标记，写入深度供拾取使用。

点光源、方向光和矩形光共用 brdf.slang 的直接光 BRDF：Schlick Fresnel、GGX 分布、height-correlated Smith 可见性和 Lambert 漫反射，金属度同时控制 F0 和漫反射权重。光源分支只生成光线方向、入射光量和阴影可见性，最后统一乘 BRDF 和 NdotL。直接光的感知粗糙度下限为 0.045，避免零粗糙度数值退化；材质数据和 IBL 粗糙度保持原值。矩形光仍用中心方向与投影面积/距离平方近似立体角，新增的镜面响应不代表有限面积光源积分；Spot 暂未接入。

Scene 使用 RGBA16F HDR 颜色图，Graph 为每个在途帧单独分配。ToneMappingPass 持有独立 descriptor set、pipeline 和绘制逻辑，只采样当前帧的 HDR 图，Graph 根据声明插入 ColorAttachment → ShaderRead barrier。灯光标记、UI 和坐标轴都在 Tone Mapping 之后绘制，不受曝光和 Tone Mapping 模式影响。

`feature.hdr` 控制 HDR 输出链路，默认 true，光栅和光追均可在编辑器切换。Renderer 检测到 Enable HDR 配置变化后重新组图并 compile；关闭时 Scene 直接写交换链，光追则将 RGBA16F 图直接 blit 到交换链，不登记 ToneMappingPass。曝光和 Tone Mapping 参数保留但控件禁用，重新开启后继续使用；开关与其他渲染设置一样仅在正常关闭时统一持久化。

`feature.toneMapping.ev100` 是手动相机曝光值，`exposureCompensation` 是额外补偿档位。CPU 计算倍率 `2^(exposureCompensation - ev100) / 1.2`，通过 push constants 传给 Tone Mapping；EV100 +1 使亮度减半，补偿 +1 使亮度加倍。编辑器修改每帧生效，不重建渲染图；正常关闭时统一保存到 renderer.json。`mode` 提供 Neutral（Khronos PBR Neutral）和 Reinhard。输出线性颜色到 sRGB attachment，由硬件编码一次；UNORM + sRGB nonlinear 交换链由 shader 显式编码。此版本不包含自动曝光、Pre-Exposure 或 Bloom。

物理灯光采用 1 世界单位 = 1 米。LightComponent 的 intensity 按类型表示 Point/RectArea 的光通量（lm）、Directional 的照度（lux）、Spot 的发光强度（cd，暂不参与照明）。RenderSystem 调用 photometricEmission：点光源除以 `4π` 转为 cd；单面 Lambert 矩形光源除以 `π × 面积` 转为 cd/m²；方向光保持 lux。线性 Rec.709 灯光颜色归一化到单位亮度，光栅与光追共享同一 LightData 和单位约定。面光源面积改变时总光通量保持不变，单位面积亮度随之改变；面积由 areaSize 决定，不使用 Actor scale。环境 HDR 图、自发光 glTF 材质仍是未做绝对光度标定的线性数据，不能据此声称整个场景都具备真实亮度。

Neutral 算法参考 [Khronos PBR Neutral Tone Mapper](https://github.com/KhronosGroup/ToneMapping/tree/main/PBR_Neutral)，shader 中注明 Slang 改写及 CC-BY-4.0 来源。

编辑器的 Renderer / Render path 可选择 Rasterization 或 Ray tracing，选择保存在内存中的 `renderPath`，正常关闭时由 ConfigManager 统一写入 `config/renderer.json`，启动时恢复。该文件由 `config/config.json` 的 `paths.renderer` 指定，并保存 BRDF LUT 绑定和 RenderFeature；环境图选择保存在 SceneAsset.environmentMap，随场景保存和切换。当前光栅化仍是默认路线；不支持所需 Vulkan 光追特性的 GPU 禁用光追选项，配置直接指定不支持的路线则明确报错，不自动退回光栅化。

`render/raytracing/RayTracingPass` 使用独立的 `VK_KHR_ray_tracing_pipeline`：缓存每个 mesh/submesh 的 BLAS，每帧根据现有 DrawItem 的 transform 构建该帧 TLAS，通过 SBT 和 `vkCmdTraceRaysKHR` 发射相机主射线。`raytracing/raytracing.slang` 将相机射线、命中表面和着色封装为结构体方法。ClosestHit 插值 Vertex UV 和法线，通过实例变换的逆转置得到世界空间法线；支持点光源直接光照和矩形光源直接光 MIS：RectAreaLight 负责矩形采样和立体角 PDF；所有矩形灯共享一个单位矩形的双三角形 BLAS，实例变换提供位置、旋转和宽高，实例自定义索引直接对应灯光 SSBO。BRDF 射线由 TLAS 确定最近命中；表面 hit group 继续着色，独立 light hit group 返回单面发光值，不再解析遍历所有灯。NEE 和 BRDF 命中发光面分别乘 balance heuristic 权重，不对物体间接光重复加权。矩形灯只生成光追内部几何，不创建 Mesh 资产或 RenderComponent；主相机射线通过 mask 排除灯面，反弹和阴影射线包含灯面，编辑器标记保持独立。Surface::isVisible 沿法线偏移后向光源发射有限长度的阴影射线，首个遮挡即可停止，跳过 closest-hit；独立 visibility miss shader 返回可见。Surface::shade 使用余弦加权半球采样，Payload 传递递归深度、随机状态和上一表面的 BRDF PDF，各次命中累积间接光照。最终着色顶点仍可发射 BRDF 射线命中灯面；到达递归上限时，普通表面 hit shader 直接返回黑色，不再发射阴影射线，而 light hit shader 仍可返回发光值。`feature.rayTracing.maxRecursionDepth` 控制 Vulkan 管线递归深度（默认 4，至少 2，受 GPU 上限限制），编辑器调整后重建光追 Pipeline/SBT，关闭时持久化；包括主射线和为可见性查询预留的一层，因此 2 仅直接光，4 最多两次反弹。SBT 包含一个 raygen、两个 miss 和两个 hit group。Miss 返回黑色，TLAS 包含场景网格和矩形发光面，不包含编辑器灯光标记。暂不包含 IBL、透明、alpha-test、时域累积或降噪，所有三角形按不透明处理；每帧最多 128 种材质纹理/采样器绑定。

光追复用 GPU mesh 的 vertex/index buffer、GpuMaterial 的纹理/采样器以及 FrameData 的相机数据，并将 RenderData 的灯光列表上传到当前光追帧的独立 SSBO；`light_data.slang` 与光栅路线共用 LightData 类型，不改变光栅 descriptor layout。RayTracing 写入 RGBA16F storage image，保留线性 HDR 光照；开启 HDR 时复用 ToneMappingPass 输出到交换链，Graph 插入 ShaderStorageWrite → ShaderSampledRead barrier，关闭时由 RayTracingBlit 直接输出。光追时关闭 Scene/Shadow 节点；EditorDepthPass 使用独立 editor/depth.slang，为编辑器遮挡和拾取生成深度，不走场景材质逻辑，与当前光追一样按不透明几何处理。两条路线统一在主场景输出之后执行 LightMarkerPass，再执行 Picking 和 EditorUi。交换链等待阶段在直接 blit 时使用 transfer，其他情况使用 color-attachment-output，仍使用原来的两帧 fence/semaphore 模型。加速结构构建/复用仍使用其原有内部内存屏障，不属于 Graph 当前管理的图像/缓冲区资源。

光追 Payload 额外传递累计 throughput，仅用于俄罗斯轮盘的存活概率计算。从第三次表面散射开始，以更新后 throughput 的最大颜色分量（上限为 1）作为存活概率；被终止的路径保留当前顶点直接光照，存活的后续贡献除以该概率补偿。Lambert 的 baseColor 和轮盘补偿在递归返回时各应用一次，不重复乘累计 throughput；NEE/BRDF 的 MIS 仍使用原来的方向 PDF，最大递归深度继续作为硬上限。

API 参考：[Khronos 基础光追示例](https://docs.vulkan.org/samples/latest/samples/extensions/ray_tracing_basic/README.html)、[多实例与材质示例](https://docs.vulkan.org/samples/latest/samples/extensions/ray_tracing_extended/README.html)、[阴影射线与递归示例](https://docs.vulkan.org/samples/latest/samples/extensions/ray_tracing_reflection/README.html)。

SceneManager 持有 AActor 和 Component。Engine 在场景 Tick 和编辑器更新之后调用 RenderSystem，遍历当前场景，提取 Transform、Render、Light 组件和材质覆盖，生成不含 GPU 对象的 SceneRenderData；RenderResourceManager 在当前帧可安全更新后解析资产、生成 DrawItem 并排序透明物体。RenderData 独立定义在 render/RenderData.h，只包含绘制数据，不缓存 Actor 引用；场景切换、增删 Actor 和 Mesh 修改在下一次 prepare 时自然生效，不重建 Graph。Editor 持有 LightMarkers，每帧读取当前场景，生成独立标记 DrawItem；绘制和拾取由对应编辑器 Pass 录制，不复用 SceneDraw 或材质逻辑。EditorCamera 生成普通 RenderView；Renderer 不依赖 Actor、SceneManager、EditorCamera 或 Editor 对象，只接收 RenderView、SceneRenderData 和 EditorFrameInput。

当前 `maxFramesInFlight` 为 2。Graph 为每个在途帧分配独立的 Scene 深度图、Shadow cubemap、拾取图和 readback Buffer。交换链图像作为外部资源导入，Graph 借用句柄并跟踪状态。资产环境纹理不登记到 Graph，由资产 GPU 资源负责上传和采样状态，Pass 直接绑定 descriptor。FrameData 的 View/Light UBO 和灯光 SSBO 由 Pass 直接引用，不登记到 Graph 资源声明；Renderer 在录制前更新当前帧的数据。Mesh/Material 的 vertex/index buffer、材质 UBO 和 base color image 由 RenderResourceManager 解析并在 DrawItem 中引用，GPU 对象由 RenderResourceManager 创建和持有。它们不登记到 Graph 资源声明，上传与可供绘制使用的状态由资产 GPU 资源实现负责。

### 离线渐进式路径追踪

`RenderPath::OfflinePathTracing` 使用独立的 `render/offline/OfflinePathTracingPass` 和 `shaders/offline/pathtracing.slang`，不修改实时 RayTracingPass 或其 shader。第一版使用与实时路线相同的基础漫反射、点光源 / 方向光 / 矩形面光源、MIS 和俄罗斯轮盘规则；尚不包含完整 PBRT 材质、光谱、IBL 或介质。离线相机射线可以看见实际矩形发光面，编辑器标记仍单独绘制。

Renderer 在离线路线只复用 frame 0，每次提交前等待其 fence；其他路线保持两帧在途，swapchain 图像数量不变。离线 Pass 独立持有一个 device-local FP32 float4 累积 buffer，通过 importBuffer 登记给 Graph，由 Graph 插入跨帧 storage read/write barrier。每轮每像素采样一次，随机种子包含 pixelIndex 和 sampleIndex，相机射线在像素内部抖动；线性样本总和除以 SPP 后写入 RGBA16F，再走现有 Tone Mapping 或直接 blit。首次采样覆盖 buffer，不读取旧值；视口外像素清零。

Pass 比较 CPU SceneRenderData 快照、相机逆 VP / 位置 / 视口和资产版本。物体、材质、灯光、环境或相机变化重置 SPP，下一轮覆盖旧贡献；对象重命名、选中和曝光变化不重置。HDR 显示链路重建保留累积 buffer，切换路线、修改离线递归深度或改变输出尺寸重新开始。`feature.offline.maxRecursionDepth` 独立于实时参数，正常关闭时与路线一起保存；采样历史不持久化。编辑器显示当前 SPP。第一版连续采样，不提供目标 SPP、暂停或图片导出。

## 场景 Shader 接口

绑定编号和 GPU 数据结构见 `resource/ShaderData.h`。

| Set | Binding | 资源 |
| --- | --- | --- |
| 0 Scene | 0 | ViewUniforms UBO |
| 0 Scene | 1 | LightUniforms UBO：light count、环境光系数、IBL 参数 |
| 0 Scene | 2 | LightData[] SSBO |
| 0 Scene | 3 | PointShadowData[] SSBO：每个点光源六个 VP |
| 0 Scene | 4 | RectAreaShadowData[] SSBO：VP、near/far、UV 系数 |
| 0 Scene | 5 | DirectionalShadowData[] SSBO：每层 VP 和相机空间深度上限 |
| 0 Scene | 6 | Point shadow cubemap array image |
| 0 Scene | 7 | Rect area shadow 2D array image |
| 0 Scene | 8 | Directional cascade shadow 2D array image |
| 0 Scene | 9 | 三种阴影共用的比较 sampler |
| 0 Scene | 10 | Linear clamp sampler，非比较模式，用于读取原始纹理值 |
| 0 Scene | 11 / 12 | Irradiance image / sampler |
| 0 Scene | 13 / 14 | Prefiltered specular image / sampler |
| 0 Scene | 15 / 16 | BRDF LUT image / sampler |
| 0 Scene | 17 / 18 | Radiance image / sampler |
| 1 Material | 0 | Base color image |
| 1 Material | 1 | Base color sampler |
| 1 Material | 2 | MaterialUniforms UBO |
| 1 Material | 3 / 4 | Normal image / sampler |
| 1 Material | 5 / 6 | Metallic image / sampler |
| 1 Material | 7 / 8 | Roughness image / sampler |
| 1 Material | 9 / 10 | Emissive image / sampler |
| 1 Material | 11 / 12 | Occlusion image / sampler |
| 1 Material | 13 / 14 | Clearcoat intensity image / sampler |
| 1 Material | 15 / 16 | Clearcoat roughness image / sampler |
| 1 Material | 17 / 18 | Clearcoat normal image / sampler |

Renderer 为每个在途帧单独持有 FrameContext 与 FrameData，后者管理 Scene Set、两个 UBO、灯光 SSBO 和三种阴影 SSBO。ViewUniforms 包含 viewProjection、inverseViewProjection、cameraPosition 和 view，共 208 字节；view 用于取得正的相机空间深度。LightUniforms 包含 lightCount、ambientCoefficient、cascadeCount、iblParameters 和 shadowParameters，共 48 字节；iblParameters.x 是 prefiltered specular 的最大 LOD，shadowParameters 的 x/y/z 分别为归一化阴影深度 bias、点阴影方向空间 PCF 半径、逐像素旋转开关（0/1），w 预留为 0。LightData 按 RenderSystem 的灯光顺序存放，每项 112 字节，包含 shadowidx 和保留矩形旋转的 areaRightData（通过 areaRight 访问右方向）；lightCount 包括禁用灯光。上传时，enabled 且 castShadow 的点光源、矩形光源和方向光各自在类型内获得连续的 shadowidx，其余为 ~0u。PointShadowData 每项包含六个 VP，共 384 字节；RectAreaShadowData 包含 VP、nearPlane、farPlane 和 worldToUvScale，共 80 字节。DirectionalShadowData 为固定四层 CascadeShadowData 数组，每层 80 字节，包含 VP 和 depthUpperBound，整项 320 字节。

SSBO 至少分配一项以维持有效 descriptor；没有灯光时 lightCount 为 0，shader 不读取数组。数量增长时，在当前帧 fence 完成后扩容并更新该帧 descriptor，其他帧保持自己的 buffer。

GpuMaterial 持有 Material Set。MaterialUniforms 的普通材质前缀为 64 字节，emissiveFactor 位于偏移 48；自发光贴图使用 sRGB，采样转换为线性颜色后乘资产中的 emissive 系数，直接加入主场景光照结果，不受灯光、IBL 开关或阴影影响。缺少自发光贴图时绑定白色 fallback，未设置系数时默认为零。set 2 Object 和 set 3 Pass 的编号已保留，当前没有对应预设 Layout；对象变换、拾取 ID 和 Shadow 的 lightidx 通过 Push Constant 传递。

MaterialAsset::clearcoat 有值时选择 ENABLE_CLEARCOAT=1 变体（`.clearcoat.spv` / `.clearcoat.no_ibl.spv`），MaterialData 追加 clearcoatFactor、clearcoatRoughnessFactor、clearcoatNormalScale 与 padding，共 80 字节；未启用的材质仍上传 64 字节且不加载 clearcoat 贴图。所有变体共享 Material descriptor layout。clearcoat 三类纹理均为 Linear，强度取 R、粗糙度取 G；未指定强度/粗糙度贴图时绑定白色 Linear fallback，未指定法线时绑定 flatNormal。

清漆层法线独立从原始 TBN 变换，不继承底层法线贴图细节，双面材质分别翻转两层法线。清漆层采用 IOR 1.5 对应的 F0=0.04，不修改底层材质的金属度和 F0。直接光复用 GGX 分布、采用 Kelemen 可见性，两个波瓣分别乘自身 NdotL，再按 glTF 的 `clearcoatFactor * Schlick(clearcoatNormal·V)` 简单分层近似混合；两层共用现有阴影可见性。IBL 复用预过滤 GGX 环境贴图与 BRDF LUT，以清漆层的法线和粗糙度采样，LUT 使用单位 Fresnel 响应（scale+bias），避免层权重重复乘 Fresnel。AO 仍只作用于环境光，底层自发光乘 `(1-clearcoatWeight)`；关闭 IBL 后只保留清漆层直接高光和底层环境光系数衰减。

AO 贴图使用 Linear 色彩空间的 R 通道，缺少时绑定白色 Linear fallback。资产的 ao 字段作为 occlusionStrength 上传到 MaterialUniforms 偏移 36；shader 使用 lerp(1, sampledAO, occlusionStrength)，只乘环境光（IBL 的漫反射和镜面反射，或关闭 IBL 时的环境光系数结果），不影响直接光和自发光。编辑器现有 AO 控件调节该强度，0 表示禁用遮蔽，1 表示完整使用贴图。

SceneInterface 定义场景和材质的共享绑定接口，EditorInterface 独立定义编辑器的 View 绑定；ToneMapping 和 DeferredLighting 的专用布局在各自 Pass 内声明。Editor layout 只包含相机 UBO，不包含灯光、阴影、IBL 或材质 descriptor；FrameData 的 View Set 和 Scene Set 只共享底层相机 Buffer。标记、拾取和编辑器深度 shader 位于 shaders/editor，使用独立的 EditorViewUniforms 与 EditorShaderData.h，push constants 上限为 80 字节。ToneMapping 的 push constants 为 16 字节，包含 exposure、mode 和 encodeSrgb。各 Pass 显式声明所需 PipelineState，材质仅提供 alpha mode、双面等材质状态，PipelineManager 不包含业务预设。DepthPrepass 复用 SceneMaterial layout 和材质状态，只替换深度 shader、关闭颜色输出，不引入额外 descriptor。

## Shader、Descriptor 与 Pipeline

`feature.shadow.enable` 控制阴影绘制和点光源阴影 VP 上传。关闭时上传的灯光不带 castShadow 标记，源场景中的灯光属性保持不变；不登记 ShadowPass，以占位深度资源维持 descriptor 布局，重新开启后重新组图。`feature.shadow.pointShadowMapSize` 指定每个 cube 面的分辨率，修改后重建图资源；容量仍为固定的四个点光源。`feature.shadow.bias` 每帧上传到 LightUniforms.shadowParameters.x，调节时无需重建图资源。`feature.shadow.filter` 选择 Nearest 或 Linear 比较过滤，默认 Linear；切换时等待 GPU 空闲后选择 sampler 并刷新 descriptor，不重建图资源。编辑器 Shadows 区域提供阴影勾选框、过滤模式、常用分辨率和 Shadow bias，编辑只更新内存，配置随其他渲染设置在正常关闭时统一保存。

点阴影 PCF 使用 `sampling.slang` 中独立的 16 点泊松圆盘表，32 点表保持原样供其他采样使用。`feature.shadow.pixelRotation` 控制稳定的逐像素旋转，`feature.shadow.sampleRadius` 控制方向空间采样半径，默认分别为 true 和 0.02；编辑器 Shadows 区域可直接调整，两项每帧通过 shadowParameters 上传，不重建图资源，同样只在关闭时持久化。

构建阶段用 Slang 生成 SPIR-V 和 reflection JSON。主场景 shader 通过 `ENABLE_IBL=1/0` 编译为 `slang.spv` 和 `slang.no_ibl.spv`；关闭变体不执行环境光采样，而是使用 `ambientCoefficient * baseColor.rgb`。ShaderManager 根据 ShaderAsset ID 与 ShaderDefines 缓存模块和 descriptor 元数据，实际文件和编译宏由 CMake 生成的 `.variants.json` 清单解析，运行时不再拼接变体后缀。`config/renderer.json` 的 `feature.ibl` 显式控制变体，`feature.ambientCoefficient` 保存关闭 IBL 时的环境光系数；SceneAsset.environmentMap 保存环境图选择，由 RenderSystem 放入 SceneRenderData，再由 Renderer 配置 SceneBindings 和 SkyboxPass，不反向读取 SceneManager。编辑器勾选 IBL 时显示当前场景的环境图选择（不提供 None 选项），取消时显示系数调整；关闭 IBL 不丢弃场景的已选环境图。未选择环境图时使用白色 cubemap 兜底，不隐式关闭 IBL；天空盒仅在启用 IBL 且存在环境图时绘制，关闭时跳过天空盒 shader 和 pipeline 的创建。环境图修改标记场景 dirty，手动保存场景时持久化；切换场景仍丢弃未保存修改。渲染设置正常关闭时统一持久化，不会连带保存场景编辑。Scene/Material descriptor layout、纹理声明和绑定保持不变。自定义场景材质 shader 需要在清单中声明对应编译宏组合。

DescriptorManager 只缓存调用方或 reflection 声明的 Layout 并分配 Set；PipelineManager 接收实际 Layout，校验反射中的 binding 类型、数组容量、stage 和 push constant 范围。业务绑定定义归 SceneInterface、EditorInterface 或对应 Pass。

PipelineKey 包含 ShaderHandle、PipelineLayout、VertexLayout、Topology、Raster/Depth/Blend/MSAA 状态，以及颜色和深度 attachment 格式。`getOrCreate` 命中时复用；首次创建时检查 shader 的 set/binding/type 与 PipelineLayout 是否一致。Pipeline 跨帧和场景复用。

GpuMaterial 根据 alpha mode 选择 Opaque、AlphaTest 或 Transparent。MaterialAsset 的 shaderId 未指定时使用 scene shader。GpuTexture 只持有 Image/View；RenderResourceManager 按 SamplerAsset::ID 创建并缓存独立 sampler。材质与环境纹理使用 TextureBinding 指定 image/sampler，BRDF LUT 的绑定来自 Renderer 配置。

普通 2D 贴图在导入阶段生成完整 mip 链并持久化，GpuTexture 上传全部层级，材质 sampler 默认使用 Linear mip 过滤且不限制最大 LOD。sRGB 颜色在线性空间做 box 过滤，Alpha 保持线性；线性数据贴图不做伽马变换。R8/RG8 非 2 次幂贴图的紧凑 mip 数据只在上传 staging 时补齐 copy offset 对齐，不改变资产二进制格式。环境图的烘焙流程保持独立。

## 初始化与图重建

Renderer init 依次初始化 device、swapchain、各 Manager、FrameContext、RenderResourceManager 和 Graph，并独占持有 Pass 对象。四个 Manager 通过 renderContext() 向外部暴露；Renderer 内部直接调用成员的 init/shutdown。Graph init 显式接收 VulkanContext、Swapchain、FrameContext 和 FrameData span，不持有 Renderer 或访问其私有成员。

Renderer 首次接收 SceneRenderData 时组图，之后检测结构性渲染配置和场景环境变化。rebuildGraph 等待 GPU 空闲、释放旧图，再由 setupRenderGraph 显式串接 Pass 并 compile。ScenePass setup 加载 BRDF LUT、环境纹理和对应最大 mip 参数，SkyboxPass setup 单独绑定 radiance。Graph 分配资源后按拓扑顺序调用节点可选的 prepare 回调，ScenePass 和 ToneMappingPass 在此绑定图资源 descriptor，Renderer 不逐个特判。ScenePass 的 Output 同时传出解析后的阴影 handle，供 TransparentPass 声明采样依赖，包括关闭阴影时的占位图。具体材质 Pipeline 在首次绘制时创建并缓存。

资产 GPU 缓存跨场景保留。RenderSystem 不拥有 Mesh/Texture/Material 的 GPU 存储。

## RenderGraph 接口

各 Pass 提供 setup/execute，可选提供 prepare；setup 直接接收 ImageHandle/BufferHandle 或前序 Pass 的 Output，返回 Output，prepare 在图资源分配后执行。
Renderer setupRenderGraph 根据配置通过 setupPass<T>(args...) 完美转发参数，显式串接资源句柄；ScenePass 直接接收深度 handle 和 ShadowPass::Output。Graph 不再提供业务组图的 setup 接口。
所有 Pass 类型只在 Renderer.cpp 的 tuple 类型列表中集中声明一次，保存 Pass 对象，通过类型直接访问。
Renderer 的 setupPass 将 Pass 对象交给 graph.addPass；addPass 通过静态反射收集参数中的 handle，限定访问记录的切换范围并在返回时恢复。Pass 仍调用 graph.use(handle, access)。Graph 单独拥有节点，保存资源依赖、use 记录和 execute 回调，不使用字符串注册。
类型列表和 Renderer 的 setupPass 模板定义只放在 cpp；RenderGraph.h 定义通用模板，不包含各 Pass 的头文件。
Renderer 在组图后显式调用 compile，从 EndPass 回溯资源依赖、补充写前读约束，将保留节点按拓扑顺序写入 sortedNodes 并分配资源。execute 只遍历 sortedNodes，先转换节点声明的资源状态，再调用执行回调。暂不实现资源复用。

- createImage/createBuffer 只描述创建属性。
- importSwapchain 借用交换链图像。
- importImage/importBuffer 借用外部资源并接收初始状态；imageState/bufferState 查询执行后的状态，所有者负责资源存活与后续使用。
- use(handle, access) 声明 stage/access/layout，Graph 从实际访问推导创建 usage。
- getImage 返回长期保存的 Image 引用，Pass 调用 Image 的 getOrCreateImageView。View 缓存属于 Image。
- getBuffer 按当前帧或指定帧取得 Buffer。

Graph 在每个 Pass 前转换其声明的资源状态，Pass 内不转换图资源状态。
Picking 绘制与复制分成两个 Pass，HostRead 和 Present 出口状态由 EndPass 声明；光追写入与 Tone Mapping（或直接 blit）分成两个 Pass。
没有拾取请求时 Picking 和 PickingCopy 的 execute 提前返回，不绘制或复制，Graph 仍处理节点声明的资源状态；有拾取请求时，CPU 在提交 fence 完成后读取 ID。

ScenePass、SkyboxPass、TransparentPass 和 LightMarkerPass 分别负责不透明物体、天空盒、透明物体和灯光标记。
Mesh/Material 直接引用 RenderSystem 准备的资产 GPU 对象，不进入图资源分配。
内部资源按 frame-in-flight 分配，交换链按 acquired image index 选择；资产纹理由 RenderResourceManager 缓存，不参与 Graph 分配。
状态粒度为整个 Image 的全部 mip/layer 和整个 Buffer，仍使用一个 graphics/present queue。

## 绘制行为

`RenderConfig.h` 中的 `kMaxPointLightShadow = 4` 固定点光源阴影容量。启用阴影时，每帧阴影图预留 24 层，PointShadowData SSBO 固定分配四项，不随场景灯光数量或单灯开关状态扩容。上传时有效点光源超过四盏直接 CHECK 终止。

ShadowPass 作为一个 Graph 节点统一创建并输出点光源、矩形面光源和方向光三组阴影资源，保留各自的 shader variant 和 pipeline。execute 遍历有效阴影灯光，通过共用的 recordShadow 录制 attachment、descriptor、viewport、push constants 和 mesh 绘制；Graph 统一负责资源状态转换。

阴影总开关切换时重建 Graph；关闭时 Renderer 不添加 ShadowPass，ScenePass 接收无效阴影 handle，并用同一张 1×1 六层深度占位图填充三类阴影 descriptor。上传阶段会清除灯光的 castShadow 标记，因此 shader 不采样占位图。阴影纹理在 ScenePass 中统一声明为 ShaderReadOnlyOptimal，由 Graph 转换状态；ShadowPass::execute 不再检查总开关。

prepareShadows 在上传前为启用投影的点光源计算六个 VP，range 为 far plane。ShadowPass 对有效点光源各录制一次六面 multiview 渲染，viewMask = 0b111111；shader 按 lightidx → shadowidx → VP[SV_ViewID] 取矩阵。每个 shadowidx 对应 cubemap array 中独立的六层，fragment 写入 `distance(worldPosition, lightPosition) / range` 的线性深度。没有有效阴影点光源时不录制清空或绘制命令。Graph 在 ScenePass 前切换阴影图到 shader 只读状态。设备需要支持至少六个 multiview view 和 imageCubeArray。

ShadowPass 的矩形面光源绘制不使用 multiview，每个有效矩形阴影光源使用一层 2D 深度图。`kMaxRectAreaLightShadow = 4` 固定图像层数和 RectAreaShadowData SSBO 容量，shadowidx 在矩形类型内独立编号。上传阶段从光源中心计算透视 VP：垂直 FOV 100 度、宽高比 areaSize.x / areaSize.y、near plane 为 min(0.5, range * 0.1)、far plane 为 range，areaRight 保留光源绕发光方向的旋转。worldToUvScale 是投影矩阵 XY 对角元素绝对值的一半，深度 z 处的世界空间半径通过 radius * worldToUvScale / z 转为 UV 半径。shadow.slang 通过 SHADOW_TYPE 宏编译为 point、rect_area 和 directional 三个变体，只有点光源变体声明 SV_ViewID。矩形变体写透视深度，不写径向距离。主场景采用矩形核 PCSS：blocker 搜索和 PCF 各使用 4×4 分层的 16 点矩形采样，采样方法位于 sampling.slang，先在 [-1,1]² 中每格取一点，再按光源宽高和投影缩放。feature.shadow.pixelRotation 对点光源仍控制圆盘旋转，对矩形光源控制稳定的逐像素格内抖动；关闭时使用每格中心，始终不旋转矩形核。搜索范围投影到 near plane，用非比较 Linear sampler 搜索 blocker，再按平均线性 blocker 深度计算过滤半径，用比较 sampler 执行 PCF，过滤模式仍由阴影配置控制。超出投影范围和越界过滤样本视为未遮挡，越界搜索样本跳过。此方案仍是单张中心投影阴影图的软阴影近似，不是精确矩形光源积分。Spot 阴影及阴影 Pass 的 AlphaTest 镂空暂不处理。

ShadowPass 的方向光绘制使用单层 2D View 逐层绘制，不使用 multiview 或 cube 图像。`kMaxDirectionalLightShadow = 1` 限制一盏有效阴影方向光，`kMaxDirectionalShadowCascades = 4` 固定 SSBO 中的级联容量；图像按配置的 count 预留层数，不随灯光开关重建。feature.shadow.cascade 包含 count、shadowMapSize、maxDistance、splitLambda 和 depthPadding。上传阶段把相机视锥按均匀/对数混合分层，为每层包围球生成方向光正交 VP，Y 翻转匹配 Vulkan；depthPadding 沿光线方向扩展深度范围，避免把相机视锥外的遮挡物全裁掉。Scene shader 逐层比较正的相机空间深度和 depthUpperBound，找到第一个覆盖层后采样 shadowidx * cascadeCount + cascadeIndex。超出最后一层不再计算阴影；共用 Shadow bias 和 Nearest/Linear 比较过滤，暂不做级联混合、texel snapping、距离淡出或额外 PCF 核。

分层流程参考 [Vulkan cascaded shadow mapping 示例](https://github.com/SaschaWillems/Vulkan/tree/master/examples/shadowmappingcascade)。编辑器 Shadows 区域可调全部级联配置；count 和 shadowMapSize 修改时重建 Graph，其余参数逐帧生效，正常关闭时统一保存 renderer.json。

仅启用 IBL 且有环境贴图时，Renderer 才在 ScenePass 之后登记 SkyboxPass。fullscreen triangle 的深度为 1，fragment 从 ViewUniforms 读取逆视投影矩阵和相机位置，结合 push constant 中的编辑器视口重建世界方向，采样 radiance 基础 mip。保留深度测试、关闭深度写入，并请求早期深度测试。随后 TransparentPass 按距离从远到近绘制透明几何，经过可选的 Tone Mapping 后才由编辑器 LightMarkerPass 绘制灯光标记。

Picking 读取 LightMarkerPass 输出的深度，其中保留场景和灯光标记的遮挡关系；只在请求拾取时绘制点击像素的 selection ID、复制至该帧的回读 Buffer。深度以只读 attachment 使用，标记从编辑器自己的 DrawItem 列表绘制。

## 每帧流程

1. Engine 完成场景更新；Editor 准备 RenderView、灯光标记和 UI，Renderer 自行分类配置变化并决定是否重建图。Engine 调用 RenderSystem prepare，再向 Renderer 显式传入视图、场景和编辑器绘制数据。
2. FrameContext 等待 Fence，Swapchain acquire 图像。RenderResourceManager 解析 CPU 场景快照。光栅路线由 SceneBindings 组织 LightUniforms，调用 feature/shadow 的 prepareShadows 生成 VP/索引，再交给 FrameData 上传；光追路线只更新所需视图数据。
3. Graph execute 重置并开始 CommandBuffer，按 compile 得到的 sortedNodes 顺序应用 use 声明的 barrier 并调用节点执行回调。Pass 直接绑定当前 FrameData 的外部 Scene Set，UI 由 EditorUi 节点录制。
4. Graph 将交换链图像转为 Present 状态，结束 CommandBuffer。
5. FrameContext 重置 Fence 并 submit 一次，等待 imageAvailable，完成后通知 acquired image 的 renderFinished；Swapchain present。
6. 若有拾取请求，等待本次 Fence、读取该帧结果，最后轮转 frameIndex。

Graph 保留每个物理资源实例的状态。布局变化或前后涉及写入时插入 pipelineBarrier2；相同布局的连续读取合并 stage/access。每帧 buffer 使用 host-coherent 内存，录制前的 host 写入在 queue submit 时对 GPU 可见；Graph 不负责这些外部 buffer 的更新或状态跟踪。普通帧不等待整个 device 空闲；拾取帧为同步回读额外等待本次提交。

## Resize 与销毁

Renderer 将 acquire/present 的重建需求通过 RenderFrameResult 返回，不处理窗口事件或反向调用 Editor。Engine 在 framebuffer 尺寸非零时调用 Renderer recreateSwapchain，随后通知 Editor refreshUi。交换链重建等待 GPU 空闲、释放旧图资源、重建交换链并组图，各执行一次。Shader、Descriptor Layout、资产 GPU 资源和 FrameContext 不因 resize 重新创建。

Engine 在 Renderer shutdown 前关闭 Editor 并清空 RenderSystem 的 CPU 数据。Renderer shutdown 先等待 device 空闲，再释放 Graph/Pass、资产 GPU 资源、Pipeline、FrameContext、Descriptor、Shader、Swapchain，最后释放 VulkanContext。外部资源所有者的生命周期必须覆盖 Graph 的使用时间。

## 验证

`test/RenderGraphTest.cpp` 简单检查 barrier 条件。`test/ShaderDataTest.cpp` 对比编译后的 Slang 反射与 C++ offsetof/sizeof，检查所有 shader 的 View/Light UBO 和 SSBO 元素布局及绑定。RenderGraphTests 不需要 GPU。

`test/RenderGraphGpuSmoke.cpp` 是需要 Vulkan 和当前配置资产的手动集成测试，不加入自动 CTest。显式构建 RenderGraphGpuSmoke 后运行，检查两帧资源轮转、真实 Shadow 绘制与清空、Skybox、重复拾取、运行中材质/纹理更新、多灯光、禁用灯光、零灯光、两帧 SSBO 扩容、Graph 重建和正常销毁；仅修改内存中的场景。
`RenderGraphGpuSmoke` 从构建根目录运行（例如 `cmake-build-default` 下的 `test/RenderGraphGpuSmoke.exe`），以便找到构建后的 shader。可设置 `VK_LAYER_VALIDATE_SYNC=1` 启用同步 validation。
