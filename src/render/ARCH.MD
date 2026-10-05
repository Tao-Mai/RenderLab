# RenderGraph

Graph 和 Pass 按当前 Renderer 直接耦合。slot 表负责图资源的依赖和校验；场景对象的 Mesh/Material GPU 资源通过 RenderSystem 直接引用。

## 生命周期

1. init：初始化 Pass，注册所有节点，分配固定 slot。
2. build：只调用启用节点的 setupPass，解析绑定、检查依赖、合并 usage、排序、分配资源、更新 descriptor。
3. Renderer 等待当前帧 fence 后调用 RenderSystem prepare，整理网格/灯光的 DrawItem、材质覆盖、透明排序和 LightData，再上传 View/Light UBO 与灯光 SSBO；随后 Graph execute 根据各 Pass 的 input 状态插入 barrier，依次调用 executePass，把命令录制进当前 FrameContext 的一个 CommandBuffer。
4. Renderer 统一 submit/present；reload scene 或 resize 时重新 build。

## 资源边界

- Graph 拥有：每帧 Scene depth、Shadow cubemap array、Picking image、Picking readback buffer。
- 外部导入：swapchain images/views、环境纹理和 BRDF LUT。
- FrameContext 持有一个 FrameData，后者管理 ViewUniforms/LightUniforms UBO、LightData[] 和 PointShadowData[] SSBO。上传时分配有效点光源的 shadowidx，并计算六面 VP；Shadow 用 multiview 渲染六层。Pass 直接引用该帧的 Scene Set；这些 buffer 不经过 slot。
- Mesh/Material 的 buffer、材质纹理由 RenderSystem 直接引用，RenderResourceManager 创建和持有，资产 GPU 资源实现负责上传同步和可供绘制使用的状态。
- Pipeline/Shader/Descriptor 的长期缓存由对应 Manager 拥有。

## slot 规则

- 新内部资源只能从自己的 input 创建。
- input 可以引用其他节点的 output。
- output 只能引用同一节点自己的 input。
- 每个 input 的 ResourceDesc 声明直接的 Vulkan usage/stage/access/layout。
- 每个 slot 引用一个资源；外部资源的 instances 区分共享、每帧和交换链实例。
- DrawItem 和材质覆盖在录制前准备，直接引用资产 GPU 对象，不触发 Graph 重新编译。
- 多个 slot 可以引用同一物理资源，但跨节点写入必须存在依赖顺序，注册顺序不用于解决资源 hazard。
- output 只映射到本节点的 input，不声明状态。Graph 在 Pass 前转换 input 状态；需要切换状态的操作拆成多个节点。

光栅路线的 Scene 保持一个节点，Skybox 的 shader 加载、pipeline 获取和绘制均由 ScenePass 内部负责。Shadow → Scene，Scene depth → Picking，Scene color → EditorUi。默认启用光栅路线及编辑器节点，RayTracing 默认关闭；通过 setPassEnabled 后重新 build 调整 feature。

Picking 分成绘制 ID 图、复制点击像素、声明 HostRead 三个节点。Graph 根据 input 依赖完成图像和 readback buffer 的状态转换，CPU 仍在 Renderer 等待提交 fence 后读取结果。

`config/renderer.json` 的 `renderPath` 选择 RayTracing 时，用 RayTracing 和 RayTracingBlit 节点替代 Shadow/Scene：BLAS/TLAS → TraceRays → 光追图像 → RayTracingBlit → EditorUi。Picking 在该路线下拥有独立深度附件，不依赖 Scene。路线切换和 resize 均在 waitIdle 后 build；资产 GPU 对象和 BLAS 缓存继续复用，每帧 TLAS 与光追描述符在对应 frame fence 完成后更新。加速结构构建/复用的内部内存同步暂时保留，不经过 Graph 的图资源 slot。

全图使用一个 graphics/present queue，状态粒度为整个 Image/Buffer。详细接口和逐帧流程见 [README](README.md)。
