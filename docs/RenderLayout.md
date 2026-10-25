# Descriptor Set Layout 与 Pipeline Layout

[渲染模块](../src/render/README.md) · [项目架构](Architecture.md)

本文记录共享 Layout 的描述、初始化预创建时机和使用它们的 Pass。

## 两种 Layout 的职责

- Descriptor Set Layout：声明各 Binding 的资源类型、数量和可访问的 Shader 阶段。
- Pipeline Layout：按 Set 编号组合 Descriptor Set Layout，并声明 Push Constant 的范围和 Shader 阶段。

## 初始化时预创建

`Renderer::init()` 初始化 Device 时，由 `Device::createSharedLayouts()` 统一预创建共享 Layout：

- 场景、光照、材质与编辑器共用四份 Descriptor Set Layout 和两份 Pipeline Layout。
- Descriptor Set Layout 直接创建：共享布局由 Device 持有，私有布局由 Pass 持有。
- Pipeline Layout 统一通过 `Device::getOrCreatePipelineLayout()` 获取，按 Set 布局内容和 Push Constant 范围缓存。Device 以 RAII 持有，Pass 只保存 `vk::PipelineLayout` 句柄。
- Device shutdown 时先清理 Pipeline，再清理 Pipeline Layout；布局匹配检查交给 Vulkan 验证层。

这些 Layout 在渲染器生命周期内固定，运行时通过 RenderContext 直接读取：

```cpp
const auto& layout = renderContext().device->meshPipelineLayout;
```

实现：[Device.h](../src/render/device/Device.h)、[DeviceLayouts.cpp](../src/render/device/DeviceLayouts.cpp)。

## Descriptor Set Layout

表中 UBO 对应 Uniform Buffer，SSBO 对应 Storage Buffer；Image 与 Sampler 分开绑定。场景与光照布局各 Binding 均为单个 Descriptor；Scene 与 Material 的阶段为 All，Lighting 默认为 Vertex | Fragment，其中 Light Data SSBO 还支持 Raygen，Radiance 图像与 sampler 为 All，供光追 Raygen 阶段复用。

Binding 编号定义于 [ShaderData.h](../src/render/resource/ShaderData.h)。

### sceneDescriptorSetLayout：场景资源，Set 0

| Binding | Descriptor 类型 | 内容 |
| --- | --- | --- |
| 0 | UBO | `SceneUniforms`：相机矩阵、相机位置 |

仅包含这一个 Binding，光栅、实时光追、离线光追与重投影统一使用。Shader 声明：[scene_data.slang](../shaders/scene_data.slang)、[scene_uniforms.slang](../shaders/scene_uniforms.slang)。

### lightingDescriptorSetLayout：光照资源，Set 1

| Binding | Descriptor 类型 | 内容 |
| --- | --- | --- |
| 0 | UBO | `LightUniforms`：光源分组范围、环境光及阴影参数 |
| 1 | SSBO | `LightData` 光源数组，固定容量 `kMaxLights`（65536） |
| 2 | SSBO | 点光源阴影数据 |
| 3 | SSBO | 聚光灯阴影数据 |
| 4 | SSBO | 矩形面光源阴影数据 |
| 5 | SSBO | 方向光 Cascade 阴影数据 |
| 6 | Sampled Image | 点光源阴影 Cube Array |
| 7 | Sampled Image | 聚光灯阴影 2D Array |
| 8 | Sampled Image | 矩形面光源阴影 2D Array |
| 9 | Sampled Image | 方向光 Cascade 阴影 2D Array |
| 10 | Sampler | 阴影深度比较采样器 |
| 11 | Sampler | 普通线性采样器 |
| 12 / 13 | Sampled Image / Sampler | Irradiance 环境图 |
| 14 / 15 | Sampled Image / Sampler | 预过滤镜面反射环境图 |
| 16 / 17 | Sampled Image / Sampler | BRDF LUT |
| 18 / 19 | Sampled Image / Sampler | 原始 Radiance 环境图，供天空盒、Sheen IBL 及实时 / 离线光追 Miss 使用 |
| 20 / 21 | Sampled Image / Sampler | Sheen 方向反照率 LUT |

Light Data SSBO 由 `Renderer` 按 `kMaxLights * sizeof(LightData)` 一次分配（6 MiB），光栅化、实时光追与离线路径追踪共用 Set 1 / Binding 1。每帧统一写入 `shadowIdx` 并上传启用的光源；阴影 Pass 直接读取这些光源及其索引，仅生成和上传各类阴影数据，不复制光源或重新编号。启用光源总数超过上限时明确报错；光源数量或内容变化不再触发 descriptor 更新，绑定仅在渲染图创建或重建时写入。

由 `meshPipelineLayout`，以及 `ShadowPass`、`SkyboxPass`、`DeferredLightingPass`、`RayTracingPass`、`OfflinePathTracingPass` 各自的 Pipeline Layout 引用，作为 Set 1。

Shader 声明：[lighting_data.slang](../shaders/lighting_data.slang)、[scene_lighting.slang](../shaders/scene_lighting.slang)、[skybox.slang](../shaders/skybox.slang)。

### materialDescriptorSetLayout：材质资源，Set 2

| Binding | Descriptor 类型 | 内容 |
| --- | --- | --- |
| 0 | SSBO | 全局 `MaterialData` 数组，包含 PBR、Clearcoat、Sheen 参数及贴图索引 |
| 1 | Sampled Image [128] | 全局材质纹理数组 |
| 2 | Sampler [32] | 全局材质采样器数组，与纹理独立索引 |

