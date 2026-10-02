# Renderer

`Renderer` 组织 Vulkan 初始化、场景加载和逐帧绘制。长期 device 对象、资产 GPU 资源、图资源和 frame-in-flight 数据分别管理生命周期。

## 模块与所有权

| 模块 | 职责 | 生命周期 |
| --- | --- | --- |
| `VulkanContext` | Instance、PhysicalDevice、Device、Queues、queue family、features/properties | 整个 Renderer |
| `Swapchain` | 交换链、图像和视图、每张图像的 renderFinished Semaphore | 每次交换链创建至重建 |
| `ShaderManager` | SPIR-V 与反射加载、VkShaderModule 缓存、ShaderHandle | device 级 |
| `DescriptorManager` | 预设和反射 Layout、Layout cache、Pool、Set allocation | Layout/Pool 为 device 级；Set 随使用者 |
| `PipelineManager` | PipelineLayout、RenderMode 模板、PipelineKey cache | device 级 |
| `RenderResourceManager` | Mesh/Texture/Material/Sampler 资产到 GPU 对象的映射和缓存 | 资产级；当前缓存至 Renderer 关闭 |
| `RenderGraph` | 注册 Pass、解析 slot、校验依赖、合并 usage、分配内部资源、barrier 和命令录制 | Graph 长期存在；内部资源在 build 时重建 |
| `FrameContext` | CommandPool/Buffer、Fence、imageAvailable、View/Light UBO、灯光 SSBO、Scene Set | 每个 frame-in-flight 一份 |

Renderer 持有上述模块和 `GpuScene`。Graph 持有 Shadow、Scene、EditorPicking、EditorUi 节点。Skybox 的 ShaderHandle、Pipeline/Layout 引用和命令录制统一放在 ScenePass 内部。Pipeline 由 PipelineManager 缓存，Pass 通过自己的 input slot 获取图资源。

当前 `maxFramesInFlight` 为 2。Graph 为每个在途帧分配独立的 Scene 深度图、Shadow cubemap、拾取图和 readback Buffer。交换链图像和资产环境纹理作为外部资源导入，Graph 借用句柄并跟踪状态。FrameContext 的 View/Light UBO 和灯光 SSBO 由 Pass 直接引用，不登记到 Graph slot；Renderer 在录制前更新当前帧的数据。Mesh/Material 的 vertex/index buffer、材质 UBO 和 base color image 由 GpuScene 直接引用，GPU 对象由 RenderResourceManager 创建和持有。它们不登记到 Graph slot，上传与可供绘制使用的状态由资产 GPU 资源实现负责。

## 引擎预设接口

绑定编号和 GPU 数据结构见 `resource/ShaderData.h`。

| Set | Binding | 资源 |
| --- | --- | --- |
| 0 Scene | 0 | ViewUniforms UBO |
| 0 Scene | 11 | LightUniforms UBO：light count、IBL 参数 |
| 0 Scene | 12 | LightData[] SSBO |
| 0 Scene | 1 / 2 | Irradiance image / sampler |
| 0 Scene | 3 / 4 | Prefiltered specular image / sampler |
| 0 Scene | 5 / 6 | BRDF LUT image / sampler |
| 0 Scene | 7 / 8 | Radiance image / sampler |
| 0 Scene | 9 / 10 | Point shadow cubemap image / sampler |
| 1 Material | 0 | Base color image |
| 1 Material | 1 | Base color sampler |
| 1 Material | 2 | MaterialUniforms UBO |

FrameContext 持有 Scene Set、两个 UBO 和灯光 SSBO。ViewUniforms 包含 viewProjection、inverseViewProjection 和 cameraPosition，共 144 字节。LightUniforms 包含 lightCount 和 iblParameters，共 32 字节；iblParameters.x 是 prefiltered specular 的最大 LOD。SSBO 按 GpuScene 的灯光顺序存放 LightData，每项 80 字节，包括 color/intensity、position/range、direction、area/cone 和 flags。lightCount 包括禁用灯光，Scene shader 跳过 enabled 为 0 的项，并累加现有点光源计算的结果。

SSBO 至少分配一项以维持有效 descriptor；没有灯光时 lightCount 为 0，shader 不读取数组。数量增长时，在当前帧 fence 完成后扩容并更新该帧 descriptor，其他帧保持自己的 buffer。

GpuMaterial 持有 Material Set。set 2 Object 和 set 3 Pass 的编号已保留，当前没有对应预设 Layout；对象变换、拾取 ID 和 Shadow 的矩阵通过 Push Constant 传递。

