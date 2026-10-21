# 项目架构

[项目首页](../README.md) · [渲染模块](../src/render/README.md)

RenderLab 将场景逻辑、资产数据和 GPU 执行分开：上层描述“渲染什么”，Renderer 准备可见数据，各 Pass 决定“如何渲染”。

## 一帧的数据流

```text
Engine：场景 / 编辑器 Tick
  → RenderSystem：提取 SceneRenderData
  → Renderer：阴影准备、可见性剔除
  → RenderResourceManager：解析资产，生成 RenderData
  → FrameData：上传相机、材质、灯光与阴影数据
  → RenderGraph：执行已编译的 Pass
  → Submit / Present
```

编辑器相机与编辑器绘制输入独立提交，不作为场景中的 Actor。

## 职责边界

| 模块 | 主要职责 |
| --- | --- |
| Engine / Context | 生命周期与主循环，协调场景、编辑器和渲染 |
| SceneManager | 反射注册、场景读写、默认场景与场景切换 |
| RenderSystem | 提取资产 ID、Transform、材质覆盖和灯光，不剔除、不创建 GPU 资源 |
| AssetManager / AssetDataManager | CPU 资产描述、缓存与二进制读写，不反向依赖场景和 Renderer |
| RenderResourceManager | 递归获取资产并缓存 GPU 表示，准备 DrawItem 与透明排序 |
| Renderer / RenderPreparation | 渲染路线组装、阴影与可见性准备、帧提交 |
| RenderGraph / Pass | Graph 处理依赖、资源和同步；Pass 负责布局、绑定与具体绘制 |
| Editor | 相机导航、选择、属性编辑和编辑器绘制输入 |

渲染层不访问 Component。传入 Renderer 的数据是独立快照，GPU DrawItem 不保留 Actor / Component 指针。

## 场景与资产

- Actor 持有 Component；SceneManager 根据注册类型创建和恢复对象。
- C++26 静态反射配合 `ReflectField` 选择持久化字段，多态数据使用“类型名 → 数据”。
- 资产使用类型化 ID 引用，网格几何与纹理像素独立持久化。
- 材质描述与 GPU 材质分离；共享材质按 ID 缓存，覆盖实例按帧安全更新。
- 场景保存编辑器相机和环境图，Renderer 配置只保存渲染行为。

详见 [场景模块](../src/scene/README.md) 与 [资产模块](../src/asset/README.md)。

## 渲染与资源

- `VulkanContext` 管理设备与队列，`Swapchain` 管理呈现资源。
- `FrameContext` 管理命令与同步；`FrameData` 管理逐帧上传数据。
- `RenderContext` 聚合 Device、Shader 和 GPU 资产管理器；Device 负责对象创建、Descriptor 分配和 Pipeline 缓存，并在初始化时统一创建和持有场景、材质与编辑器的共享 Layout。
- Pass 直接使用 Vulkan API；共享场景绘制函数位于 `SceneDraw`，它不是额外的资源管理层。
- Graph 默认共享阴影图、深度和颜色中间结果；跨帧访问由同一队列上的 Barrier 同步。
- CPU 回读缓冲显式按帧分配；时域历史使用独立的双层 Ping-pong 资源。

详见 [RenderGraph](../src/render/ARCH.MD) 与 [Descriptor / Pipeline Layout](render_layout.md)。

## 配置与编辑器

- 路线或图结构变化时重建 Graph；曝光等数值参数通过逐帧数据更新。
- IBL 决定是否加入 SkyboxPass；场景环境图变化刷新绑定，不作为 Graph 的业务状态。
- UI 修改配置，正常关闭时统一持久化；场景内容只在显式保存时写入。
- EditorBackend 负责平台 / Vulkan backend；场景树、Inspector、RendererSettings 和导入面板各自处理业务。
- 光源标记、拾取与 Gizmo 使用编辑器独立逻辑，不混入场景材质 Shader。
- 光源标记使用各类型光源的线性强度，在 Bloom / Tone Mapping 之前绘制；UI 不参与后处理。

新增渲染功能通常只需实现对应 Pass、声明资源访问，并在 Renderer 的路线中串接；无需给底层管理器加入特定场景或材质的分支。
