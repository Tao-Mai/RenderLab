# RenderLab

面向**渲染学习**的实践台：每个 Demo 独立实验一种渲染算法；窗口、相机、资产、Editor 等基础设施放在 `base/`。

## 目录

```
base/           共用基础设施（core / gfx / asset / editor / io / platform）
demos/          渲染 Demo（如 shadow/）
assets/         资产 JSON（scene、mesh）+ models 源文件
config/         app.yaml（窗口、启动场景 UUID、默认相机）
notes/docs/     子系统设计文档
shaders/        共享着色器
```

设计文档见 **[notes/docs/](notes/docs/)**（含 [clangd 配置说明](notes/docs/dev-environment.md)）。

## 依赖

通过 [vcpkg](https://vcpkg.io/)：`glfw3`、`glad`、`glm`、`assimp`、`imgui`、`imguizmo`、`glog`、`yaml-cpp`、`entt`、`nlohmann-json`。

需设置 `VCPKG_ROOT`。Windows MinGW 默认 triplet：`x64-mingw-dynamic`。

## 构建 / 运行（Cursor / VS Code）

1. 安装扩展：`CMake Tools`、`clangd`
2. 打开工程后底部状态栏选 preset `default`、启动目标 `RenderLab`
3. **Ctrl+F5** → 编译并运行

```bat
cmake --preset default
cmake --build --preset default
build\RenderLab.exe
```

`compile_commands.json` 由 CMake 生成在 `build/`，并链接到项目根供 clangd 使用。

## 启动与场景

`config/app.yaml` 用 **Scene 资产 UUID** 指定启动场景：

```yaml
scene: 8c1d4e2f1a003b90
```

场景 JSON 的 `"name": "shadow"` 决定加载哪个 Demo（须与 `REGISTER_DEMO(..., "shadow")` 一致）。详见 [notes/docs/asset-system.md](notes/docs/asset-system.md)。

## 相机

- 右键按住：环视 + WASD/QE 飞行
- 中键拖拽：平移
- 滚轮：沿视线缩放
- 左键：拾取物体（Gizmo）
- **Save Scene**：写回 `assets/scene/<uuid>.json`

## 新增 Demo

1. 在 `demos/<name>/` 实现 `IDemo`，`REGISTER_DEMO(Class, "name")`
2. 添加 `assets/scene/<uuid>.json`，`name` 字段与注册名相同
3. 在 `draw()` / `on_scene_loaded()` 里遍历 `scene.registry()`（见 [notes/docs/scene-system.md](notes/docs/scene-system.md)）
4. 算法与专用 shader 放在该 Demo 目录内
