# RenderGraph

Graph 和 Pass 按当前 Renderer 直接耦合。各 Pass 直接接收类型化资源句柄或前序 Pass 的 Output，返回 Output，不再使用 Input 结构或 slot 表。

Pass 类型在 RenderGraph.cpp 的 tuple 类型列表中集中声明，通过 setupPass<T>/executePass<T> 按类型访问。
每项保存 Pass 对象和 Graph 的资源访问记录；setup 的隐式切换限定在模板内部，返回时自动恢复。Graph 头文件只声明模板，不引入 Pass 头文件。

## 生命周期

1. init：选择深度格式，创建 Pass 对象。
2. setup：等待 GPU 空闲，按 renderer 配置显式串接 Pass，调用 setup 收集 use，合并 usage 并分配资源。ScenePass 在分配完成后绑定阴影 descriptor。
3. Renderer 等待当前帧 fence，RenderSystem 整理 DrawItem 和 LightData，FrameData 上传相机、灯光和点阴影六面 VP。
4. Graph 按 setup 顺序在各 Pass 前插入 barrier，再调用 execute。最后将交换链图像转换为 Present。
5. Renderer 统一 submit/present；拾取结果在 fence 完成后读取。

当前先不做拓扑排序、剪枝、资源复用或跨队列调度。状态粒度为整个 Image/Buffer。

## 资源边界

- Graph 持有每帧的 Scene depth、点阴影 cubemap array、Picking image、readback buffer 和光追输出图像；交换链和资产纹理为借用资源。
- 每个图像实例持有 Image，View 缓存在 Image 内。Pass 通过 getImage 获取引用，再调用 getOrCreateImageView；Graph 不提供 View 缓存或转发接口。
- FrameContext/FrameData 持有 UBO、灯光和点阴影 SSBO、Scene Set；Mesh/Material 的资产 GPU 对象由 RenderResourceManager 缓存。
- Renderer 通过 RenderContext 暴露 Shader/Descriptor/Pipeline/Resource Manager，并负责 init/shutdown。

## 当前路线

光栅：PointShadow → Scene → Picking → PickingCopy → PickingReadback → EditorUi。
Scene 内部仍依次绘制 opaque、Skybox、transparent、灯光标记。关闭阴影时不绘制阴影，并清除上传数据中的 castShadow 标记，但保留资源和 descriptor。

光追：RayTracing → RayTracingBlit → Picking → PickingCopy → PickingReadback → EditorUi。
Picking 创建独立的深度图。光追的 BLAS/TLAS 构建同步仍由原实现负责，不纳入图资源状态跟踪。

无拾取请求时跳过三个 Picking 节点。Resize 时先释放 Graph 资源和缓存 View，再销毁交换链，最后重新 setup。

详细接口和逐帧流程见 [README](README.md)。
