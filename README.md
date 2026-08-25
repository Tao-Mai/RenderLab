# RenderLab

面向**渲染学习**的实践台：每个 Demo 独立实验一种渲染算法；窗口、相机、Shader、Mesh、Editor 等基础设施放在 `base/`。

## 目录

```
base/           共用基础设施
demos/          渲染 Demo（如 shadow/）
shaders/        共享着色器
assets/         共用模型 / 贴图
config/         应用与场景配置
```

## 依赖

通过 [vcpkg](https://vcpkg.io/)：`glfw3`、`glad`、`glm`、`assimp`、`imgui`、`imguizmo`、`glog`、`yaml-cpp`。

需设置 `VCPKG_ROOT`。Windows MinGW 默认 triplet：`x64-mingw-dynamic`。

## 构建 / 运行（Cursor / VS Code）

项目用标准 **CMake Presets**（`CMakePresets.json`），不依赖自定义脚本：

1. 安装扩展：`CMake Tools`、`clangd`
2. 打开工程后底部状态栏选 preset `default`、启动目标 `RenderLab`
3. **Ctrl+F5**（或状态栏 ▶）→ 编译并运行

命令行等价：

```bat
cmake --preset default
cmake --build --preset default
build\RenderLab.exe
```

## 相机

- 右键按住：环视 + WASD/QE 飞行
- 中键拖拽：平移
- 滚轮：沿视线缩放
- 左键：拾取物体（Gizmo）

## 场景配置

物体 / 灯光 / 可选相机写在 `config/scenes/<demo>.yaml`。

- `app.yaml` 的 `camera` 是默认相机
- 场景 yaml 里的 `camera` 可选，有则覆盖
- Editor 右侧 **Save Scene** 写回 transform / 相机 / 灯光

## 新增 Demo

1. 在 `demos/<name>/` 实现 `IDemo`
2. 文件末尾 `REGISTER_DEMO(Class, "name")`
3. 用 `LoadScene(...)` 加载场景
4. 算法与专用 shader 放在该 Demo 目录内
