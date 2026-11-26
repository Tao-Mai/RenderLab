# 项目架构

[项目首页](../README.md) · [渲染模块](Render.md)

RenderLab 将场景逻辑、资产数据和 GPU 执行分开：上层描述“渲染什么”，Renderer 准备 GPU 场景数据，各 Pass 决定“如何渲染”。

## 一帧的数据流

```text
Engine：载入当前场景并更新预览相机
  → GpuScene.update：解析网格，准备材质并录制相机与灯光上传
  → 获取交换链图像
  → RenderGraph.execute：执行已编译的 Pass，按需准备光追场景
  → MinimalPathTracer：追踪光线，后处理生成呈现纹理
  → Submit / Present
```

编辑器相机与编辑器绘制输入由 Editor 独立提交。

## 职责边界

| 模块 | 主要职责 |
| --- | --- |
| Engine / Context | 生命周期与主循环，协调场景、编辑器和渲染 |
| SceneManager | 载入和切换预览场景 |
| AssetManager | CPU 资产描述、缓存与二进制读写 |
| RenderResourceManager | 通过资产 ID 获取并缓存 GPU 网格、纹理和采样器 |
| Renderer | 组装渲染图，安排场景准备、图执行与帧提交 |
| GpuScene | 从 Scene 提取子网格并上传材质、灯光和命中数据 |
| MaterialTable | 按材质资产 ID 上传并绑定材质表和纹理 |
| RenderGraph / Pass | RenderGraphCompiler 解析依赖，ResourceCache 登记命名资源，RenderGraphExe 按序调用 Pass 的 execute |
| Editor | 场景列表、相机导航、渲染设置和界面绘制 |


## 场景与资产

- Scene 直接持有 Object、AnalyticLight 和 Camera。
- reflect-cpp 从聚合类型获取字段描述，多态数据使用“类型名 → 数据”。
- 资产使用类型化 ID 引用，网格几何与纹理像素独立持久化。
- MaterialTable 按材质资产 ID 维护当前场景内的稳定索引，并用当前帧的 encoder 录制 GPU 数据上传。切换场景时重建材质表和关联的绘制、发光面数据，旧场景的纹理不再占用当前场景的索引容量；已提交的命令保留其缓冲区和不可变纹理绑定。
- 场景保存相机和环境图。Context 持有应用配置，Engine 启停时读写资源路径、初始场景、编辑器和渲染设置。 [配置结构](../src/core/Config.h)

详见 [场景模块](Scene.md) 与 [资产模块](Asset.md)。

### 反射与 JSON

reflect-cpp 直接获取聚合结构体的字段与枚举名称。路径追踪的常量缓冲在 Slang 中声明，C++ 通过 `ShaderCursor` 按字段上传设置与采样序号。 [路径追踪](../src/render/impl/passes/minimal/MinimalPathTracer.cpp)

配置、资产和 Shader 缓存通过头文件里的通用模板调用 `rfl::json`。持久化结构只包含需要保存的数据，JSON 异常由模板捕获。 [JSON 读写](../src/core/Json.h)

## 渲染与资源

- Renderer 直接持有 `rhi::IDevice`、Graphics Queue 和 Surface，使用 RHI 接口创建资源、获取和呈现纹理。
- Renderer 用时间线 Fence 管理帧提交，复用槽位前等待完成。
- `GpuScene` 和 `MaterialTable` 各自持有场景与材质的 RHI 缓冲。通过 encoder 录制上传，由 RHI 跟踪资源状态。
- Renderer 在获取交换链图像前调用 `GpuScene::update()`。
- `RenderContext` 将 `GpuScene`、命令 encoder 和资源服务传给各个 Pass。
- Pass 直接使用 RHI render / compute / ray tracing pass encoder，并通过 ShaderCursor 写入原生 shader object。
- 光追 Pass 在开启 RHI Pass 前向 `GpuScene` 请求当帧的光追数据。`SceneAccelerationStructures` 根据已解析的网格和实例输入缓存并构建 BLAS/TLAS。
- Graph Buffer 和历史图像跨帧共享。图重建时等待队列，重新创建该图的资源登记表并重置时域历史。

详见 [RenderGraph](RenderGraph.md) 与 [Shader 参数与管线](RenderLayout.md)。

## 配置与编辑器

- Renderer 初始化时取得渲染配置引用，并通过原生 `RenderContext` 提供给渲染代码。
- 路线或图结构变化时重建 Graph；曝光等数值参数通过逐帧数据更新。
- UI 修改运行配置，Engine 关闭时统一持久化。
- Engine 持有 `Window`，由它负责 GLFW 窗口的创建与销毁、事件循环和尺寸操作。调用方通过原生句柄以 lambda 注册尺寸、按键和滚轮回调。
- EditorBackend 负责 ImGui 的 GLFW 接入与 slang-rhi 绘制。预览面板提供场景切换、只读列表和渲染设置。
- UI 在后处理之后叠加到场景输出。

新增 Pass 时，在 `src/render/` 对应模块声明资源端口和设置，并在 C++ 组图函数中创建 Pass 和连接端口。 [组图实现](../src/render/impl/graphs/minimal/MinimalPathTracerGraph.cpp)
