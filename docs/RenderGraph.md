# RenderGraph

[渲染模块](Render.md) · [项目架构](Architecture.md)

Python 选择 Pass 并连接资源。C++ Pass 声明资源访问，RenderGraph 据此安排执行顺序、分配资源并处理同步。

## Pass 插件

每个 Pass 目录用 `renderlab_add_plugin()` 构建共享库，在自己的 `registerPlugin(PluginRegistry&)` 入口登记类型和可选的编辑器回调。CMake 生成 `plugins/plugins.json`，宿主启动时按清单加载插件并校验 ABI。插件实例持有动态库的生命周期，宿主和插件共用 `RenderLabCore`。

## Python 构图

`config/config.json` 的 `renderGraphs` 登记每张图的 `id`、显示名 `label`、C++ 渲染类型 `renderPath` 和
相对于配置文件的 `script` 路径。应用按场景模式和设备能力提供 Graph 选择。默认入口为 [Forward](../graphs/forward.py)、
[Deferred](../graphs/deferred.py)、[ReSTIR](../graphs/realtime.py)、[MinimalPathTracer](../graphs/minimal_path_tracer.py)、
[PT](../graphs/pt.py) 和 [BDPT](../graphs/bdpt.py)。
四个光追积分器各有自己的插件和图参数，两张离线图用于离线场景。
`renderer.json` 的 `realtimeGraph` 与 `offlineGraph` 分别记录两种场景模式选择的 Graph ID。

脚本提供 `build_graph(graph)`。`graph.create_pass(name, type, properties)` 声明实例，`graph.add_edge(source, target)` 以 `"实例.端口"` 连接资源。`graph.input(name)` 返回宿主输入端口。C++ 通过插件工厂创建实例，并验证端口方向、类型、格式约束和必需连线。

`graph.param()` 同时声明默认值和编辑器控件，返回应用当前图实例覆盖后的值。把声明写在使用该参数的分支里。

```python
if graph.param("bloom", True):
    graph.create_pass("bloom", "Bloom", {"intensity": 0.04}, ui=True)
    graph.add_edge(color, "bloom.hdrColor")
    color = "bloom.color"
```

布尔参数显示复选框，枚举或带 `choices=(...)` 的字符串参数显示下拉框。数值默认显示输入框，有
`bounds=(minimum, maximum)` 时显示滑块。标签由名字生成，可以通过 `label="IBL"` 指定，也可以用
`ui=False` 声明隐藏参数。与 C++ `RenderFeature` 同名的参数会同步到原生渲染设置，原生枚举的选项由其类型提供。

只有执行过的声明才会生成控件。隐藏的参数保留实例覆盖值，重新进入分支时恢复。修改图参数会在提交后重建图。

`graphs/` 存放 Python 组图入口和共享模块。Editor 面板的 Graph 区由编辑器模块绘制。 [GraphSettingsUi.cpp](../src/editor/GraphSettingsUi.cpp)

环境图在 Editor 面板的 Environment 区选择，随场景保存。IBL 开关是当前图的参数。

各图调用同一个 [`add_post_process(graph, sceneColor, target)`](../graphs/modules/post_process.py)
组装后处理。模块将 Pass 登记到当前图，由 RenderGraph 统一编译。随后 [编辑器输出模块](../graphs/modules/editor.py)
仅在 Forward 和 Deferred 中使用场景深度做物体拾取。编辑器 UI 叠加在后处理的颜色结果上。

`graph.mark_output("editorUi.color", name="color")` 指定交换链目标 `presentTarget` 的写入版本。两张光栅图还标记各阴影资源端口，可以选择加入拾取回读 `pickingReadback`。Python 在建图和重建时运行，逐帧调度和 Vulkan 命令录制由 C++ 执行。

`graph.showObjectList` 控制编辑器左侧的 Scene Objects 面板，默认值为 `False`。在 `build_graph()` 中将其
设为 `True` 即可显示列表。切换时窗口按左栏宽度调整并在当前屏幕居中。编辑器配置中的宽度以收起左栏时的宽度保存。
场景下拉框位于右侧编辑器顶部。

## Pass 参数与编辑器

Pass 通过 `ConfigurablePass<Settings>` 持有类型化参数。`Settings` 声明可设置字段和默认值，可以提供
`validate()` 校验值，并通过 `requiresGraphRebuild(current)` 指出资源或管线改动是否需要重建图。

