# Descriptor Set Layout 与 Pipeline Layout

[渲染模块](../src/render/README.md) · [项目架构](architecture.md)

本文记录共享 Layout 的描述、初始化预创建时机和使用它们的 Pass。

## 两种 Layout 的职责

- Descriptor Set Layout：声明各 Binding 的资源类型、数量和可访问的 Shader 阶段。
- Pipeline Layout：按 Set 编号组合 Descriptor Set Layout，并声明 Push Constant 的范围和 Shader 阶段。

## 初始化时预创建

`Renderer::init()` 在 ShaderManager、Device 初始化后调用 `SceneInterface::init()` 和 `EditorLayouts::init()`：

- `SceneInterface` 预创建两份 Descriptor Set Layout 和三份 Pipeline Layout。
- `EditorLayouts` 预创建一份 Descriptor Set Layout 和一份 Pipeline Layout。
- Device 直接创建 Layout，不做 Layout 缓存；共享 Layout 由上述对象以 RAII 持有，私有 Layout 由 Pass 在 `setup()` 中创建并持有。

这些 Layout 在渲染器生命周期内固定，运行时通过 RenderContext 直接读取：

```cpp
const auto& layout = renderContext().sceneInterface->meshLayout;
```

实现：[Renderer.cpp](../src/render/Renderer.cpp)、[SceneInterface.h](../src/render/feature/SceneInterface.h)、[SceneInterface.cpp](../src/render/feature/SceneInterface.cpp)。

## Descriptor Set Layout

以下两份布局的每个 Binding 均为单个 Descriptor，阶段均为 Vertex | Fragment。表中 UBO 对应 Uniform Buffer，SSBO 对应 Storage Buffer；Image 与 Sampler 分开绑定。

Binding 编号定义于 [ShaderData.h](../src/render/resource/ShaderData.h)。

### sceneLayout：场景资源，Set 0

| Binding | Descriptor 类型 | 内容 |
| --- | --- | --- |
| 0 | UBO | `ViewUniforms`：相机矩阵、相机位置 |
| 1 | UBO | `LightUniforms`：光源分组范围、环境光及阴影参数 |
| 2 | SSBO | `LightData` 光源数组 |
| 3 | SSBO | 点光源阴影数据 |
| 4 | SSBO | 聚光灯阴影数据 |
| 5 | SSBO | 矩形面光源阴影数据 |
| 6 | SSBO | 方向光 Cascade 阴影数据 |
| 7 | Sampled Image | 点光源阴影 Cube Array |
| 8 | Sampled Image | 聚光灯阴影 2D Array |
| 9 | Sampled Image | 矩形面光源阴影 2D Array |
| 10 | Sampled Image | 方向光 Cascade 阴影 2D Array |
| 11 | Sampler | 阴影深度比较采样器 |
| 12 | Sampler | 普通线性采样器 |
| 13 / 14 | Sampled Image / Sampler | Irradiance 环境图 |
| 15 / 16 | Sampled Image / Sampler | 预过滤镜面反射环境图 |
| 17 / 18 | Sampled Image / Sampler | BRDF LUT |
| 19 / 20 | Sampled Image / Sampler | 原始 Radiance 环境图，供天空盒及 Sheen IBL 使用 |
| 21 / 22 | Sampled Image / Sampler | Sheen 方向反照率 LUT |

由 `meshLayout`、`shadowLayout`、`skyboxLayout` 引用；`DeferredLightingPass` 也将它用作自身 Pipeline Layout 的 Set 0。

Shader 声明：[scene_data.slang](../shaders/scene_data.slang)、[scene_lighting.slang](../shaders/scene_lighting.slang)、[skybox.slang](../shaders/skybox.slang)。

### materialLayout：材质资源，Set 1

| Binding | Descriptor 类型 | 内容 |
| --- | --- | --- |
| 0 / 1 | Sampled Image / Sampler | Base Color |
| 2 | UBO | 材质参数，对应 C++ `MaterialUniforms` / Shader `MaterialData` |
| 3 / 4 | Sampled Image / Sampler | Normal |
| 5 / 6 | Sampled Image / Sampler | Metallic |
| 7 / 8 | Sampled Image / Sampler | Roughness |
| 9 / 10 | Sampled Image / Sampler | Emissive |
| 11 / 12 | Sampled Image / Sampler | Occlusion |
| 13 / 14 | Sampled Image / Sampler | Clearcoat |
| 15 / 16 | Sampled Image / Sampler | Clearcoat Roughness |
| 17 / 18 | Sampled Image / Sampler | Clearcoat Normal |
| 19 / 20 | Sampled Image / Sampler | Sheen Color |
| 21 / 22 | Sampled Image / Sampler | Sheen Roughness |

由 `meshLayout` 引用，作为 Set 1。Clearcoat、Sheen 等 Shader 分支共用这份固定布局。

Shader 声明：[material_data.slang](../shaders/material_data.slang)。

## Pipeline Layout 与 Pass 对应关系

这三份布局均在 `SceneInterface::init()` 中预创建。Push Constant 都从偏移 0 开始，阶段均为 Vertex | Fragment。

| Layout | Set 排列 | Push Constants | 使用它的 Pass |
| --- | --- | --- | --- |
| `meshLayout` | Set 0：`sceneLayout`；Set 1：`materialLayout` | `MeshPushConstants`，64 B：Model 矩阵 | `DepthPrepass`；`ScenePass`、`GBufferPass`、`TransparentPass` 通过 `SceneDraw::recordMeshes()` 使用 |
| `shadowLayout` | Set 0：`sceneLayout` | `ShadowPushConstants`，80 B：Model 矩阵、光源索引、Cascade 索引及对齐填充 | `ShadowPass`，点光源、聚光灯、矩形面光源与方向光共用 |
| `skyboxLayout` | Set 0：`sceneLayout` | `SkyboxPushConstants`，16 B：Viewport | `SkyboxPass` |

实现：[DepthPrepass.cpp](../src/render/feature/DepthPrepass.cpp)、[SceneDraw.cpp](../src/render/feature/SceneDraw.cpp)、[ShadowPass.cpp](../src/render/feature/shadow/ShadowPass.cpp)、[SkyboxPass.cpp](../src/render/feature/SkyboxPass.cpp)。

## 编辑器共享 Layout

| Layout | 描述 | 使用它的 Pass |
| --- | --- | --- |
| `EditorLayouts::viewLayout` | Binding 0：View UBO，数量 1，阶段 Vertex、Fragment | 作为编辑器 Pipeline Layout 的 Set 0 |
| `EditorLayouts::pipelineLayout` | Set 0：`viewLayout`；Push Constant 从偏移 0 开始，80 B，阶段 Vertex、Fragment | `LightMarkerPass`、`EditorPickingPass`、`EditorDepthPass` |

实现：[EditorLayouts.cpp](../src/render/feature/editor/EditorLayouts.cpp)。
