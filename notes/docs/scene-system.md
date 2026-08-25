# 场景系统

## 职责划分

| 层 | 类型 | 职责 |
|----|------|------|
| 磁盘 | `SceneAsset`（JSON） | 可编辑的场景描述，由 `AssetManager` 管理 |
| 运行时 | `Scene`（`entt::registry`） | 实体 + 组件，GPU 上传后的可渲染场景 |
| 算法 | `IDemo` | 自己的 shader/FBO/绘制逻辑，遍历 registry 渲染 |

Editor 负责加载/保存场景、相机、拾取、Gizmo；Demo 不负责读 YAML/JSON 路径。

## 运行时 `Scene`

代码：`base/core/scene.h`、`base/core/scene.cpp`

```cpp
class Scene {
  entt::registry registry_;
  AssetId source_scene_id_;  // 当前实例化自哪个 scene 资产
  glm::vec3 clear_color_, ambient_;
  // ...
  void instantiate(const SceneAsset&, AssetManager&, const SceneCameraConfig& app_camera);
  bool sync_to_asset(SceneAsset& asset) const;
};
```

### 实例化流程

1. `registry_.clear()`，重建默认相机实体
2. 应用 `clear_color` / `ambient`
3. 应用 `app.yaml` 默认相机，再用 scene JSON 的 `camera` 覆盖
4. 为每个 light 创建实体（`Light` + `Transform` + `TagName`）
5. 为每个 object 创建实体：`LoadMesh` → `MeshRenderer::upload` → 挂上 `Transform` / `TagName`

### 保存流程

`Scene::sync_to_asset` 写回：

- 全局：`clear_color`、`ambient`、相机 position/target/fov
- 按顺序更新 `lights[]` 的位置/颜色/强度
- 按 `objects[].name` 匹配实体，写回 `transform` 与材质 albedo 等

Editor 调用 `AssetManager::save_scene_from_runtime(scene_id, scene)` 落盘。

## 组件

| 组件 | 头文件 | 说明 |
|------|--------|------|
| `TagName` | `base/component/tag_name.h` | 逻辑名，对应 JSON `name` |
| `Transform` | `base/component/transform.h` | position / rotation(quat) / scale |
| `Camera` | `base/component/camera.h` | 主相机（第一个带 Camera 的实体） |
| `Light` | `base/component/light.h` | 点光源等 |
| `MeshRenderer` | `base/component/mesh_renderer.h` | CPU `Mesh` + `MeshGPU` + `Material` |
| `DynamicTag` | `base/component/dynamic_tag.h` | 动态网格标记（upload 时传 dynamic） |

## 遍历约定（Demo / Editor）

使用 `base/core/scene_ops.h` 辅助函数，不要再用已删除的 `Entity` 类：

```cpp
auto& reg = scene.registry();

// 所有可绘制网格
for (entt::entity e : scene_ops::mesh_entities(reg)) {
  if (!scene_ops::has_mesh(reg, e)) continue;
  glm::mat4 m = scene_ops::model(reg, e);
  scene_ops::draw(reg, e);
}

// 灯光
for (auto e : reg.view<Light>()) {
  auto& light = reg.get<Light>(e);
}

// 名称
scene_ops::tag_name(reg, e);
```

## Editor 交互

- **选中**：`entt::entity selected_entity_`（`entt::null` 表示未选中）
- **拾取**：`IdPicker` 把 `entt::to_integral(entity)` 写入 R32I 缓冲，读回还原实体
- **Gizmo**：改 `Transform` 组件
- **Save Scene**：`save_scene_from_runtime`，不写 entt 句柄进 JSON

场景重载后应清空选中，避免 entity 复用导致错乱。

## Scene JSON 格式（实体 + 组件）

```json
"entities": [
  {
    "name": "light0",
    "components": {
      "Transform": { "position": [-3, 5, -2], "rotation": [0, 0, 0], "scale": [1, 1, 1] },
      "Light": { "color": [1, 1, 1], "intensity": 1.2 }
    }
  },
  {
    "name": "sphere",
    "components": {
      "Transform": { ... },
      "MeshRenderer": { "mesh": "builtin:sphere", "albedo": [0.75, 0.35, 0.25] }
    }
  }
]
```

- **Light 是挂在实体上的组件**，与 `Transform` 同级，不再使用顶层 `lights[]`
- 组件类型名 = `entt::meta` 注册名（`meta_json::resolve_type` 按名称查找）
- 序列化/反序列化：`base/core/reflection/meta_json.*` 遍历 meta 成员自动读写 JSON

## 反射与组件注册

`reflection::register_all()` 注册 glm、组件 struct 的 meta；`component_registry::register_bindings()` 绑定运行时 `emplace` / `collect`：

| JSON 类型名 | C++ 类型 | 运行时 |
|-------------|----------|--------|
| `Transform` | `Transform` | 同类型组件 |
| `Light` | `Light` | 同类型；`post_process` 同步 `position` ← `Transform` |
| `MeshRenderer` | `MeshRendererDesc` | 实例化为 `MeshRenderer` + `MeshSource` |
| `DynamicTag` | `DynamicTag` | 同类型 |
| `TagName` | `TagName` | 可选；缺省用 `entities[].name` |

## 已废弃

- 顶层 `lights[]` / `objects[]` 数组

## 新增 Demo 检查清单

1. `demos/<name>/` 实现 `IDemo`，末尾 `REGISTER_DEMO(Class, "name")`
2. 添加 `assets/scene/<uuid>.json`，`"name"` 与注册名一致
3. `config/app.yaml` 的 `scene:` 指向该 UUID（或 Editor 内切换 demo 时按 name 找场景）
4. 在 `draw()` 里用 `registry.view<MeshRenderer, Transform>()` 等遍历，在 `on_scene_loaded()` 里读灯光等

## 已废弃

- `base/core/entity.h`、`config/scenes/*.yaml`、`scene_loader`：已由 entt + Scene 资产 JSON 替代