插件用 `makePassEditor<MyPass>()` 生成布尔、枚举及标注了 `RL_SLIDER` 的数值控件。
复杂界面可以在 Pass 旁边编写 `*Ui.cpp`，再用 `makePassEditor<MyPass, drawSettings>()` 注册回调。
回调接收 `Gui::Widgets&`，Pass 定义具体参数的控件标签、范围和提示，编辑器负责实现通用控件及 ImGui 后端。
回调通过 `PluginRegistry::registerClass()` 与工厂一起发布。注册时显式指定图使用的 Pass 类型名，图通过 `graph.create_pass(..., ui=True)` 展示当前实例的控件。

生效顺序为 Pass 默认值、Python `properties`、实例覆盖值。`renderer.json` 中的 `graphOverrides` 按 Graph ID 保存用户设置。
`parameters` 保存图参数，
`passes` 按 Pass 类型与实例名保存 Pass 参数。编辑器的 Reset settings 恢复该实例的脚本初值。
普通修改在下一帧应用，Pass 可以在准备阶段重置受影响的历史。需要改变资源或连线时重建图。

## Pass 编译与执行

`reflect()` 声明命名输入输出，`compile()` 读取已连接的端口、声明 GPU 资源并发布输出。图编译器按连线排序，从标记输出追溯活跃节点。GPU 资源分配后执行 `bindResources()`。逐帧依次执行 `prepare()` 和 `execute()`。图按类型和实例名持有 Pass，重建时可以复用 Pipeline 和异步任务。

Bloom、SVGF 等复合 Pass 继承 `CompositeRenderPass`，在 `compile()` 中用 `graph.addStage()` 展开内部命令阶段，各阶段独立声明资源访问。

### 资源声明与版本

createImage、createBuffer 声明资源的尺寸、格式、大小和内存要求等规格，并返回 Handle。write 表示当前 Pass 将写入该资源，Graph 会记录生产者并返回新版本的 Handle。

同一资源的各个版本共享底层存储。版本用于区分不同 Pass 写入后的内容，让 Graph 确定生产、读取和覆盖的顺序。例如场景绘制、透明绘制和 UI 可以依次写入同一张颜色图，各自返回颜色的新版本，后续 Pass 使用对应版本作为输入。需要同时保存两份内容时，应分别创建资源。

外部图像通过 createExternalImage 声明规格，由 Renderer 每帧 acquire 后通过 bindImage 绑定实际交换链图像。交换链负责图像的生命周期，Graph 负责本次执行的访问与状态转换。

### 依赖与访问声明

图编译器从命名端口连线确定 Pass 的编译依赖。底层执行图从 `use()` 和 `write()` 产生的资源访问与版本关系确定命令依赖。`write()` 同时记录生产者和被覆盖的旧版本，保证旧内容的读写顺序。

use 描述资源在哪个流水线阶段、以什么方式访问；图像还需要指定布局。例如，同一张图可以先作为颜色附件写入，再作为纹理采样。Graph 根据这些声明生成同步，并汇总创建资源所需的 Usage，包括附件、采样、Storage 和传输等用途。缓冲的 Usage 还包含创建时显式指定的用途。

上游结果通过命名端口传递。`write()` 声明覆盖依赖，`use()` 声明执行时的访问要求。同一阶段对同一 Handle 的多次 `use()` 会合并访问阶段和权限，图像布局需保持一致。

### 声明最终出口

`graph.mark_output()` 保留指定的输出及其生产链。Renderer 添加 EndPass，为最终颜色声明 Present 布局，并为可选的拾取回读缓冲声明 HostRead 访问。EndPass 调用 `retainStage()` 保留提交前的状态转换。

## Compile：确定调度与资源绑定

编译过程保留输出需要的 Pass，建立读写顺序，分配资源并写入描述符。编译结果供后续每帧复用。

### 保留有效节点

图编译器从标记输出沿连线追溯需要编译的 Pass。命令图再从这些输出和 `retainStage()` 标记的阶段追溯生产关系，找出参与资源绑定、帧准备和执行的阶段。Renderer 统一上传的灯光缓冲等资源可直接作为输入。

### 确定读取与覆盖顺序

资源版本确定基本的生产顺序：使用某个版本的 Pass，必须等待生成该版本的 Pass 完成。由于各版本共享存储，Graph 还会增加旧版本读取者到后续写入者的约束，保证覆盖之前已完成所有需要保留的读取。

例如 A 生成颜色，B 读取这份颜色，C 在原图上继续写入。A 必须先于 B 和 C，B 也必须先于 C，否则 B 可能读到 C 修改后的内容。这条额外约束使得分支读取和原地覆盖能够正确共存，只在保留的节点之间建立。

