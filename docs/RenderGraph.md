# RenderGraph

[渲染模块](Render.md) · [项目架构](Architecture.md)

`Falcor::RenderGraph` 采用 Falcor 的命名 Pass、反射、编译器、执行器和资源缓存结构。C++ Pass 继承 `Falcor::RenderPass`，Python 通过 `falcor.RenderGraph` 组合 Pass。C++ 编译器解析连线、裁剪节点和分配资源，执行器按编译顺序录制 RHI 命令。 [Falcor RenderGraph 源码](https://github.com/NVIDIAGameWorks/Falcor/tree/759aad033ff610fb0d82c74f7e0a508d0096d5f2/Source/Falcor/RenderGraph)

## Pass 插件

每个 Pass 目录用 `renderlab_add_plugin()` 构建共享库，在自己的 `registerPlugin(PluginRegistry&)` 入口登记类型和可选的编辑器回调。CMake 生成 `plugins/plugins.json`，宿主启动时按清单加载插件并校验 ABI。插件实例持有动态库的生命周期，宿主和插件共用 `RenderLabCore`。

## Python 构图

`config/config.json` 的 `renderGraphs` 按 `RenderPath` 分组，组内用图名映射相对于配置文件的脚本路径。
应用按场景模式和设备能力提供图选择。默认入口为 [Forward](../graphs/forward.py)、
[Deferred](../graphs/deferred.py)、[ReSTIR](../graphs/realtime.py)、[MinimalPathTracer](../graphs/minimal_path_tracer.py)、
[PT](../graphs/pt.py) 和 [BDPT](../graphs/bdpt.py)。
四个光追积分器各有自己的插件和图参数，两张离线图用于离线场景。
`renderer.json` 的 `realtimeGraph` 与 `offlineGraph` 分别记录两种场景模式选择的图名。`CaptureScene` 的图参数也使用这些名称。

RenderLab 在隔离的 `Falcor::Scripting::Context` 中读取脚本，用 `RenderGraphIR::getFuncName()` 从图名定位并调用 `render_graph_*()` 工厂。工厂用 `create_pass(pass_name, pass_type, dict)`、`add_edge(src, dst)` 和 `mark_output(name)` 构建同名的 `falcor.RenderGraph`。宿主通过 `getActiveGraph()` 获取当前图。通过 `RenderGraph::createFromFile()` 运行脚本时，模块用默认参数构图并通过 `m.addGraph()` 注册。

资源端口使用 `"实例.端口"`。Renderer 在图执行后读取首个标记的颜色输出，将它绘制到交换链，并在同一次渲染中合成编辑器 UI。光源和材质缓冲由渲染后端通过帧 shader 参数和材质表共享。插件工厂创建 Pass，编译器检查端口方向、类型、格式和必需连线。

`RenderGraphExporter::save()` 将 Pass 参数、连线和输出掩码写入 Python 脚本。`RenderGraphImporter::import()` 按名称调用图工厂，通过 `getObject<ref<RenderGraph>>()` 读取结果。`importAllGraphs()` 通过 `getObjects<ref<RenderGraph>>()` 读取脚本中定义的图。宿主用 `setInput()` 重新绑定外部资源。

图工厂通过带默认值的 Python 关键字参数声明可配置项，例如 `render_graph_PT(*, hdr: bool = True, postProcess: str = "RenderLab", bloom: bool = True)`。Renderer 将 `renderer.json` 中该图的覆盖值传给工厂，并将与 `RenderFeature` 同名的参数同步到渲染设置。Editor 面板的 Graph 区从同一组默认值生成控件。HDR 关闭时隐藏后处理选项，Bloom 控件在 HDR 且使用 RenderLab 后处理时显示。隐藏选项的覆盖值会保留。提交修改后重建图。 [RendererUi.cpp](../src/editor/RendererUi.cpp)

环境图在 Editor 面板的 Environment 区选择，随场景保存。IBL 开关是当前图的参数。

各图调用同一个 [`add_post_process()`](../graphs/modules/post_process.py)
组装后处理。模块将 Pass 登记到当前图，由 RenderGraph 统一编译。随后 [编辑器输出模块](../graphs/modules/editor.py)
仅在 Forward 和 Deferred 中使用场景深度做物体拾取。编辑器 UI 叠加在后处理的颜色结果上。

`add_post_process()` 将最终颜色标为首输出，宿主用 `getOutput(0)` 取得呈现资源。
RenderLab 后处理同时标记 `exposure.state`。编辑器启用相机曝光覆盖时，通过 `getPass()` 和 `getProperties()` 读取该 Pass 当前的曝光设置。
图编译后，可以用 Python 的 `graph.get_output(name)` 获取已标记的 `RhiTexture` 或 `RhiBuffer`。`RhiTexture.format` 返回 `falcor.Format`。`mark_output(name, mask)` 的掩码使用 `TextureChannelFlags`，省略时使用 `TextureChannelFlags.RGB`。

`renderer.json` 的 `SceneObjects:enabled` 选项控制编辑器左侧的 Scene Objects 面板，默认关闭。切换时窗口按左栏宽度调整并在当前屏幕居中。编辑器配置中的宽度以收起左栏时的宽度保存。
场景下拉框位于右侧编辑器顶部。

## Pass 参数与编辑器

Pass 通过 `getProperties()` 和 `setProperties()` 读写属性。各 Pass 持有自己的参数并负责校验。
`Properties` 负责与 Python 字典互转。构图、Pass 的 Python 属性接口和 `RenderGraphIR` 导出共用这条转换路径。
影响反射或管线的属性变更通过 `requestRecompile()` 通知图重新编译。

插件通过 `registerClass<RenderPass, T>()` 注册使用设备和属性构造实例的 `T::create()`。
编辑器调用 `RenderGraph::renderUI()`，执行器按顺序调用活跃 Pass 的 `renderUI()`。
Pass 可通过 `drawPassEditor()` 生成布尔、枚举及标注了 `RL_SLIDER` 的数值控件，也可提供绘制函数设置标签、范围和提示。
ShadowPass 关闭时隐藏自己的控件。

生效顺序为 Pass 默认值、Python `properties`、用户选项。`renderer.json` 的 `options` 使用 Falcor 的冒号分隔名称。
`renderGraphs:<图名>:parameters:<参数名>` 保存图参数。`renderGraphs:<图名>:passes:<Pass 类型>:<实例名>:<属性名>` 保存实例属性。
编辑器的 Reset settings 恢复该实例的脚本初值。
普通修改在下一帧应用，Pass 可以在 `execute()` 中重置受影响的历史。需要改变资源或连线时重建图。

## Pass 与资源

- `reflect(const CompileData&)` 使用 `addInput()`、`addOutput()`、`addInputOutput()` 和 `addInternal()` 声明命名资源。`Field` 用 `texture2D()`、`textureCube()`、`rawBuffer()`、`rhi::Format` 和 `ResourceBindFlags` 描述资源。反射使用 Pass 参数、图的默认纹理规格和 `CompileData.connectedResources`。
- `compile(RenderContext*, const CompileData&)` 在图资源分配前依据反射准备 Pass。
- `execute(RenderContext*, const RenderData&)` 通过 `getTexture()`、`getResource()` 或 `operator[]` 访问已分配的图资源，更新 Pass 数据并录制命令。Pass 通过具名 shader 参数绑定实际图资源。

`addInputOutput()` 表示原地读写。例如宿主将交换链图像绑定到 `toneMapping.color`，下游继续使用同一资源。`add_edge()` 接受两个裸 Pass 名时只建立执行顺序。

## 图编译

`RenderGraphCompiler` 从标记输出和显式执行依赖找到活跃 Pass，并用同一张依赖图进行拓扑排序。
原地覆盖时，脚本用执行边声明需要先完成的读者。连续覆盖依次连接上一次写入的输出。

编译器通过 `ResourceCache::registerField()` 登记活跃端口和资源别名。所有活跃 Pass 的 `compile()` 返回后，`ResourceCache::allocateResources()` 通过 `rhi::IDevice` 分配原生纹理和缓冲。
`ExposurePass` 的直方图、后处理的图像金字塔和 ReSTIR 的临时缓冲通过 `addInternal()` 声明，由图编译器分配。光追 Pass 通过 `rhi::IDevice::createBuffer()` 创建并持有随场景数据增长的缓冲。
`RenderGraphExe` 保存执行顺序，Pass 通过 `RenderData` 获取资源。Renderer 为命令记录提供 GpuResources 与原生 `rhi::ICommandEncoder`；Surface 在 RHI 内管理 acquire / present 同步。

## 图执行

Renderer 等待在途槽位的 Fence 并写入全局帧数据，然后调用 `RenderGraph::execute()`。
光追路线执行前，Renderer 将本帧 TLAS 构建录入命令缓冲，RHI 录制上下文持有指向该加速结构的引用。

Pass 可以用 `ChannelList` 与 `addRenderPassInputs()`、`addRenderPassOutputs()` 声明纹理端口。`getValidResourceDefines()` 为可选端口生成字符串值为 `"0"` 或 `"1"` 的 `DefineList`。Renderer 在执行前初始化共享字典的 `kRenderPassRefreshFlags`，会改变输出的光追选项通过同一个键发布 `RenderOptionsChanged`。

Renderer 先录制宿主准备的数据上传。执行器按拓扑顺序调用 Pass，并在执行时登记计时。每个 Pass 录制绘制、计算或光追操作与局部上传，RHI 跟踪资源状态。
光栅图将 `picking.pickingImage` 标记为颜色之后的图输出。收到拾取请求时，Renderer 通过 `RenderGraph::getOutputTexture()` 取得对象 ID 纹理，复制点击像素并等待 CPU 回读。呈现资源的状态由 RHI Surface 管理。

`GpuResources` 管理稳定资源 handle、缓冲容量、上传和历史版本。缓冲扩容创建新的原生对象，生产者通过 `RenderData::setBuffer()` 更新图端口；下游端口别名和图输出随之引用新对象。
每次录制操作从当前资源构建具名 shader 绑定，slang-rhi 根据实际用法生成状态转换和读写屏障。

## 重建边界

切换渲染路线或修改脚本参数时，Renderer 加载脚本生成的新图。属性热改通过 `RenderPass::setProperties()` 生效。资源或管线发生变化时，Pass 调用 `requestRecompile()`，下一帧 Renderer 等待 RHI 队列完成旧图的提交，重新创建 GpuResources 并编译图。登记表的生命周期与一次图编译结果一致，所有 Pass 在编译时重新登记资源。
Resize 通过 `rhi::ISurface::configure()` 更新呈现尺寸，并通过 `RenderGraph::onResize(Extent2D, rhi::Format)` 更新图的默认纹理规格。

`RenderGraph::update()` 用传入图更新 Pass 集合、连线和输出标记，保留同名 Pass 实例及其属性。
编译时缺少标记输出或必需输入、以及同一输入同时有连线和外部资源时，`RenderGraph::compile(context, log)` 返回 `false` 并写入诊断。修复图后可以再次编译。

场景管理器持有运行时 `Scene`。更换场景时，`RenderGraph::setScene()` 通知所有 Pass 并触发重编译。环境图变化更新资源绑定，并经 `onSceneUpdates()` 发送 `EnvMapChanged`。普通物体变换、材质参数和灯光数据变化更新帧数据。

相关实现见 [RenderGraph](../src/render/graph/RenderGraph.h)、[RenderGraphCompiler](../src/render/graph/RenderGraphCompiler.cpp)、[RenderGraphExe](../src/render/graph/RenderGraphExe.cpp) 和 [ResourceCache](../src/render/graph/ResourceCache.cpp)。