`PipelineLayoutPreset` 提供 SceneMaterial（set 0 + set 1）和 SceneOnly（set 0）。`PipelineState::preset(RenderMode)` 提供 Opaque、AlphaTest、Transparent、Shadow、DepthOnly、Skybox、PostProcess、LightMarker、Picking 状态模板。DepthOnly 只是状态模板，目前没有独立的深度预绘制节点。

## Shader、Descriptor 与 Pipeline

构建阶段用 Slang 生成 SPIR-V 和 reflection JSON。ShaderManager 根据 Shader 资产 ID 读取并缓存模块和 descriptor 元数据。DescriptorManager 创建 Scene/Material 预设 Layout，并支持从 reflection 缓存特殊 Layout；PipelineManager 当前使用上述两种预设 PipelineLayout。

PipelineKey 包含 ShaderHandle、PipelineLayout、VertexLayout、Topology、Raster/Depth/Blend/MSAA 状态，以及颜色和深度 attachment 格式。`getOrCreate` 命中时复用；首次创建时检查 shader 的 set/binding/type 与 PipelineLayout 是否一致。Pipeline 跨帧和场景复用。

GpuMaterial 根据 alpha mode 选择 Opaque、AlphaTest 或 Transparent。Material 的 shaderId 未指定时使用 scene shader。GpuTexture 只持有 Image/View；RenderResourceManager 按 Sampler::ID 创建并缓存独立 sampler。材质与环境纹理使用 TextureBinding 指定 image/sampler，BRDF LUT 的绑定来自 Renderer 配置。

## 初始化与加载场景

Renderer init 依次初始化 device、swapchain、各 Manager、FrameContext、RenderResourceManager 和 Graph。Graph init 从 `config/config.json` 的 Renderer.BrdfLut 加载线性 2D LUT，初始化各 Pass，并注册 slot。

Renderer loadScene 等待 GPU 空闲，GpuScene 加载渲染项并建立对资产 GPU 对象的引用，ScenePass 更新每帧 Scene Set 中的环境纹理绑定，随后 Graph build 编译依赖、合并 usage、分配内部资源、更新 Shadow descriptor。具体材质 Pipeline 在首次绘制时创建并缓存。

资产 GPU 缓存跨场景保留。GpuScene 不拥有 Mesh/Texture/Material 的 GPU 存储。

## RenderGraph 的 slot 约定

`init()` 注册全部节点并分配固定 input/output slot。`setPassEnabled(name, enabled)` 决定节点是否参与下一次 build，默认全部启用。

setupPass 中：

- `createResource(input, desc)`：从自己的 input 声明新内部资源。
- `importResource(input, desc, instances)`：导入一个外部资源。同一组物理句柄只跟踪一份。
- `bindInput(input, otherOutput, desc)`：显式依赖另一个 Pass 的 output。
- `bindOutput(output, ownInput, desc)`：output 只引用同一 Pass 自己的 input。
- `ignoreInput(input)`：显式忽略当前 feature 不需要的输入；这个输入不能导出为 output。

ResourceDesc 同时记录创建信息（类型、格式、尺寸、mip/layer、内存属性）和 slot 的 Vulkan usage、pipeline stage、access、image layout。引用既有资源时可以只填写使用状态；物理创建信息由创建资源的 input 决定，usage 在所有 slot 之间合并。

每个 slot 引用一个资源。外部资源的 instances 表示同一资源的共享实例、每帧实例或交换链实例；Graph 按当前 frame index 或取得的 swapchain image index 选择对应实例。

input 状态在 Pass 开始前应用，output 状态在 Pass 结束后应用。拾取 Pass 需要先把 ID 图转换为 TransferSrc，再执行复制，因此在 pass 内通过 `useOutput()` 提前请求该状态；实际 barrier 由 Graph 生成。

build 等待 GPU 空闲，调用启用节点的 setup，并检查未绑定 slot、引用环、禁用的 producer、类型/格式/尺寸不一致、不完整创建信息、外部资源 usage 不足，以及没有依赖顺序的共享资源写入。随后排序、分配资源。关闭节点后仍依赖它的消费者会明确报错；Scene 在 Shadow 关闭时主动忽略这个输入。feature 变化后必须重新 build。

```text
Shadow ── shadow cubemap ──▶ Scene ── depth ──▶ EditorPicking
                              └──── color ──▶ EditorUi
```

Scene 保持一个完整节点，内部顺序为 opaque、Skybox、transparent、灯光标记。Picking 和 UI 之间没有资源依赖，当前按注册顺序执行。

Scene、Shadow 和 Picking 直接使用 GpuScene 的网格引用；Scene 的 DrawCall 引用对应 GpuMaterial，绘制时绑定其 vertex/index buffer 和 Material descriptor set。

