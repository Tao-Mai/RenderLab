# Renderer

`Renderer` 负责组织 Vulkan 初始化、场景资源上传和逐帧绘制。各类资源按生命周期分配所有权：设备级对象在 `Renderer` 存续期间复用；交换链相关对象在窗口尺寸变化时重建；网格、纹理和材质按资产创建；命令缓冲与帧数据按 frame-in-flight 复用。

## 模块与所有权

| 模块 | 主要职责 | 生命周期 |
| --- | --- | --- |
| [`VulkanContext`](device/VulkanContext.h) | Instance、物理设备、逻辑设备、队列、队列族、设备属性与特性 | 整个 `Renderer` |
| [`Swapchain`](present/Swapchain.h) | 交换链图像和视图、深度图、拾取图、presentation 同步对象 | 每次创建交换链到下次 resize |
| [`ShaderManager`](ShaderManager.h) | 按资产 ID 加载 SPIR-V 和反射信息，缓存 `VkShaderModule`，返回 `ShaderHandle` | 设备级 |
| [`DescriptorManager`](DescriptorManager.h) | 预设及缓存的 DescriptorSetLayout、DescriptorPool、DescriptorSet 分配 | Layout/Pool 为设备级；Set 随使用者销毁 |
| [`PipelineManager`](PipelineManager.h) | 预设 PipelineLayout、渲染状态模板、按 `PipelineKey` 查找或创建 Pipeline | 设备级 |
| [`RenderResourceManager`](resource/RenderResourceManager.h) | 将 Mesh、Texture、Material 资产映射到 GPU 资源，并缓存上传结果 | 资产级；当前缓存到 `Renderer` 关闭 |
| [`FrameContext`](device/FrameContext.h) | CommandPool、CommandBuffer、Fence、image-available Semaphore、Scene UBO 与 Scene DescriptorSet | 每个 frame-in-flight 一份 |

[`Renderer`](Renderer.h) 持有上述模块，并协调 [`GpuScene`](scene/GpuScene.h)、[`ScenePass`](pass/ScenePass.h) 和 [`EditorPickingPass`](pass/EditorPickingPass.h)。Pass 负责录制绘制命令；Pipeline 和分辨率相关图像分别由对应的 Manager 与 `Swapchain` 持有。

当前 [`maxFramesInFlight`](RenderConfig.h) 为 `2`。`Swapchain` 为每个在途帧创建独立深度图，并为每张交换链图像创建 `renderFinished` Semaphore；`FrameContext` 的 `imageAvailable` Semaphore 和 Fence 则按帧复用。拾取图属于 `Swapchain`，拾取结果的 readback Buffer 属于 `EditorPickingPass`。

## 引擎预设接口

绑定编号和 GPU 数据结构定义在 [`ShaderData.h`](resource/ShaderData.h)。当前预设如下：

| Descriptor set | Binding | 资源 | 实例持有者 |
| --- | --- | --- | --- |
| `0` Scene | `0` | `SceneUniforms` UBO | `FrameContext` |
| `0` Scene | `1` | 环境贴图 sampled image | `FrameContext` 的 Scene Set，引用场景纹理 |
| `0` Scene | `2` | 环境贴图 sampler | `FrameContext` 的 Scene Set，引用场景纹理 |
| `1` Material | `0` | Base Color sampled image | `Material` |
| `1` Material | `1` | Base Color sampler | `Material` |
| `1` Material | `2` | `MaterialUniforms` UBO | `Material` |

`set 2` Object 和 `set 3` Pass 的编号已保留，但当前没有对应的预设 DescriptorSetLayout。对象变换和拾取 ID 目前通过 Push Constant 传递。

`PipelineLayoutPreset` 当前提供 `SceneMaterial`（set 0 + set 1）和 `SceneOnly`（仅 set 0）。`PipelineState::preset(RenderMode)` 定义了 Opaque、AlphaTest、Transparent、Shadow、DepthOnly、Skybox、PostProcess、LightMarker、Picking 的状态模板；实际绘制路径目前使用场景材质、灯光标记与拾取相关模式。新增 Pass 时，需要接入相应的录制流程。

## Shader、Descriptor 与 Pipeline

