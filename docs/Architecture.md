# 项目架构

[项目首页](../README.md) · [渲染模块](../src/render/README.md)

RenderLab 将场景逻辑、资产数据和 GPU 执行分开：上层描述“渲染什么”，Renderer 准备可见数据，各 Pass 决定“如何渲染”。

## 一帧的数据流

```text
Engine：场景 / 编辑器 Tick
  → RenderSystem：按 Submesh 提交 DrawItem，提取灯光
  → Renderer 全局 prepare：光源分组、光栅化主视图剔除
  → RenderResourceManager：缓存、更新 GPU 资源
  → 更新相机 UBO，材质与灯光数据写入本帧 staging pool
  → RenderGraph.prepare：ShadowPass 准备阴影数据与剔除，TransparentPass 排序
  → RenderGraph.execute：同步并复制 SSBO，执行已编译的 Pass
  → Submit / Present
```

编辑器相机与编辑器绘制输入独立提交，不作为场景中的 Actor。

## 职责边界

| 模块 | 主要职责 |
| --- | --- |
| Engine / Context | 生命周期与主循环，协调场景、编辑器和渲染 |
| SceneManager | 反射注册、场景读写、默认场景与场景切换 |
| RenderSystem | 读取 CPU MeshAsset，按 Submesh 提交 DrawItem，提取 Transform、材质覆盖和灯光 |
| AssetManager / AssetDataManager | CPU 资产描述、缓存与二进制读写，不反向依赖场景和 Renderer |
| RenderResourceManager | 通过资产 ID 获取并缓存 GPU 网格、纹理和采样器 |
| Renderer | 渲染路线组装、材质覆盖解析与材质表更新、光栅化主视图剔除与帧提交 |
| MaterialTable | 将材质描述转换为全局 MaterialData，按同一索引提供 CPU 绘制信息，维护纹理索引与材质 Set |
| RenderGraph / Pass | Graph 处理依赖、资源和同步，先统一调用可选的 prepare，再执行；Pass 负责自身准备、绑定与绘制 |
| Editor | 相机导航、选择、属性编辑和编辑器绘制输入 |

渲染层不访问 Component。光栅化与光追共用 Submesh 粒度的 DrawItem；Renderer 通过只读 span 引用提交列表，在独立数组中记录对应的材质索引和可见性，各 Pass 按相同的条目序号访问。提交列表在准备和命令录制期间保持有效。

## 场景与资产

- Actor 持有 Component；SceneManager 根据注册类型创建和恢复对象。
- C++26 静态反射配合 `ReflectField` 选择持久化字段，多态数据使用“类型名 → 数据”。
- 资产使用类型化 ID 引用，网格几何与纹理像素独立持久化。
- Renderer 合并材质描述与 override；MaterialTable 统一生成 GPU 数据，按材质 ID 与覆盖对象维护稳定索引，各 FIF 安全更新。
- 场景保存编辑器相机和环境图，Renderer 配置只保存渲染行为。

详见 [场景模块](../src/scene/README.md) 与 [资产模块](../src/asset/README.md)。

## 渲染与资源

- `VulkanContext` 管理设备与队列，`Swapchain` 管理呈现资源。
- Renderer 声明外部呈现目标的 ImageHandle，acquire 后只绑定本帧 Image；图像选择和呈现由 Renderer / Swapchain 管理。Graph 执行时初始化外部目标的同步状态，不接收 imageIndex。PassSetup 提供独立的 renderExtent，Pass 从图像描述获取目标尺寸和格式。
- `FrameContext` 管理命令与同步；`FrameData` 仅持有逐帧 UBO、公共 Descriptor Set 和绑定版本。
- Renderer 管理共享光源 Buffer handle；ShadowPass 管理阴影 CPU 数据与阴影 Buffer handle，底层 SSBO 由 Graph 分配和同步。
- Renderer 持有环境纹理、LUT 引用和阴影 sampler；图编译后将阴影视图交给 FrameData 写入，场景环境图改变时更新环境绑定。
- `RenderContext` 聚合 Device、Shader、GPU 资产管理器和 MaterialTable；Device 负责对象创建、Descriptor 分配和 Pipeline 缓存，并在初始化时统一创建和持有场景、材质与编辑器的共享 Layout。
- Pass 直接使用 Vulkan API；共享场景绘制函数位于 `SceneDraw`，它不是额外的资源管理层。
- Graph 默认共享阴影图、深度和颜色中间结果；跨帧访问由同一队列上的 Barrier 同步。
- 材质、灯光、阴影与光追命中 SSBO 在 setup 申请，跨帧共用；prepare 从各 FIF 的 staging pool 线性分配并写入，execute 录制复制与前后 Barrier。容量增长时等待 GPU 完成后重新分配，并刷新受影响的 Buffer 绑定。
- Graph Buffer 统一跨帧共享；拾取在等待本次提交完成后立即回读。时域历史由 Graph 创建并持有，使用双层 Ping-pong 资源，图重建时重置历史。

详见 [RenderGraph](../src/render/Arch.md) 与 [Descriptor / Pipeline Layout](RenderLayout.md)。

## 配置与编辑器

- Renderer 初始化时取得渲染配置引用；Pass 通过 `PassSetup.config` 共享同一份配置，编辑器修改后直接读取最新值。
- 路线或图结构变化时重建 Graph；曝光等数值参数通过逐帧数据更新。
- IBL 决定是否加入 SkyboxPass；场景环境图变化刷新绑定，不作为 Graph 的业务状态。
- UI 修改配置，正常关闭时统一持久化；场景内容只在显式保存时写入。
- EditorBackend 负责平台 / Vulkan backend；场景树、Inspector、RendererSettings 和导入面板各自处理业务。
- 光源标记、拾取与 Gizmo 使用编辑器独立逻辑，不混入场景材质 Shader。
- 光源标记使用各类型光源的线性强度，在 Bloom / Tone Mapping 之前绘制；UI 不参与后处理。

新增渲染功能通常只需实现对应 Pass、声明资源访问，并在 Renderer 的路线中串接；无需给底层管理器加入特定场景或材质的分支。
