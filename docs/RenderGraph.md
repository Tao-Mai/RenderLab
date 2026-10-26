# RenderGraph

[渲染模块](Render.md) · [项目架构](Architecture.md)

Renderer 选择渲染流程，Pass 声明工作及资源访问，RenderGraph 据此安排执行顺序、分配资源并处理同步。

## Setup：声明工作与资源

Setup 在组图或重建图时执行，确定有哪些 Pass、它们如何传递结果，以及各自使用哪些资源。Graph 此时记录资源规格，等到 Compile 再创建实际的 Image / Buffer；因此下游 Pass 可以提前查询输入的尺寸、格式，准备对应的管线和布局。

### 登记和连接 Pass

Renderer 按配置选择渲染路线，将需要的 Pass 交给 addPass 登记。PassSetup 提供 Vulkan 上下文、渲染尺寸、帧数据和配置，输入 Handle 或上游 Output 则用于连接资源。Pass 的 setup 返回 Output，后续 Pass 通过其中的 Handle 使用结果。

Graph 会记录每个 Pass 的资源声明，以及 prepare、writeDescriptorSet 和 execute 回调，分别用于每帧准备数据、资源分配后绑定描述符，以及录制命令。Pass 对象由 Renderer 或封装器持有，需要覆盖整张图的使用周期。

多个 Pass 可以由封装器统一持有和连接。Renderer 调用封装器的 setup，由封装器向 Graph 登记内部节点；这些节点仍分别参与依赖分析、同步和执行。

### 资源声明与版本

createImage、createBuffer 声明资源的尺寸、格式、大小和内存要求等规格，并返回 Handle。write 表示当前 Pass 将写入该资源，Graph 会记录生产者并返回新版本的 Handle。

同一资源的各个版本共享底层存储。版本用于区分不同 Pass 写入后的内容，让 Graph 确定生产、读取和覆盖的顺序。例如场景绘制、透明绘制和 UI 可以依次写入同一张颜色图，各自返回颜色的新版本，后续 Pass 使用对应版本作为输入。需要同时保存两份内容时，应分别创建资源。

外部图像通过 createExternalImage 声明规格，由 Renderer 每帧 acquire 后通过 bindImage 绑定实际交换链图像。交换链负责图像的生命周期，Graph 负责本次执行的访问与状态转换。BLAS / TLAS 也通过 Handle 跟踪依赖和同步，底层对象由所属 Pass 创建和持有。

### 依赖与访问声明

Graph 从 addPass 的输入参数收集资源依赖。输入可以是单个 Handle，也可以是包含 Handle 的聚合结构，例如上游 Output；C++26 静态反射用于收集结构内部的有效 Handle。

Pass 在 setup 中的 write 和 use 都会被 Graph 记录。write 除了标记生产者，还会让当前 Pass 依赖被覆盖的旧版本，保证旧内容的生产者先执行。

use 描述资源在哪个流水线阶段、以什么方式访问；图像还需要指定布局。例如，同一张图可以先作为颜色附件写入，再作为纹理采样。Graph 根据这些声明生成同步，并汇总创建资源所需的 Usage，包括附件、采样、Storage 和传输等用途。缓冲的 Usage 还包含创建时显式指定的用途。

资源依赖与访问状态分别服务于调度和同步：上游结果通过输入参数传递，write 补充覆盖依赖，use 声明执行时的访问要求。同一 Pass 对同一 Handle 的多次 use 会合并访问阶段和权限，图像布局需保持一致。

### 声明最终出口

EndPass 接收最终颜色和回读缓冲，将呈现与 CPU 回读连接到同一个出口，并声明 Present 布局和 HostRead 访问。它决定哪些结果需要保留，也让 Graph 完成提交前的最终状态转换。

当前 Graph 以最后登记的节点为终点，Renderer 因此在其他节点之后登记 EndPass。需要保留的分支都应通过资源依赖连接到这个出口。

