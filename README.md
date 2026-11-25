# RenderLab

RenderLab 是基于 **slang-rhi / C++23 / Slang** 的路径追踪实验项目。场景由网格对象、解析光源和相机构成，通过 MinimalPathTracer 渲染。预览窗口支持场景切换和相机导航，并可显示场景对象与光源列表。

## 构建与运行

当前主要开发环境为 Windows + CLion。

首次检出时初始化依赖：

```sh
git submodule update --init --recursive
```

[slang-rhi](https://github.com/shader-slang/slang-rhi) 通过 `external/slang-rhi` 子模块固定版本。
设备、资源、同步、呈现和光追均使用其公开接口。ImGui 也直接通过 RHI 绘制。

- 支持 C++23 的编译器。Windows 的 MSVC 配合 `x64-windows`，MSYS2 UCRT64 的 GCC 配合 `x64-mingw-dynamic`。
- CMake 4.2+、Ninja。slang-rhi 自动获取配套的 Slang 编译器及后端依赖，无需单独安装 Vulkan SDK。
- vcpkg 主要依赖 GLFW、GLM、ImGui（Docking 与 GLFW backend）、TinyGLTF、Assimp、TinyEXR、DirectXTex（`directxtex[core]`）、MikkTSpace、nlohmann-json、glog、zlib。
- CMake 获取固定版本的 reflect-cpp。配置、资产和 Shader 缓存通过 `rfl::json` 读写。 [reflect-cpp](https://github.com/getml/reflect-cpp)

在 CLion 中选择编译器和对应的 vcpkg triplet，构建 `RenderLab`。
未指定构建类型时默认使用 Release。同一构建目录使用同一套 CMake / Ninja 工具链。
运行需要支持硬件光线追踪的 Vulkan 设备。

主程序与手动验证工具共用 `RenderLabCore` 的预编译头。TinyGLTF 的实现和资产保存的序列化集中在独立源文件中。
内置 Pass 编译到 RenderLabCore。构建将 [src/render](src/render/) 中的 Slang 源文件部署到当前构建配置的 `shaders/source`。程序从该路径加载 Shader，并将编译缓存写到同一输出目录下的 `shaders/cache`。`BUILD_TESTING` 默认关闭，
手动验证时开启并单独构建所需工具即可。

应用配置保存资源路径、初始场景、编辑器和渲染设置。Engine 启动时读取配置，关闭时写回。启动前按配置准备本地资产和初始场景。 [应用配置](config/config.json)

## 文档导航

| 主题                                                                      | 内容                                |
|---------------------------------------------------------------------------|-------------------------------------|
| [功能清单](docs/Feature.md)                                               | 路径追踪和编辑器功能                |
| [项目架构](docs/Architecture.md)                                          | 场景、资产、Renderer 与编辑器的职责 |
| [渲染模块](docs/Render.md) / [RenderGraph](docs/RenderGraph.md)           | 渲染路线、资源与帧同步              |
| [材质工作流](docs/Material.md) | MR / SG 材质求值、统一 BRDF |
