# 架构与扩展入口

## 分层

- **Engine**：初始化、主循环、场景和编辑器 Tick、调用 RenderSystem，再将快照交给 Renderer。
- **SceneManager**：Actor/Component 反射注册、场景读写、默认场景和场景切换；不持有 GPU 资源和编辑器相机实例。
- **RenderSystem**：只读取场景，生成 SceneRenderData（资产 ID、变换、材质覆盖、灯光）；不加载 GPU 资源。
- **AssetManager / AssetDataManager**：底层 CPU 资产工具，初始化显式传入资产根目录。各资产类型独立头文件；不反向依赖 ConfigManager、SceneManager 或 Renderer。
- **RenderResourceManager**：递归解析资产、缓存 GPU 对象，将 CPU 快照转换为 RenderData，并排序透明物体。
- **Renderer**：设备生命周期、渲染路线组装、配置变化处理、帧提交；Pass 负责实际绘制。
- **RenderGraph**：资源版本、依赖编译、分配、状态转换和执行；保留类型化 setup，不增加字符串节点注册系统。
- **Editor**：相机导航、选择、场景操作和编辑器绘制输入；不决定 Renderer 是否重建图。

## 一帧的数据流

Engine Tick → RenderSystem::prepare → SceneRenderData → Renderer 等待当前帧 Fence → RenderResourceManager::prepare → RenderData → SceneBindings / FrameData 上传 → Graph 执行 → Submit / Present。

- FrameContext 只保存命令与同步对象；Renderer 分别持有每帧 FrameContext 和 FrameData。
- 阴影索引、VP、级联划分在 feature/shadow/ShadowData 中计算，FrameData 只管理上传数据和绑定。
- EditorCamera 由 Editor 持有，配置仍位于 SceneAsset::editorCamera。切换场景时重置导航状态。
- 编辑器灯光标记和拾取维持独立 Shader 与绑定，不复用场景材质逻辑。

## 渲染路线与配置

Renderer 分成光栅、光追、后处理、编辑器四段组图。光栅内部选择 Forward 或 Deferred。

- 路线、IBL、HDR、阴影开关、阴影分辨率、级联数量、环境图有无改变：重建 Graph。
- 环境图绑定、BRDF LUT、阴影 sampler 过滤改变：等待 GPU 空闲并刷新绑定。
- 曝光、环境系数、阴影 bias/半径等：逐帧上传，无需重建。
- UI 只修改配置。ConfigManager 正常关闭时统一持久化；场景仍只在显式保存时写入。

## Shader 与管线

- cmake/Shaders.cmake 声明每个 Shader 程序的入口和宏组合，生成 SPIR-V、reflection JSON 和 variants.json。
- ShaderManager 通过资产 ID 和 ShaderDefines 查清单，加载模块；不通过全局变体枚举或文件后缀分支识别业务。
- DescriptorManager 只负责 Layout 缓存、Pool 和 Set 分配。
- PipelineManager 只负责 Layout / Pipeline 缓存及反射校验。
- SceneInterface 定义场景共享布局，EditorInterface 定义编辑器布局；专用后处理、延迟光照布局在各自 Pass 内声明。
- SceneDraw 显式接收 SceneShaderOptions，不读取全局 Renderer 配置。

添加功能时，在对应 Pass 声明资源、状态、布局和 Shader 选项，再在 Renderer 的相应路线中串接；无需给底层 Manager 增加业务枚举。

## 材质资源生命周期

- 共享材质按资产 ID 缓存，通过 AssetManager 的运行期 revision 感知保存后的更新；不使用序列化 JSON 作为键。
- 材质覆盖按在途帧及对象/组件/子网格定位。参数变化更新当前帧 UBO，纹理或布局变化重建当前实例。
- 只在对应帧 Fence 完成后更新；DrawItem 持有共享材质引用，旧材质不会在其他在途帧使用期间销毁。
- 每次准备帧移除该帧不再使用的覆盖实例，滑块历史值不会累计为永久缓存。
- Mesh / Texture / Sampler 仍按 ID 缓存；本轮没有增加完整的资产热重载系统。

## 编辑器拆分

- EditorBackend：ImGui Context、平台/Vulkan backend、字体和提交。
- EditorUI：Dock、视口、Gizmo、各面板协调。
- SceneHierarchy：场景选择、对象创建、删除、原地重命名。
- Inspector：相机和对象属性，继续复用 ComponentDraw。
- RendererSettings：渲染设置。
- AssetImportPanel / EnvironmentImportTask：导入表单、异步环境图准备与主线程提交。重建 UI backend 不取消导入；关闭编辑器时停止并等待任务。

## 外部资源和构建

- Graph 可借用 Image / Buffer，导入时提供初始状态，执行期间追踪状态；可查询最终状态供资源所有者后续使用。
- 所有者必须保证外部资源活到所有引用它的 GPU 工作结束；Graph 不释放借用资源。
- RenderLabRuntime 静态库统一编译运行时代码；应用和手动 GPU 检查复用它，Shader 构建依赖保持统一。
- 不改变 frames-in-flight、同步模型、资产持久化格式或现有渲染算法。