材质覆盖在命令录制前解析，生成本帧 DrawCall。RenderResourceManager 获取或创建对应 GPU 材质，材质与纹理调整不改变 Graph 的 slot 和资源分配，也不触发重新编译。

当前状态跟踪粒度为整个 Image 的全部 mip/layer 和整个 Buffer。全图使用一个 graphics/present queue，不进行跨队列调度或资源内存别名复用。

## 绘制行为

Shadow 使用当前 FrameContext 的第一项 LightData，与 shader 中的 lights[0] 对应。启用投影的点光源以 range 为 far plane，从六个方向绘制到 1024 × 1024 × 6 深度 cubemap；fragment 写入 `distance(worldPosition, lightPosition) / range` 的线性深度。没有投影时清为 1。完成后切换到深度只读状态。Scene shader 目前只声明 set 0 的 Shadow 资源，尚不采样；Shadow 绘制暂未处理透明材质和 AlphaTest 镂空。

有环境贴图时，Scene 内部在 opaque 之后绘制 Skybox。fullscreen triangle 的深度为 1，fragment 从 ViewUniforms 读取逆视投影矩阵和相机位置，结合 push constant 中的编辑器视口重建世界方向，采样 radiance 基础 mip。保留深度测试、关闭深度写入，并请求早期深度测试。随后透明几何按距离从远到近绘制，再绘制灯光标记。

Picking 复用 Scene 深度，只在请求拾取时绘制点击像素的 selection ID、复制至该帧的回读 Buffer。深度以只读 attachment 使用。

## 每帧流程

1. Renderer 等待当前 FrameContext 的 Fence，Acquire 交换链图像，重置 Fence。
2. Renderer 更新当前帧的 View/Light UBO 和灯光 SSBO，再调用 Graph prepareRenderData 解析材质覆盖、准备 DrawCall；这些都在 execute 之前完成。
3. Graph execute 重置并开始 CommandBuffer，按编译顺序应用 input barrier、调用 executePass、应用 output 状态；Pass 直接绑定当前 FrameContext 的外部 Scene Set，UI 由 EditorUi 节点录制。
4. Graph 将交换链图像转为 Present 状态，结束 CommandBuffer。
5. Renderer 统一 submit 一次，等待 imageAvailable，完成后通知 acquired image 的 renderFinished；随后 Present。
6. 若有拾取请求，等待本次 Fence、读取该帧结果，最后轮转 frameIndex。

Graph 保留每个物理资源实例的状态。布局变化或前后涉及写入时插入 pipelineBarrier2；相同布局的连续读取合并 stage/access。每帧 buffer 使用 host-coherent 内存，录制前的 host 写入在 queue submit 时对 GPU 可见；Graph 不负责这些外部 buffer 的更新或状态跟踪。普通帧不等待整个 device 空闲；拾取帧为同步回读额外等待本次提交。

## Resize 与销毁

最小化时等待非零 framebuffer 尺寸。重建时先等待 GPU 空闲，再重建交换链图像、视图和 presentation Semaphore，调用 Graph build 重建内部图资源、更新 attachment Pipeline/descriptor，最后刷新编辑器 UI。Shader、Descriptor Layout、资产 GPU 资源和 FrameContext 不因 resize 重新创建。

shutdown 先等待 device 空闲，再释放 GpuScene、Graph/Pass、资产 GPU 资源、Pipeline、FrameContext、Descriptor、Shader、Swapchain，最后释放 VulkanContext。外部资源所有者的生命周期必须覆盖 Graph 的使用时间。

## 验证

`test/RenderGraphTest.cpp` 检查 slot 解析、usage 合并、排序、无效绑定/环、feature 关闭、共享资源 hazard 和 barrier 条件。`test/ShaderDataTest.cpp` 对比编译后的 Slang 反射与 C++ offsetof/sizeof，检查所有 shader 的 View/Light UBO 和 SSBO 元素布局及绑定。RenderGraphTests 不需要 GPU。

`test/RenderGraphGpuSmoke.cpp` 是需要 Vulkan 和当前配置资产的手动集成测试，不加入自动 CTest。显式构建 RenderGraphGpuSmoke 后运行，检查两帧资源轮转、真实 Shadow 绘制与清空、Skybox、重复拾取、运行中材质/纹理更新、多灯光、禁用灯光、零灯光、两帧 SSBO 扩容、Graph 重建和正常销毁；仅修改内存中的场景。
`RenderGraphGpuSmoke` 从构建根目录运行（例如 `cmake-build-default` 下的 `test/RenderGraphGpuSmoke.exe`），以便找到构建后的 shader。可设置 `VK_LAYER_VALIDATE_SYNC=1` 启用同步 validation。
