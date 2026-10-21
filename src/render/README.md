# 渲染模块

[项目首页](../../README.md) · [架构](../../docs/architecture.md) · [RenderGraph](ARCH.MD) · [Descriptor / Pipeline Layout](../../docs/render_layout.md)

渲染模块接收场景快照和编辑器输入，准备 GPU 数据，再通过 RenderGraph 调度完整的渲染 Pass。Renderer 负责流程，Pass 负责 Vulkan 绘制细节。

## 四条路线

| 路线 | 主流程 | 用途 |
| --- | --- | --- |
| Forward | DepthPrepass → ScenePass → Skybox → Transparent | 基础 PBR、Clearcoat、Sheen 与透明物体 |
| Deferred | DepthPrepass → GBuffer → DeferredLighting → Skybox → Transparent | 统一材质缓冲与全屏光照 |
| Ray Tracing | BLAS / TLAS 构建 → RayTracing → 可选历史重投影 | 每帧随机采样与时域累积 |
| Offline | BLAS / TLAS 构建 → OfflinePathTracing | 独立渐进式路径追踪与高采样参考图 |

光栅路线在需要时加入 ShadowPass。光源标记在线性场景颜色上绘制，随后一起经过 Bloom（可选）与 ToneMapping；光追关闭 HDR 时通过 Blit 输出。UI 在后处理之后绘制，拾取仍使用独立阶段。

### 光栅化

- 深度使用 D32Sfloat，主场景支持不透明、Alpha Mask、双面与排序透明绘制。
- 延迟 G-buffer 保存 Albedo / AO、Normal / Roughness、Emissive / Metallic、Sheen / Roughness，并从深度重建位置。
- Forward / Deferred 复用基础 BRDF、灯光、阴影和 IBL；Clearcoat 当前仅在 Forward 计算。
- 透明材质仍走前向混合，不等同于物理折射或透射。
- 阴影由统一 [ShadowPass](feature/shadow/ShadowPass.cpp) 处理，算法见 [阴影文档](../../docs/shadow.md)。

### 实时光追

- 基础 PBR 复用漫反射与 GGX 函数，使用 VNDF 和完整混合 PDF 生成反弹方向。
- 点光源和面光源分别加权选择；面光源采样与 BRDF 命中使用 MIS。
- 方向光通过可见性射线计算直接照明；矩形发光面以三角形参与求交。
- 递归深度可调，使用俄罗斯轮盘终止。BLAS 按几何缓存，TLAS 按提交帧更新。
- 输出 RGBA16F 颜色、原始线性深度与法线。当前主射线不做像素内 Jitter。

可选的历史重投影根据深度重建世界位置，投影到上一帧并用深度 / 法线判断历史有效性。前期计算算术平均，之后使用可调 EMA；同时记录样本数和亮度一阶 / 二阶矩。当前没有方差引导的空间滤波，不能视为完整 SVGF。

### 离线路径追踪

- 使用独立 Pass 和 Shader，不覆盖实时光追实现；活动 Frames-in-flight 为 1。
- Owen 扰乱 Sobol 序列用于像素内采样，FP32 缓冲保存颜色总和，再除以样本数。
- 场景、相机、材质、灯光或图重建变化时重新累积；曝光等纯显示参数无需清空。
- 输出接入同一套 HDR 后处理。支持基础 PBR、多次反弹与面光源 MIS，不是完整 PBRT。

当前两条光追路线尚不处理环境照明、聚光灯及完整 glTF 材质扩展，也不提供透明介质传输。

## 帧与资源

- 默认两帧在途，每个槽位有独立 FrameContext、FrameData；复用前等待对应 Fence。
- 光栅、实时与离线光追共用全局材质 SSBO、纹理数组和独立索引的采样器数组；每个在途槽位持有一套材质快照，等待 Fence 后更新。
- Graph 创建的图像跨帧共享，直接通过 `getImage(handle)` 访问；缓冲默认跨帧共享，阴影图不按 Frames-in-flight 复制。
- 同一队列上追踪资源最后访问，下一帧首次使用前插入 Barrier；不意味着前后帧可无约束并行写同一资源。
- 回读缓冲使用 PerFrame；交换链图像按获取到的 Image Index 选择。
- 历史重投影资源由 Pass 持有，颜色 / 深度 / 法线 / 亮度矩各使用双层 Ping-pong。
- Device、ShaderManager、RenderResourceManager 通过 RenderContext 聚合；共享 Layout 由 Device 在初始化时统一创建并持有，私有 Layout 由 Pass 在 setup 时直接创建并持有，不缓存 Layout。Device 保留 Pipeline 变体缓存。

## 代码导航

| 位置 | 内容 |
| --- | --- |
| [Renderer.cpp](Renderer.cpp) / [RenderPreparation.cpp](RenderPreparation.cpp) | 路线调度、帧准备、主视图与阴影剔除 |
| [RenderGraph](feature/RenderGraph.h) | 类型化登记、依赖编译、资源状态与执行 |
| [SceneDraw](feature/SceneDraw.cpp) / [SceneBindings](feature/SceneBindings.cpp) | 共享场景绘制函数与绑定 |
| [feature/](feature/) | 光栅、阴影、后处理与编辑器 Pass |
| [raytracing/](raytracing/) / [offline/](offline/) | 实时光追、历史重投影与离线路径 |
| [device/](device/) / [present/](present/) | Vulkan、帧同步、计时与呈现 |

CPU Scope、GPU Timestamp、Draw / 三角形计数用于观察各阶段成本；不把这些统计等同于跨硬件的性能基准。
