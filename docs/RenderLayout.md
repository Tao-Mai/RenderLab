# Descriptor Set Layout 与 Pipeline Layout

[渲染模块](Render.md) · [项目架构](Architecture.md)

Descriptor Set Layout 定义资源类型、数量和可见阶段；Pipeline Layout 按 Set 编号组合布局，并定义 Push Constant。共享布局由 Device 在初始化时创建，私有布局由 Pass 持有。Pipeline Layout 通过 `getOrCreatePipelineLayout` 按布局内容与 Push Constant 范围缓存，由 Device 统一持有和释放。

实现：[DeviceLayouts.cpp](../src/render/device/DeviceLayouts.cpp)、[ShaderData.h](../src/render/resources/ShaderData.h)。

## 共享资源

| 布局 | Set | 内容 |
| --- | --- | --- |
| `sceneDescriptorSetLayout` | 0 | 相机矩阵和位置 |
| `lightDescriptorSetLayout` | 1 | 通用光照参数、LightData、环境图及 LUT |
| `materialDescriptorSetLayout` | 2 | glTF 材质 SSBO、全局纹理和采样器数组 |
| `shadowDescriptorSetLayout` | 3，光栅路线 | 阴影参数、矩阵、深度贴图和采样器 |

Set 编号按 Pipeline 解释：光追的私有资源占 Set 3，延迟光照的 G-buffer 占 Set 2。实时与离线光追的 Pipeline Layout 使用 Scene、Light、Material 和私有 Set；光栅光照使用 ShadowSet。

### SceneSet

Binding 0 为 `SceneUniforms` UBO，保存 viewProjection、inverseViewProjection、cameraPosition 和 view。光栅、光追与重投影共用它。

### LightSet

| Binding | 类型 | 内容 |
| --- | --- | --- |
| 0 | UBO | `LightUniforms`：光源分组范围、环境光系数、预过滤环境图最大 LOD，32 B |
| 1 | SSBO | `LightData` 数组，固定容量 `kMaxLights`（65536） |
| 2 / 3 | Image / Sampler | Irradiance 环境图 |
| 4 / 5 | Image / Sampler | 预过滤镜面环境图 |
| 6 / 7 | Image / Sampler | BRDF LUT |
| 8 / 9 | Image / Sampler | Radiance 环境图 |
| 10 / 11 | Image / Sampler | Sheen LUT |

LightData Buffer 由 Renderer 按 `kMaxLights * sizeof(LightData)` 创建，启用光源每帧统一上传，`shadowIdx` 使用相同的写入规则。描述符在图创建或重建时写入。LightData 支持 Vertex、Fragment 和 Raygen 读取，Radiance 图像与采样器对所有阶段可见，其余资源用于光栅阶段。

Shader 声明：[lighting_data.slang](../shaders/common/lighting_data.slang)、[scene_lighting.slang](../shaders/raster/scene_lighting.slang)。

### ShadowSet

| Binding | 类型 | 内容 |
| --- | --- | --- |
| 0 | UBO | `ShadowUniforms`：Cascade 数量、方向光采样半径、bias、点光和聚光采样半径、采样旋转与阴影开关，32 B |
| 1 / 2 / 3 / 4 | SSBO | 点光、聚光、矩形面光与方向光的阴影变换数据 |
| 5 / 6 / 7 / 8 | Image | 点光 Cube Array、聚光 2D Array、矩形面光 2D Array、Cascade 2D Array |
| 9 | Sampler | 深度比较采样器 |
| 10 | Sampler | 阴影线性采样器 |

阴影资源对 Vertex 和 Fragment 可见。FrameData 持有独立的 ShadowSet 与 ShadowUniforms Buffer；阴影矩阵、贴图或采样器变化时更新 ShadowSet，光栅帧准备阶段上传 ShadowUniforms。

Shader 声明：[shadow_data.slang](../shaders/raster/shadow_data.slang)。

### MaterialSet

Binding 0 为全局 `MaterialData` SSBO，Binding 1 为 128 项纹理数组，Binding 2 为 32 项独立索引的采样器数组。光栅与实时光追读取 glTF 材质数据；离线 RGB 预览读取自身的 PBRT 材质与纹理指令缓冲，共用全局图像和采样器数组。

## Pipeline 布局

| Pipeline | Set 排列 | Push Constant |
| --- | --- | --- |
| Mesh | Scene、Light、Material、Shadow | `MeshPushConstants`，80 B |
| Shadow | Scene、Light、Material、Shadow；绘制时绑定 Light 和 Shadow | `ShadowPushConstants`，80 B |
| Skybox | Scene、Light | `SkyboxPushConstants`，16 B |
| DeferredLighting | Scene、Light、G-buffer、Shadow | `DeferredLightingPushConstants`，16 B |
| Realtime RayTracing | Scene、Light、Material、光追私有资源 | `PathTracingParameters`，48 B |
| OfflinePathTracing | Scene、空 Set、Material、离线私有资源 | `OfflinePathParameters`，112 B |