由 `meshPipelineLayout`、`RayTracingPass` 和 `OfflinePathTracingPass` 的 Pipeline Layout 引用，统一作为 Set 2。所有材质分支使用固定的 192 B SSBO 元素布局；光栅通过 Push Constant 传入材质索引，光追从命中数据取得材质索引。

Shader 声明：[material_data.slang](../shaders/material_data.slang)。

## Pipeline Layout 与 Pass 对应关系

`meshPipelineLayout` 在 `Device::createSharedLayouts()` 中预创建；Shadow、Skybox 等 Pass 在 `setup()` 中获取所需的缓存句柄，不负责销毁 Pipeline Layout。表中 Push Constant 都从偏移 0 开始，阶段均为 Vertex | Fragment。

| Layout | Set 排列 | Push Constants | 使用它的 Pass |
| --- | --- | --- | --- |
| `meshPipelineLayout` | Set 0：Scene；Set 1：Lighting；Set 2：Material | `MeshPushConstants`，80 B：Model 矩阵、材质索引及对齐填充 | `DepthPrepass`；`ScenePass`、`GBufferPass`、`TransparentPass` 通过 `SceneDraw::recordMeshes()` 使用 |
| `ShadowPass::pipelineLayout` | Set 0：Scene；Set 1：Lighting，仅绑定 Lighting | `ShadowPushConstants`，80 B：Model 矩阵、光源索引、Cascade 索引及对齐填充 | `ShadowPass`，点光源、聚光灯、矩形面光源与方向光共用 |
| `SkyboxPass::pipelineLayout` | Set 0：Scene；Set 1：Lighting | `SkyboxPushConstants`，16 B：Viewport | `SkyboxPass` |
| `DeferredLightingPass::pipelineLayout` | Set 0：Scene；Set 1：Lighting；Set 2：空；Set 3：GBuffer | `DeferredLightingPushConstants`，16 B：Viewport | `DeferredLightingPass` |

实现：[DepthPrepass.cpp](../src/render/feature/DepthPrepass.cpp)、[SceneDraw.cpp](../src/render/feature/SceneDraw.cpp)、[ShadowPass.cpp](../src/render/feature/shadow/ShadowPass.cpp)、[SkyboxPass.cpp](../src/render/feature/SkyboxPass.cpp)。

实时与离线光追的 Pipeline Layout 依次为 Scene、Lighting、Material、私有 Set；共享 `PathTracingParameters` Push Constants 为 48 B，包含 Viewport、光源分组、`maxBounces`、采样参数和 IBL 开关，阶段为 Raygen。两条路线共用 RayGen 路径循环，ClosestHit 只返回命中信息。`SVGFReprojection` 布局为 Scene、私有 Set，Push Constants 为 128 B，包含 `historyLength` 与 `emaAlpha`，阶段为 Compute。

实时光追私有 Set 3 的 Binding 1 在关闭 SVGF 时写合并颜色，开启时写直接光；Binding 9 / 10 / 11 仅由 SVGF Shader 使用，分别写间接光 / 相机直接可见的 `emission` / 主命中材质的线性 albedo（RGBA16F）。Binding 7 / 8 始终写线性深度 / 世界法线。SVGF 开关重建图并选择匹配的 Shader。

`SVGFReprojection` 私有 Set 1 的 Binding 0–4 为当前帧 direct / indirect / depth / normal / albedo；5–10 为两路颜色、共享深度 / 法线、两路亮度矩 / 样本数的双层历史；11 / 12 为两路 RGBA32F 颜色 / 方差输出。亮度矩输出直接引用历史数组（RG 为一阶 / 二阶矩，B 为样本数），没有额外的二维副本。颜色及亮度统计均已除以 albedo，颜色历史在本阶段只读，由第一级滤波回写。

`SVGFFilter` 注册五个 Compute Pass，每级拥有独立实例和 descriptor。私有 Set 0 的 Binding 0–3 读取两路解调后的颜色 / 方差、深度与法线。前四级的 Binding 4 / 5 写两路滤波结果，第一级额外通过 Binding 6 / 7 写颜色历史；最后一级的 Binding 4 写合并颜色，Binding 5 / 6 读取当前帧 emission / albedo，乘回 albedo 后添加 emission。Push Constants 为 24 B：viewport、步长和本帧历史层。全部仅在 SVGF 开启时加入图。

前四级在重投影输出与另一组 RGBA32F 贴图间交替读写，覆盖时创建新的 Graph handle 版本；各级 descriptor 仍指向对应的固定图像，不需要逐帧重写 descriptor。

## 编辑器共享 Layout

| Layout | 描述 | 使用它的 Pass |
| --- | --- | --- |
| `editorViewDescriptorSetLayout` | Binding 0：View UBO，数量 1，阶段 Vertex、Fragment | 作为编辑器 Pipeline Layout 的 Set 0 |
| `editorPipelineLayout` | Set 0：`editorViewDescriptorSetLayout`；Push Constant 从偏移 0 开始，80 B，阶段 Vertex、Fragment | `LightMarkerPass`、`EditorPickingPass`、`EditorDepthPass` |

实现：[DeviceLayouts.cpp](../src/render/device/DeviceLayouts.cpp)。