## Compile：确定调度与资源绑定

Compile 将 Setup 的声明变成可执行的图：保留最终结果需要的 Pass，建立完整的读写顺序，分配资源并写入描述符。编译结果供后续每帧复用。

### 保留有效节点

Graph 从最终出口沿资源的生产关系向前追溯，保留出口依赖的全部 Pass，并建立生产者到消费者的执行约束。Renderer 统一上传的灯光缓冲等资源可直接作为输入。

图仅保留通过资源依赖连接到出口的分支。所有节点都经历 Setup，保留的节点继续参与后续的描述符绑定、帧准备和执行。

### 确定读取与覆盖顺序

资源版本确定基本的生产顺序：使用某个版本的 Pass，必须等待生成该版本的 Pass 完成。由于各版本共享存储，Graph 还会增加旧版本读取者到后续写入者的约束，保证覆盖之前已完成所有需要保留的读取。

例如 A 生成颜色，B 读取这份颜色，C 在原图上继续写入。A 必须先于 B 和 C，B 也必须先于 C，否则 B 可能读到 C 修改后的内容。这条额外约束使得分支读取和原地覆盖能够正确共存，只在保留的节点之间建立。

同一个版本只能作为一次 write 的源，连续写入应使用上一次返回的新版本。对同一版本发起两次覆盖写会报错，避免写入次序含糊；这项检查覆盖所有已登记节点。多个不同版本依次写入同一资源则是正常用法。

### 排序与同步准备

Graph 使用拓扑排序安排执行顺序，确保每个 Pass 的前置依赖都已满足。生产、读取和覆盖约束如果构成循环，说明无法找到满足资源使用要求的顺序，Compile 会报错。重复覆盖和循环检查在所有构建中生效。

最终节点仍在同一队列上顺序执行。对于外部呈现图像，Compile 还会确定其首次使用阶段，Renderer 据此设置交换链 acquire 信号量的等待阶段，让等待与实际首次访问衔接。

### 分配资源与写入描述符

Graph 按声明的规格和汇总后的 Usage 创建物理资源。普通图像使用 DeviceLocal 内存，缓冲使用各自声明的内存属性。同一资源的版本共用一次分配，不同资源各自分配。外部图像与加速结构使用所属对象提供的实际资源。

当前节点裁剪只减少后续回调和执行，资源分配仍包含 Setup 声明的全部资源，Usage 也保留全部访问声明的累计结果。

资源创建完成后，Graph 调用保留节点的 writeDescriptorSet，由 Pass 获取实际图像、缓冲和所需 View，完成描述符绑定。这使管线和布局可以在 Setup 准备，依赖实际资源的绑定则统一放在分配之后。

Compile 完成后，执行顺序、资源及绑定可以持续复用。具体 Barrier 在每次 Execute 时根据资源当前状态生成，以适应逐帧上传和跨帧访问。

## Execute：上传、同步与命令录制

每帧先等待对应 FIF 的 Fence，再准备帧数据。Renderer 的全局数据准备和各 Pass 的 prepare 通过 staging pool 写入上传内容，Graph 在 Execute 开始时录制 Buffer Copy，并在消费者访问前完成上传后的同步。

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

渲染路线、HDR、IBL、阴影开关或资源规格等改变时，需要重新 Setup 和 Compile。Resize 也会释放旧图资源与 View，重建交换链，再重建图。

普通物体变换、材质参数和灯光数据变化只更新帧数据。环境图变化更新资源绑定，IBL 开关则决定天空盒等节点是否加入图，因此会触发重建。

实现入口：[RenderGraph.h](../src/render/feature/RenderGraph.h)（节点登记与反射）、[RenderGraphSetup.cpp](../src/render/feature/RenderGraphSetup.cpp)（资源声明与依赖编译）、[RenderGraph.cpp](../src/render/feature/RenderGraph.cpp)（分配、上传与执行）。
