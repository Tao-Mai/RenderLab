# 渲染模块

[项目首页](../../README.md) · [架构](../Architecture.md) · [RenderGraph](Arch.md) · [Descriptor / Pipeline Layout](../RenderLayout.md)

渲染模块接收场景快照和编辑器输入，准备 GPU 数据，再通过 RenderGraph 调度完整的渲染 Pass。Renderer 负责流程，Pass 负责 Vulkan 绘制细节。

场景以 Submesh 粒度的 DrawItem 提交，光栅化直接绘制，光追用同一批条目关联 BLAS、生成 TLAS 实例与命中数据。Renderer 只读引用 DrawItem 列表，材质索引按条目顺序单独保存在数组中。

## 四条路线

| 路线 | 主流程 | 用途 |
| --- | --- | --- |
| Forward | DepthPrepass → ScenePass → Skybox → Transparent | 基础 PBR、Clearcoat、Sheen 与透明物体 |
| Deferred | DepthPrepass → GBuffer → DeferredLighting → Skybox → Transparent | 统一材质缓冲与全屏光照 |
| Ray Tracing | BLAS / TLAS 构建 → RayTracing → 可选 SVGFReprojection → 五级 SVGFFilter | 分路累积、方差估计与多尺度滤波；末级直接合成 |
| Offline | BLAS / TLAS 构建 → OfflinePathTracing | 独立渐进式路径追踪与高采样参考图 |

光栅路线在需要时加入 ShadowPass。光源标记在线性场景颜色上绘制，随后一起经过 Bloom（可选）与 ToneMapping；光追关闭 HDR 时通过 Blit 输出。UI 在后处理之后绘制，拾取仍使用独立阶段。

### 光栅化

- 深度使用 D32Sfloat，主场景支持不透明、Alpha Mask、双面与排序透明绘制。
- 延迟 G-buffer 保存 Albedo / AO、Normal / Roughness、Emissive / Metallic、Sheen / Roughness，并从深度重建位置。
- Forward / Deferred 复用基础 BRDF、灯光、阴影和 IBL；Clearcoat 当前仅在 Forward 计算。
- 透明材质仍走前向混合，不等同于物理折射或透射。
- 阴影由统一 [ShadowPass](../../src/render/feature/shadow/ShadowPass.cpp) 处理，算法见 [阴影文档](../Shadow.md)。

### 实时光追

Shader 入口：[raytracing/realtime.slang](../../shaders/raytracing/realtime.slang)。光追专用的随机数、光源采样和 BRDF 采样文件统一放在同一目录。

- 基础 PBR 复用漫反射与 GGX 函数，使用 VNDF 和完整混合 PDF 生成反弹方向。
- 点光源与聚光灯共用一次加权选择，合计只选一个；权重为最大 RGB 强度乘锥角衰减再除以距离平方，超出 range 或聚光锥的光源跳过，并补偿选择概率。面光源独立加权选择，采样与 BRDF 命中使用 MIS。
- 方向光通过可见性射线计算直接照明；矩形发光面以三角形参与求交。
- 实时与离线共用 [RayGen 路径循环](../../shaders/raytracing/path_tracing.slang)，ClosestHit 只返回命中与材质信息，所有续射线和阴影射线均由 RayGen 发出。Vulkan 管线递归深度固定为 1。
- `maxBounces` 控制最多 1–64 次表面散射，结束后仍计算终点发光；1 对应直接照明与终点发光。使用俄罗斯轮盘终止。修改反弹次数不重建渲染图或 Pipeline，但会清空对应的时域历史 / 离线累积。BLAS 按几何缓存，TLAS 按提交帧更新。
- 关闭 SVGF 时输出 RGBA16F 合并颜色；开启时分别输出直接光、间接光、`emission` 与主命中材质的线性 albedo。两种模式均输出原始线性深度与法线。当前主射线不做像素内 Jitter。
- `emission` 保存相机直接可见的发光表面、面光源与环境。直接光包含主表面的光源采样、一次 BRDF 采样命中发光面 / 环境的贡献；更深层路径归入间接光。直接光保留面光源 MIS 的两部分，合并颜色为三路之和。

