# RenderGraph

Graph 和 Pass 按当前 Renderer 直接耦合。slot 表负责图资源的依赖和校验；场景对象的 Mesh/Material GPU 资源通过 RenderSystem 直接引用。

## 生命周期

1. init：初始化 Pass，注册所有节点，分配固定 slot。
2. build：只调用启用节点的 setupPass，解析绑定、检查依赖、合并 usage、排序、分配资源、更新 descriptor。
3. Renderer 等待当前帧 fence 后调用 RenderSystem prepare，整理网格/灯光的 DrawItem、材质覆盖、透明排序和 LightData，再上传 View/Light UBO 与灯光 SSBO；随后 Graph execute 在 input/output 状态之间插入 barrier，依次调用 executePass，把命令录制进当前 FrameContext 的一个 CommandBuffer。
4. Renderer 统一 submit/present；reload scene 或 resize 时重新 build。

## 资源边界

- Graph 拥有：每帧 Scene depth、Shadow cubemap、Picking image、Picking readback buffer。
- 外部导入：swapchain images/views、环境纹理和 BRDF LUT。
- FrameContext 持有一个 FrameData，后者管理 ViewUniforms/LightUniforms UBO 和 LightData[] SSBO。Renderer 在 execute 前更新，Pass 直接引用该帧的 Scene Set；这些 buffer 不经过 slot。
- Mesh/Material 的 buffer、材质纹理由 RenderSystem 直接引用，RenderResourceManager 创建和持有，资产 GPU 资源实现负责上传同步和可供绘制使用的状态。
- Pipeline/Shader/Descriptor 的长期缓存由对应 Manager 拥有。

## slot 规则

- 新内部资源只能从自己的 input 创建。
- input 可以引用其他节点的 output。
- output 只能引用同一节点自己的 input。
- 每个使用 slot 的 ResourceDesc 声明直接的 Vulkan usage/stage/access/layout。
- 每个 slot 引用一个资源；外部资源的 instances 区分共享、每帧和交换链实例。
- DrawItem 和材质覆盖在录制前准备，直接引用资产 GPU 对象，不触发 Graph 重新编译。
- 多个 slot 可以引用同一物理资源，但跨节点写入必须存在依赖顺序，注册顺序不用于解决资源 hazard。
- output 是节点的最终使用状态；中途需要该状态时调用 Graph 的 useOutput。

Scene 保持一个节点，Skybox 的 shader 加载、pipeline 获取和绘制均由 ScenePass 内部负责。Shadow → Scene，Scene depth → Picking，Scene color → EditorUi。节点默认全部启用，通过 setPassEnabled 后重新 build 调整 feature。

全图使用一个 graphics/present queue，状态粒度为整个 Image/Buffer。详细接口和逐帧流程见 [README](README.md)。
