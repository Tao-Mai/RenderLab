# RenderGraph

Renderer 负责业务组图，Graph 负责节点、资源和执行。Graph 显式引用 VulkanContext、Swapchain 和 FrameContext，不引用 Renderer。各 Pass 直接接收类型化资源句柄或前序 Pass 的 Output，返回 Output，不使用 Input 结构或 slot 表。

Renderer 持有 Pass 对象，类型在 Renderer.cpp 的 tuple 类型列表中集中声明，通过 setupPass<T> 按类型访问。
Graph 通过 addPass 登记节点，节点记录依赖、资源访问和执行回调。setup 的隐式切换限定在模板内部，返回时自动恢复。Graph 头文件定义通用登记和反射模板，不引入具体 Pass 头文件。

## 生命周期

1. Renderer init：初始化 Graph，创建 Pass 对象。
2. Renderer rebuildGraph：等待 GPU 空闲并释放旧图资源，再由 setupRenderGraph 按配置串接 Pass，调用 setup 收集 use、合并 usage。最后通过 setupPass<EndPass> 设置终点，声明 Present 和 HostRead 出口状态。场景对象和 Mesh 更新不触发图重建。
3. Graph compile：由 Renderer 显式调用，从终点 BFS 回溯资源生产者，补充写前读依赖，拓扑排序并分配资源，然后执行保留节点可选的 prepare 回调。ScenePass 和 ToneMappingPass 在 prepare 中绑定图资源 descriptor。重复写和循环在所有构建中通过 CHECK 检查。
4. Engine 调用 RenderSystem 整理场景数据，Editor 准备普通 RenderView 和灯光标记；Renderer 等待当前帧 fence，ScenePass 组织光照参数并调用 FrameData 上传相机、灯光和阴影数据。
5. Graph 遍历 sortedNodes，先应用节点的资源状态，再调用执行回调。EndPass 在最后应用出口状态。
6. FrameContext submit，Swapchain present；Renderer 等待拾取 fence 后下载 selection ID。Renderer 返回交换链重建需求，Engine 协调重建和 Editor UI 刷新。Graph 不负责结果解释或拾取状态。

compile 将保留节点的拓扑顺序保存到 sortedNodes，execute 只执行这些节点，被剪枝节点不录制命令。当前不做资源复用或跨队列调度。状态粒度为整个 Image/Buffer。

## 资源边界

- Graph 持有每帧的 Scene depth、点阴影 cubemap array、Picking image、readback buffer 和光追输出图像；交换链为借用资源。资产纹理不登记到 Graph，由资产 GPU 资源负责上传和采样状态，Pass 直接绑定 descriptor。
- 每个图像实例持有 Image，View 缓存在 Image 内。Pass 通过 getImage 获取引用，再调用 getOrCreateImageView；Graph 不提供 View 缓存或转发接口。
- FrameContext/FrameData 持有 UBO、灯光和点阴影 SSBO、Scene Set；Mesh/Material 的资产 GPU 对象由 RenderResourceManager 缓存。
- Renderer 通过 RenderContext 暴露 Shader/Descriptor/Pipeline/Resource Manager，并负责 init/shutdown。

## 当前路线

光栅依赖：Shadow / DepthPrepass → Scene → Skybox → Transparent → ToneMapping → LightMarker，随后分为 EditorUi 和 Picking → PickingCopy 两条分支，最终汇合到 EndPass。Skybox、ToneMapping 和 Shadow 按配置登记。关闭阴影时不登记 ShadowPass，ScenePass 提供占位纹理，并清除上传数据中的 castShadow 标记。

光追依赖：RayTracing（RGBA16F）→ ToneMapping，与 EditorDepthPass 汇合到 LightMarker，再分为 EditorUi 和 Picking → PickingCopy 两条分支，最终汇合到 EndPass。关闭 HDR 时用 RayTracingBlit 代替 ToneMapping。Picking 读取前序深度，不创建独立深度图。光追的 BLAS/TLAS 构建同步仍由原实现负责，不纳入图资源状态跟踪。

无拾取请求时 Picking 和 PickingCopy 在 execute 内提前返回，不绘制或复制；Graph 仍按节点声明处理资源状态。Resize 时先释放 Graph 资源和缓存 View，再销毁交换链，最后由 Renderer 重新组图和 compile。

详细接口和逐帧流程见 [README](README.md)。