光栅 Push Constant 用于 Vertex / Fragment，实时光追用于 Raygen，离线参数用于 Raygen / Compute。离线 Set 1 留空，光源由私有 Set 提供。SVGFReprojection 使用 Scene 和自己的 Set 1；SVGFFilter 使用自己的 Set 0。

实时光追的 48 B 参数包含 viewport、LightRanges、最大反弹次数、样本序号、IBL 开关及初始光源候选数量 M。M 来自 `feature.restir.candidateCount`，运行时调整直接更新 Push Constant。

光追私有 Set 3 的编号由 [RayTracingBindings.h](../src/render/raytracing/RayTracingBindings.h) 统一定义，CPU 与 shader 共用。

| 路线 | Binding 排列 |
| --- | --- |
| 实时 | 0：TLAS；1：命中数据；2：发光三角形；3：合并颜色或直接光；4：深度；5：法线；6：间接光；7：emission；8：albedo；9 / 10：当前 / 上一帧表面；11：初始 / 时域 reservoir；12：双层 reservoir 历史；13：ReSTIR 帧参数 |
| 离线 | 0：TLAS；1：命中数据；2：材质预览；3：纹理指令；4：发光网格共享参数；5–11：point / spot / distant / 三角形 / infinite / projection / goniometric；12：portal 顶点；13：累积缓冲；14：颜色输出；15：BDPT splat；16：BDPT 光源 alias table |

实时光追的 Binding 6–8 用于 SVGF，SVGF 开关重建图并选择匹配的 Shader。Binding 2 的 SSBO 保存 16 B 数量头及每个实例展开后的 80 B 三角形记录，发光系数和纹理通过材质索引共享 MaterialSet。

SVGF 的两种模式分别缓存 Pipeline、私有 Layout 与 SBT；关闭模式的 Layout 省略 Binding 6–8。图重建时重新分配当前模式的私有描述符集，并绑定新图像和缓冲。

Binding 9 / 10 分别绑定两份 64 B / 像素的表面缓冲，按历史层交换。Binding 11 使用 32 B / 像素，初始阶段写入、时域阶段更新、空间阶段只读；Binding 12 包含两层 32 B / 像素 reservoir。Binding 13 的 144 B UBO 保存上一帧矩阵、视口、深度行、图像尺寸、历史状态与复用参数。三个阶段共用 Pipeline Layout 和 SBT，阶段间显式同步存储读写。完整流程见 [ReSTIR](ReSTIR.md)。

`RayTracingScenePass` 使用独立 Compute 布局：Set 0 的 Binding 0 / 1 / 2 分别为命中记录、发光实例任务和输出三角形缓冲。8 B Push Constant 保存本次三角形数量和任务数量。Graph 同步 Compute 写入与后续光追读取。

`SVGFReprojection` 私有 Set 1 的 Binding 0–4 为当前帧 direct / indirect / depth / normal / albedo；5–10 为两路颜色、共享深度 / 法线、两路亮度矩 / 样本数的双层历史；11 / 12 为两路 RGBA32F 颜色 / 方差输出。亮度矩输出引用历史数组（RG 为一阶 / 二阶矩，B 为样本数）。颜色及亮度统计均已除以 albedo，颜色历史在本阶段只读，由第一级滤波回写。

`SVGFFilter` 注册五个 Compute Pass，每级拥有独立实例和 descriptor。私有 Set 0 的 Binding 0–3 读取两路解调后的颜色 / 方差、深度与法线。前四级的 Binding 4 / 5 写两路滤波结果，第一级额外通过 Binding 6 / 7 写颜色历史；最后一级的 Binding 4 写合并颜色，Binding 5 / 6 读取当前帧 emission / albedo，乘回 albedo 后添加 emission。Push Constants 为 24 B：viewport、步长和本帧历史层。全部仅在 SVGF 开启时加入图。

前四级在重投影输出与另一组 RGBA32F 贴图间交替读写，通过 Graph Handle 版本表达覆盖依赖，各级描述符绑定对应的固定图像并跨帧复用。

## 编辑器共享 Layout

| Layout | 描述 | 使用它的 Pass |
| --- | --- | --- |
| `editorViewDescriptorSetLayout` | Binding 0：View UBO，数量 1，阶段 Vertex、Fragment | 作为编辑器 Pipeline Layout 的 Set 0 |
| `editorPipelineLayout` | Set 0：`editorViewDescriptorSetLayout`；Push Constant 从偏移 0 开始，80 B，阶段 Vertex、Fragment | `LightMarkerPass`、`EditorPickingPass`、`EditorDepthPass` |

实现：[DeviceLayouts.cpp](../src/render/device/DeviceLayouts.cpp)。