同一个版本只能作为一次 write 的源，连续写入应使用上一次返回的新版本。对同一版本发起两次覆盖写会报错，避免写入次序含糊；这项检查覆盖所有已登记节点。多个不同版本依次写入同一资源则是正常用法。

### 排序与同步准备

Graph 使用拓扑排序安排执行顺序，确保每个 Pass 的前置依赖都已满足。生产、读取和覆盖约束如果构成循环，说明无法找到满足资源使用要求的顺序，Compile 会报错。重复覆盖和循环检查在所有构建中生效。

最终节点仍在同一队列上顺序执行。对于外部呈现图像，Compile 还会确定其首次使用阶段，Renderer 据此设置交换链 acquire 信号量的等待阶段，让等待与实际首次访问衔接。

### 分配资源与写入描述符

Graph 按声明的规格和汇总后的 Usage 创建物理资源。普通图像使用 DeviceLocal 内存，缓冲使用各自声明的内存属性。同一资源的版本共用一次分配，不同资源各自分配。外部图像由所属对象提供。

资源分配涵盖活跃 Pass 的 `compile()` 所声明的资源。资源创建完成后，Graph 调用保留阶段的 `bindResources()`，由 Pass 获取实际图像、缓冲和所需 View 并绑定描述符。

Compile 完成后，执行顺序、资源及绑定可以持续复用。具体 Barrier 在每次 Execute 时根据资源当前状态生成，以适应逐帧上传和跨帧访问。

## Execute：上传、同步与命令录制

每帧先等待对应 FIF 的 Fence，再准备帧数据。Renderer 的全局数据准备和各 Pass 的 prepare 通过 staging pool 写入上传内容，Graph 在 Execute 开始时录制 Buffer Copy，并在消费者访问前完成上传后的同步。

光追图执行前，Renderer 录制场景加速结构的构建与屏障，并通过 `PassFrame::sceneTlas` 将当前帧槽位的 TLAS 交给光追 Pass 读取。网格 BLAS 缓存归 Renderer 所有，可跨渲染图复用。

随后 Graph 按编译顺序执行各节点：根据 use 声明处理资源状态转换和读写同步，再调用 Pass 的 execute 录制绘制、计算或复制命令。执行到 EndPass 时，最终图像转入 Present 布局，回读缓冲转入 HostRead 状态；提交完成后的读取由 Renderer / Editor 负责。

每次绑定的外部呈现图像会丢弃旧内容，从 Undefined 布局开始使用；首次转换与 acquire 等待阶段衔接。Graph 持有的资源则保留上一帧的访问状态，继续建立跨帧同步。

## 资源与跨帧同步

| 资源 | 管理方式 |
| --- | --- |
| 深度、阴影图、G-buffer、HDR 中间结果 | Graph 持有，全局一份，通过 Handle 访问 |
| 材质、灯光、阴影数据、光追命中数据与离线累积 SSBO | Graph 持有，全局一份；CPU 更新经 staging 上传，离线累积由 shader 直接读写 |
| Staging pool | Graph 按 FIF 持有；Fence 完成后复用，线性分配，空间不足时增加块 |
| 拾取回读缓冲 | Graph 持有一份；等待提交完成后立即读取 |
| 外部呈现目标 | Renderer 选择图像后绑定；Graph 处理本次执行的状态转换 |
| Mesh / Texture / Sampler | RenderResourceManager 缓存 |

共享资源的访问状态在帧间保留，下一帧首次访问需要与上一帧尚有影响的读写建立同步。同一布局下的连续只读访问可以合并；布局改变或涉及写入时需要 Barrier。连续只读的阶段和权限会累计，保证后续写入等待这些读取全部完成。

当前同步粒度为整个 Image / Buffer，所有访问都在同一队列上处理。图像的 ImageView 由 Image 对象缓存，交换链图像及其 View 的生命周期由 Swapchain 管理。

## 重建边界

渲染路线、HDR、IBL、阴影开关或资源规格等改变时，Renderer 重新编译图。Resize 也会释放旧图资源与 View，重建交换链，再重建图。

普通物体变换、材质参数和灯光数据变化只更新帧数据。环境图变化更新资源绑定，IBL 开关则决定天空盒等节点是否加入图，因此会触发重建。

类型工厂、端口连接和活跃 Pass 由 [RenderGraphCompiler.cpp](../src/render/graph/RenderGraphCompiler.cpp) 处理。命令阶段的资源声明与依赖编译在 [RenderGraphSetup.cpp](../src/render/graph/RenderGraphSetup.cpp)，资源分配、上传与执行在 [RenderGraph.cpp](../src/render/graph/RenderGraph.cpp)。
