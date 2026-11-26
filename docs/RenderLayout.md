# Shader 参数与管线

[渲染模块](Render.md) · [RenderGraph](RenderGraph.md)

项目使用 slang-rhi 的 ShaderProgram、ShaderObject 与 ShaderCursor，Slang 反射决定资源绑定和常量布局。
Shader 中声明具名的 Texture、Sampler、Buffer、加速结构和 ConstantBuffer；CPU 通过同名参数写入数据。

`ShaderManager` 根据构建生成的变体清单加载 `.slang-module`，在设备的 Slang session 中组合并链接入口，创建 RHI shader program。
模块和运行库使用 slang-rhi 获取的同一版 Slang。构建同时生成反射数据，用于校验 CPU 常量和 storage image 格式。

Pass 直接持有 `rhi::IShaderObject`，或通过原生 pass encoder 的 `bindPipeline()` 取得根 shader object，再使用 `rhi::ShaderCursor` 绑定资源和常量。
公共场景和材质参数可供多个 program 共享，每个 program 读取自身反射中存在的字段。RHI 在录制时保存参数快照。
纹理数组使用参数名与元素索引，CPU/GPU 共享常量保留布局校验。

- `RenderView` 提供 `sceneUniforms`。`SceneLighting` 提供灯光缓冲、`lightUniforms` 和环境资源。阴影常量来自本次 `RenderContext`，阴影贴图与变换缓冲来自渲染图。
- `MaterialTable` 提供 materials、textures 和 samplers。
- 光追 Pass 提供 scene、objects、emissiveTriangles 或离线材质与光源缓冲。
- Pass 的 parameters / pushConstants 字段通过具名 ConstantBuffer 写入。

Pass 直接持有 RHI 管线。MinimalPathTracer 按场景光源变体创建光追管线。原生管线描述包含 ShaderProgram、顶点输入、附件格式与渲染状态。
原生 descriptor 分配、shader table、资源状态转换及后端内存分配由 slang-rhi 管理。