构建阶段由 [`CMakeLists.txt`](../../CMakeLists.txt) 调用 Slang，生成 `.spv` 和同名 `.reflection.json`。`ShaderManager::getOrLoad(id)` 读取 [`ShaderDesc`](../asset/AssetDesc.h) 的 binary 路径及对应反射文件，缓存模块、descriptor binding 元数据，并返回 `ShaderHandle`。多个 `PipelineKey` 可以引用同一个 handle。

`DescriptorManager` 创建 Scene、Material 预设 Layout，并按 binding、类型、数量和 stage flags 缓存其他 Layout。`getOrCreateLayout(shaderMetadata, set)` 可根据反射信息生成特殊 DescriptorSetLayout。当前 `PipelineManager` 只创建上述两种预设 PipelineLayout；使用特殊 Layout 的 Pass 还需要增加对应的 PipelineLayout 与绑定逻辑。

`PipelineKey` 由 ShaderHandle、PipelineLayout 预设、顶点布局、拓扑、光栅化/深度/混合/MSAA 状态，以及颜色和深度 attachment 格式组成。`PipelineManager::getOrCreate` 命中缓存时复用 Pipeline；首次创建时检查 Shader 反射的 set/binding/type 是否在所选预设 Layout 中。Pipeline 不随 frame 或 scene 重建。当前图形 Pipeline 使用 Shader 的 `vertMain` 和 `fragMain` 入口。

Material 可通过 `MaterialDesc::shaderId` 选择 Shader；未指定时使用 `scene`。Material 根据 alpha mode 选择 Opaque、AlphaTest 或 Transparent 状态，持有自己的 UBO 和 DescriptorSet。透明物体在 Scene Pass 中按距离从远到近绘制。

## 初始化与加载场景

`Renderer::init()` 依次初始化 `VulkanContext`、`Swapchain`、`ShaderManager`、`DescriptorManager`、`PipelineManager`、各 `FrameContext`、`RenderResourceManager` 和 Pass。初始化阶段加载场景、灯光及拾取 Shader，并创建对应的基础 Pipeline。

`Renderer::loadScene(scene)` 等待当前 GPU 工作结束，然后由 `GpuScene` 建立场景对象与渲染项的引用。`RenderResourceManager` 按需加载网格、纹理和材质，上传 Buffer/Image，并为 Material 分配 DescriptorSet；随后 Scene Pass 将环境贴图写入每个 FrameContext 的 Scene Set。具体材质组合的 Pipeline 在首次绘制时按 `PipelineKey` 创建，之后复用。

当前资产缓存跨场景加载保留，到 `Renderer::shutdown()` 才统一释放。`GpuScene` 持有渲染项和场景对象引用，不拥有 Mesh/Texture/Material 的 GPU 存储。

## 每帧流程

`Renderer::render(camera, editor)` 的主要顺序：

1. 等待当前 `FrameContext` 的 Fence，取得交换链图像；若交换链过期，进入重建流程。
2. 重置 Fence，更新该帧的 Scene UBO，并重置/开始录制 CommandBuffer。
3. 录制 Scene Pass；按需要录制拾取 Pass；最后录制编辑器 UI。
4. 提交命令：等待该帧的 `imageAvailable`，提交完成后信号通知所取得交换链图像的 `renderFinished`。
5. Present；如果请求拾取，等待本次 Fence 后读取 selection ID。最后轮转 `frameIndex`。

普通帧不会等待整个设备空闲。拾取需要同步读取结果，因此请求拾取的帧会额外等待本次提交完成。

## Resize 与销毁

`Renderer::recreateSwapchain()` 在窗口最小化时等待非零 framebuffer 尺寸；随后等待 GPU 空闲，重建交换链图像、视图、每帧深度图、拾取图和 presentation Semaphore，刷新 Scene/Picking Pipeline 的 attachment 格式，并通知编辑器刷新 UI。Shader、Descriptor Layout、Mesh、Texture、Material 和 FrameContext 不因 resize 重新创建。

`Renderer::shutdown()` 先等待设备空闲，再依次释放场景、Pass、资产 GPU 资源、Pipeline、FrameContext、Descriptor、Shader、Swapchain，最后释放 `VulkanContext`。新增 Vulkan 对象时，应把其所有权放在相应生命周期的模块中，并检查其依赖对象的销毁顺序。