仅在开启 SVGF 时执行 [SVGFReprojection](../../src/render/raytracing/SVGFReprojection.h) 和 [SVGFFilter](../../src/render/raytracing/SVGFFilter.h)。直接光与间接光先除以各自像素的 albedo，在解调后的空间中累积颜色、亮度矩并估计方差。有效样本数不超过 `historyLength` 时使用算术平均，超过后使用 `emaAlpha`；首个样本或历史失效时权重为 1。历史不足 4 帧时使用 7×7 空间方差，五级 à-trous 按深度、法线、亮度与方差滤波。第一级回写解调后的颜色历史，最后一级乘回当前像素的 albedo 并添加当前帧 `emission`。除法和乘回统一使用 `max(albedo, 0.01)`，避免黑色通道除零。算法与限制见 [SVGF 文档](../SVGF.md)。

### 离线路径追踪

Shader 入口：[raytracing/offline.slang](../../shaders/raytracing/offline.slang)，复用同目录下的光追采样代码。

- 使用独立 Pass 和 Shader，不覆盖实时光追实现；活动 Frames-in-flight 为 1。
- Owen 扰乱 Sobol 序列用于像素内采样，FP32 缓冲保存颜色总和，再除以样本数。
- 场景、相机、材质、灯光或图重建变化时重新累积；曝光等纯显示参数无需清空。
- 输出接入同一套 HDR 后处理。支持基础 PBR、多次反弹与面光源 MIS，不是完整 PBRT。

当前两条光追路线尚不处理环境照明及完整 glTF 材质扩展，也不提供透明介质传输。

## 帧与资源

- 默认两帧在途，每个槽位有独立 FrameContext、FrameData；复用前等待对应 Fence。
- FrameData 仅持有逐帧 UBO、公共 Set 和绑定版本；共享光源 Buffer handle 归 Renderer，阴影 CPU 数据与 Buffer handle 归 ShadowPass。
- 光栅、实时与离线光追共用全局材质 SSBO、纹理数组和独立索引的采样器数组；SSBO 由 Graph 持有，所有在途帧共用一份，上传与读取通过 Barrier 排序。
- 每个在途槽位有一个持久映射的 staging pool；Fence 完成后重置分配游标，prepare 取得 offset 后写入数据，Graph 执行时复制到 SSBO。
- Graph 创建的图像跨帧共享，直接通过 `getImage(handle)` 访问；缓冲默认跨帧共享，阴影图不按 Frames-in-flight 复制。
- 同一队列上追踪资源最后访问，下一帧首次使用前插入 Barrier；不意味着前后帧可无约束并行写同一资源。
- Graph Buffer 统一跨帧共享，通过 `getBuffer(handle)` 访问；拾取等待提交完成后立即回读。Renderer 根据 acquire 结果选择交换链 Image 并绑定外部目标，Graph 不感知 imageIndex；ImageView 缓存由 Swapchain 中的 Image 持有。
- SVGF 历史资源由 Graph 创建并持有，Pass 保存 handle；两路颜色与亮度矩各自使用双层 Ping-pong，深度 / 法线历史共享，图重建时重置历史。
- Device、ShaderManager、RenderResourceManager、MaterialTable 通过 RenderContext 聚合；共享 Layout 在初始化时预创建。Device 缓存并持有全部 Pipeline Layout 和光栅 Pipeline，Pass 只保存 Pipeline Layout 句柄；私有 Descriptor Set Layout 仍由各 Pass 持有。布局匹配交给 Vulkan 验证层检查。

## 代码导航

| 位置 | 内容 |
| --- | --- |
| [Renderer.cpp](../../src/render/Renderer.cpp) | 路线调度、全局帧准备、光栅化主视图剔除 |
| [ShadowPass](../../src/render/feature/shadow/ShadowPass.cpp) / [TransparentPass](../../src/render/feature/TransparentPass.cpp) | 每帧 prepare 中准备阴影数据与阴影剔除、排序透明物体 |
| [RenderGraph](../../src/render/feature/RenderGraph.h) | 类型化登记、依赖编译、资源状态与执行 |
| [SceneDraw](../../src/render/feature/SceneDraw.cpp) / [FrameData](../../src/render/FrameData.cpp) | 共享绘制与资源依赖函数、逐帧数据和公共 Set 写入 |
| [feature/](../../src/render/feature/) | 光栅、阴影、后处理与编辑器 Pass |
| [raytracing/](../../src/render/raytracing/) / [offline/](../../src/render/offline/) | 实时光追、历史重投影与离线路径 |
| [device/](../../src/render/device/) / [present/](../../src/render/present/) | Vulkan、帧同步、计时与呈现 |

CPU Scope、GPU Timestamp、Draw / 三角形计数用于观察各阶段成本；不把这些统计等同于跨硬件的性能基准。
