# RenderGraph

Graph 和 Pass 按当前 Renderer 直接耦合。各 Pass 直接接收类型化资源句柄或前序 Pass 的 Output，返回 Output，不再使用 Input 结构或 slot 表。

Renderer 持有 Pass 对象，类型在 Renderer.cpp 的 tuple 类型列表中集中声明，通过 setupPass<T> 按类型访问。
Graph 通过 addPass 登记节点，节点记录依赖、资源访问和执行回调。setup 的隐式切换限定在模板内部，返回时自动恢复。Graph 头文件定义通用登记和反射模板，不引入具体 Pass 头文件。

## 生命周期

1. Renderer init：初始化 Graph，创建 Pass 对象。
2. Renderer setupRenderGraph：等待 GPU 空闲并释放旧图资源，按配置显式串接 Pass，调用 setup 收集 use、合并 usage。最后通过 setupPass<EndPass> 设置终点，自动收集有效的颜色和 readback handle 依赖，声明 Present 和 HostRead 出口状态。
3. Graph compile：由 Renderer 显式调用，从终点 BFS 回溯资源生产者，给保留节点补充写前读依赖，再做拓扑排序并分配资源。重复写和循环在所有构建中通过 CHECK 检查。资源分配完成后，Renderer 调用 ScenePass 绑定阴影 descriptor。
4. Engine 调用 RenderSystem 整理 DrawItem 和 LightData，再将 RenderData 交给 Renderer；Renderer 等待当前帧 fence，FrameData 上传相机、灯光和点阴影六面 VP。
5. Graph 遍历 sortedNodes，先应用节点的资源状态，再调用执行回调。EndPass 在最后应用出口状态。
6. Renderer 保存 PickingCopy 的 readback handle，统一 submit/present；fence 完成后由 Renderer 通过 Graph 的 getBuffer 下载 selection ID。Graph 不负责结果解释或拾取状态。

compile 将保留节点的拓扑顺序保存到 sortedNodes，execute 只执行这些节点，被剪枝节点不录制命令。当前不做资源复用或跨队列调度。状态粒度为整个 Image/Buffer。

## 资源边界

- Graph 持有每帧的 Scene depth、点阴影 cubemap array、Picking image、readback buffer 和光追输出图像；交换链为借用资源。资产纹理不登记到 Graph，由资产 GPU 资源负责上传和采样状态，Pass 直接绑定 descriptor。
- 每个图像实例持有 Image，View 缓存在 Image 内。Pass 通过 getImage 获取引用，再调用 getOrCreateImageView；Graph 不提供 View 缓存或转发接口。
- FrameContext/FrameData 持有 UBO、灯光和点阴影 SSBO、Scene Set；Mesh/Material 的资产 GPU 对象由 RenderResourceManager 缓存。
- Renderer 通过 RenderContext 暴露 Shader/Descriptor/Pipeline/Resource Manager，并负责 init/shutdown。

## 当前路线

光栅依赖：PointShadow → Scene → LightMarker，随后分为 EditorUi 和 Picking → PickingCopy 两条分支，最终汇合到 EndPass。无依赖关系的节点之间不要求固定先后顺序。
Scene 内部依次绘制 opaque、Skybox、transparent；LightMarkerPass 随后加载场景颜色和深度绘制灯光标记。关闭阴影时不绘制阴影，并清除上传数据中的 castShadow 标记，但保留资源和 descriptor。

光追依赖：RayTracing → RayTracingBlit → EditorUi 与 Picking → PickingCopy 两条分支最终汇合到 EndPass。
Picking 创建独立的深度图。光追的 BLAS/TLAS 构建同步仍由原实现负责，不纳入图资源状态跟踪。

无拾取请求时 Picking 和 PickingCopy 在 execute 内提前返回，不绘制或复制；Graph 仍按节点声明处理资源状态。Resize 时先释放 Graph 资源和缓存 View，再销毁交换链，最后由 Renderer 重新组图和 compile。

详细接口和逐帧流程见 [README](README.md)。
