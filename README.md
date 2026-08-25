# RenderLab

面向**渲染学习**的实践台：每个 Demo 独立实验一种渲染算法；窗口、相机、Shader、Mesh、Editor 等基础设施放在 `base/`。

从 `playground` 迁入并裁剪：只保留渲染相关代码，去掉物理 / 几何 Demo 与 2D overlay 路径。

## 目录

```
base/           共用基础设施
demos/          渲染 Demo（如 shadow/）
shaders/        共享着色器
assets/         共用模型 / 贴图
config/         应用与场景配置
scripts/        构建脚本
```

## 依赖

通过 [vcpkg](https://vcpkg.io/)：`glfw3`、`glad`、`glm`、`assimp`、`imgui`、`imguizmo`、`glog`、`yaml-cpp`。

需设置 `VCPKG_ROOT`。Windows MinGW 默认 triplet：`x64-mingw-dynamic`。

## 构建

```bat
scripts\build.cmd
```

或：

```bat
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target RenderLab
```

## 运行

在项目根目录：

```bat
build\RenderLab.exe
```

默认 Demo 由 `config/app.yaml` 的 `demo` 指定（当前为 `shadow`）。

## 相机

- 右键按住：环视 + WASD/QE 飞行
- 中键拖拽：平移
- 滚轮：沿视线缩放
- 左键：拾取物体（Gizmo）

## 场景配置

物体 / 灯光 / 可选相机写在 `config/scenes/<demo>.yaml`，不要在 Demo 里硬编码。

- `app.yaml` 的 `camera` 是默认相机
- 场景 yaml 里的 `camera` 可选，有则覆盖 app 默认
- Editor 右侧 **Save Scene** 会把当前物体 transform（以及相机、灯光位置）写回对应 yaml

## 新增 Demo

1. 在 `demos/<name>/` 实现 `IDemo`
2. 文件末尾 `REGISTER_DEMO(Class, "name")`
3. 用 `LoadScene(paths::scene_config_dir() / "<name>.yaml", ...)` 加载场景
4. 重写 `scene_config_path()` 以支持保存
5. 算法与专用 shader 放在该 Demo 目录内
