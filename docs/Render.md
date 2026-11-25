# 渲染模块

[项目首页](../README.md) · [架构](Architecture.md) · [RenderGraph](RenderGraph.md) · [Shader 参数与管线](RenderLayout.md)

渲染模块接收场景快照和编辑器输入。C++ 代码创建 Pass 并连接资源端口，Renderer 准备 GPU 数据并交由 RenderGraph 调度。

`RenderLabCore` 编译 GPU 资源、RenderGraph 和内置 Pass。各 Pass 的 C++ 与 Slang 源码按照职责放在 [src/render](../src/render/) 对应目录。
MinimalPathTracer 渲染场景，内置渲染图将其 HDR 颜色接入后处理。窗口呈现渲染图的首个输出，编辑器 UI 叠加在场景颜色上。 [路径追踪](MinimalPathTracer.md) · [组图实现](../src/render/impl/graphs/minimal/MinimalPathTracerGraph.cpp)

Renderer 每帧提交 Scene。GpuScene 比较场景对象和资产版本，按子网格更新 GPU 实例和材质绑定。网格与子网格范围对应的 BLAS 可复用，实例几何或变换变化时重建 TLAS。相机、光源和材质数据通过具名绑定传给 Slang。 [场景更新](../src/render/GpuScene.cpp)

## 帧与资源

下述曝光流程对应 `Post process = RenderLab`；`Falcor` 选项使用独立的 [Falcor 后处理](PostProcess.md)。


[ExposurePass](../src/render/impl/postprocess/ExposurePass/ExposurePass.h) 在 Bloom 之前读取场景 HDR 颜色。自动模式每隔两个像素测光，将线性 HDR 亮度映射到 256 档对数直方图，按配置裁掉两端百分位后计算代表亮度，并换算为目标 EV100。目标值限制在 EV 范围内，依据帧时间和明暗适应速度进行指数平滑；首次有效测光、切换场景和图重建时直接初始化。全黑画面保留已有曝光，等待首次有效测光。手动模式直接使用配置值。

曝光状态保存在 GPU 缓冲中，由 ToneMappingPass 直接读取并作用于线性 HDR 结果。两个 Pass 共用 [ExposureState](../src/render/impl/postprocess/exposure_state.slang) 的布局。曝光补偿同时适用于两种模式，曝光调整保留离线光照累计。

- 默认两帧在途，离线光追使用一个槽位；FrameContext 以 RHI Fence 等待槽位完成。
- GpuResources 保留图像、缓冲的稳定 handle、容量与历史版本，Pass 通过 getImage / getBuffer 访问。
- CPU 上传数据由 RHI encoder 复制，原生 staging 分配、资源状态和读写同步由 slang-rhi 管理。
- Renderer 直接获取和呈现 Surface 返回的原生纹理。
- Pass 使用原生 ShaderCursor 按名称设置资源与常量，RHI 在录制时保存 shader object 的参数快照。
- Pass 直接持有原生 GPU 管线；材质变体和离线光追管线使用业务缓存，底层布局由 Slang 反射和 RHI 创建。

## 代码导航

| 位置 | 内容 |
| --- | --- |
| [Renderer.cpp](../src/render/Renderer.cpp) | 图重建、全局帧准备和提交 |
| [MinimalPathTracerGraph.cpp](../src/render/impl/graphs/minimal/MinimalPathTracerGraph.cpp) | 路径追踪与后处理连接 |
| [graph/](../src/render/graph/) | RenderGraph、Pass 注册与最终出口 |
| [impl/passes/](../src/render/impl/passes/) · [impl/raytracing/](../src/render/impl/raytracing/) | 路径追踪 Pass 与光追 Shader |
| [impl/postprocess/](../src/render/impl/postprocess/) | 曝光、Bloom 和色调映射 |
| [SceneAccelerationStructures.cpp](../src/render/resources/SceneAccelerationStructures.cpp) | 场景 BLAS / TLAS 的构建与缓存 |
| [resources/](../src/render/resources/) | GPU 资源、缓存、材质表与 Shader 管理 |
| [RhiUtils.h](../src/render/RhiUtils.h) | RHI 调用检查和缓冲区扩容 |
