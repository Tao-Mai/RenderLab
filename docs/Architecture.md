# 项目架构

[项目首页](../README.md) · [渲染模块](Render.md)

RenderLab 将场景逻辑、资产数据和 GPU 执行分开：上层描述“渲染什么”，Renderer 准备可见数据，各 Pass 决定“如何渲染”。

## 一帧的数据流

```text
Engine：场景 / 编辑器 Tick
  → RenderSystem：按 Submesh 提交 DrawItem，提取灯光
  → GpuScene.update：解析网格，准备材质并录制相机与灯光上传
  → 获取交换链图像
  → RenderGraph.execute：执行已编译的 Pass，按需准备光追场景
  → Pass.execute：阴影数据与剔除、透明排序、绘制或计算
  → Submit / Present
```

编辑器相机与编辑器绘制输入由 Editor 独立提交。

## 职责边界

| 模块 | 主要职责 |
| --- | --- |
| Engine / Context | 生命周期与主循环，协调场景、编辑器和渲染 |
| SceneManager | 反射注册、场景读写、默认场景与场景切换 |
| RenderSystem | 读取 CPU MeshAsset，按 Submesh 提交 DrawItem，提取 Transform、材质覆盖和灯光 |
| AssetManager / AssetDataManager | CPU 资产描述、缓存与二进制读写 |
| RenderResourceManager | 通过资产 ID 获取并缓存 GPU 网格、纹理和采样器 |
| Renderer | 组装渲染路线，安排场景准备、图执行与帧提交 |
| GpuScene | 解析每个 DrawItem 对应的 GPU 网格，录制场景上传，按需提供主视图可见性与光追场景数据 |
| MaterialTable | 解析材质覆盖与 glTF、PBRT 材质，管理材质数据和指令表的上传及绑定 |
| RenderGraph / Pass | RenderGraphCompiler 解析依赖，ResourceCache 登记命名资源，RenderGraphExe 按序调用 Pass 的 execute |
| Editor | 相机导航、选择、属性编辑和编辑器绘制输入 |

光栅化与光追共用 Submesh 粒度的 DrawItem。`GpuScene` 保存场景条目和解析好的网格，各 Pass 按相同的条目序号访问网格、材质索引和可见性。

## 场景与资产

- Actor 持有 Component；SceneManager 根据注册类型创建和恢复对象。
- 构建期生成字段描述，多态数据使用“类型名 → 数据”。
- 资产使用类型化 ID 引用，网格几何与纹理像素独立持久化。
- MaterialTable 合并材质描述与 override，按材质 ID 与覆盖对象维护稳定索引，并用当前帧的 encoder 录制 GPU 数据上传。
- 场景保存编辑器相机和环境图，Renderer 配置只保存渲染行为。

详见 [场景模块](Scene.md) 与 [资产模块](Asset.md)。

### 元数据生成

在 C++ 类型声明上使用 `RL_RECORD`，在持久化字段上用 `RL_FIELD("key")` 固定 JSON 键。
`RL_REGISTER_TYPE(Base, "id")` 声明多态子类型，`RL_TYPE("id")` 可为 `std::variant` 分支指定标签。
枚举通过 `RL_ENUM` 或 `RL_FLAGS` 生成名称表，序列化键由 `RL_ENUM_VALUE` 提供。
在类型定义之后引入对应的 `RL_METADATA_HEADER`。CMake 自动发现 `src/` 和各 Pass 自己的目录中使用该指令的 `.h` 文件。
生成器按目标扫描 Clang AST，输出标准 C++ 字段描述和注册代码。依赖文件记录扫描时包含的头文件，生成结果按构建配置保存在构建目录下。

## 渲染与资源

- Renderer 直接持有 `rhi::IDevice`、Graphics Queue 和 Surface，使用 RHI 接口创建资源、获取和呈现纹理。
- Renderer 用时间线 Fence 管理帧提交，复用槽位前等待完成。
- `GpuScene` 和 `MaterialTable` 各自持有场景与材质的 RHI 缓冲。通过 encoder 录制上传，由 RHI 跟踪资源状态。
- Renderer 在获取交换链图像前调用 `GpuScene::update()`。ShadowPass 用已解析的网格绘制阴影，并为点光源逐面录制六个深度层。
- `RenderContext` 将 `GpuScene`、命令 encoder 和资源服务传给各个 Pass。
- Pass 直接使用 RHI render / compute / ray tracing pass encoder，并通过 ShaderCursor 写入原生 shader object。
- 光追 Pass 在开启 RHI Pass 前向 `GpuScene` 请求当帧的光追数据。`SceneAccelerationStructures` 根据已解析的网格和实例输入缓存并构建 BLAS/TLAS。
- Graph Buffer 和历史图像跨帧共享，拾取在提交完成后回读。图重建时等待队列，重新创建该图的资源登记表并重置时域历史。

详见 [RenderGraph](RenderGraph.md) 与 [Shader 参数与管线](RenderLayout.md)。

## 配置与编辑器

- Renderer 初始化时取得渲染配置引用，并通过原生 `RenderContext` 提供给渲染代码。
- 路线或图结构变化时重建 Graph；曝光等数值参数通过逐帧数据更新。
- IBL 决定是否加入 SkyboxPass，场景环境图变化时刷新绑定。
- UI 修改配置，正常关闭时统一持久化；场景内容只在显式保存时写入。
- EditorBackend 负责GLFW 平台接入与 slang-rhi 绘制；场景树、Inspector、RendererSettings 和导入面板各自处理业务。
- 编辑器独立处理物体拾取与 Gizmo。灯光可通过场景树选中并编辑。
- UI 在后处理之后叠加到场景输出。

新增渲染功能时，在 Pass 目录声明插件工厂和资源端口，再在对应 Python 图中连线。
