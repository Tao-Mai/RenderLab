# 资产系统

## 目标

- 非 builtin 资源用 **64 位 UUID**（`AssetId`）标识，按类型分子目录
- `assets/` 只放 **JSON 描述**，源文件（obj 等）放在 `assets/models/` 等路径，由 JSON 引用
- 启动时 `AssetManager` 扫描并加载所有 JSON 到内存
- **builtin** 不进 `assets/`、不分配 UUID

## 目录约定

```
assets/
  mesh/           # MeshAsset JSON，文件名可用 <uuid>.json
  scene/          # SceneAsset JSON
  models/         # 外部模型源文件（obj 等），被 mesh json 的 source 引用
config/
  app.yaml        # 应用配置；scene 字段为启动场景 UUID
```

## 三种 ID（不要混用）

| 概念 | 类型 | 用途 | 写入 JSON？ |
|------|------|------|-------------|
| 资产 UUID | `AssetId` (`uint64_t`) | 磁盘上 mesh/scene 文件身份 | 是 |
| entt 实体 | `entt::entity` | 运行时选中、拾取、Gizmo | 否 |
| 逻辑名 | `TagName` 组件 | Inspector 显示、保存时匹配物体 | 是（`objects[].name`） |

## 核心类型

### `AssetId`（`base/asset/asset_id.h`）

- `format_asset_id` / `parse_asset_id`：十六进制字符串 ↔ `uint64_t`
- `generate_asset_id()`：新建资产时生成

### 资产类（均可反序列化）

每个资产类型对应一个 **纯数据类** + JSON 读写（在 `*_json` 或 `deserialize` 中实现）：

| 类 | 路径 | 说明 |
|----|------|------|
| `MeshAsset` | `base/asset/mesh_asset.*` | `id`, `name`, `source`（`builtin:...` 或相对路径） |
| `SceneAsset` | `base/asset/scene_asset.*` | 场景全局设置 + lights + objects 列表 |

JSON 序列化入口：`scene_asset_json::deserialize` / `serialize`（`base/asset/scene_asset_json.h`）。

**重要**：资产描述里**不能**包含 GPU 句柄（如 `TextureGPU`）。材质用 `SceneMaterialDesc`（仅 vec3/float/string），在 `Scene::instantiate` 时再转成运行时 `Material` 并 `upload`。

### `AssetManager`（`base/asset/asset_manager.*`）

启动流程（`main.cpp`）：

```
reflection::register_all()
AssetManager::init()   // 扫描 assets/mesh、assets/scene
```

主要 API：

- `get_mesh(AssetId)` / `get_scene(AssetId)` / `find_scene_by_name(name)`
- `resolve_mesh_source(mesh_ref)`：`builtin:sphere` 或 mesh UUID → 实际加载路径
- `save_scene_from_runtime(AssetId, Scene&)`：运行时场景写回 JSON（原地更新，不拷贝整份 `SceneAsset`）

## Builtin 资产

- 网格：`LoadMesh("builtin:cube|sphere|plane|...")`（`base/gfx/mesh_loader.cpp`）
- 贴图：`LoadTexture("builtin:white|builtin:color/r,g,b|...")`（`base/gfx/texture_loader.cpp`）
- 场景 JSON 里 `objects[].mesh` 可直接写 `builtin:sphere`，无需 mesh 资产文件

## 应用配置与 Demo 绑定

`config/app.yaml`：

```yaml
scene: 8c1d4e2f1a003b90   # Scene 资产 UUID
```

启动链：

```
app.yaml scene UUID
  → AssetManager::get_scene
  → SceneAsset.name（如 "shadow"）
  → DemoRegistry::create(name)
  → Scene::instantiate(scene_asset)
  → IDemo::on_scene_loaded()
```

Demo 名由 **Scene 资产的 `name` 字段**决定，须与 `REGISTER_DEMO(Class, "shadow")` 一致。

## entt::meta + JSON

`base/core/reflection/meta_json.*`：

- `resolve_type(name)`：按类型名查找已注册 meta
- `from_json(type_name, json)` → `entt::meta_any`：构造默认实例并按成员名填充
- `to_json(meta_any)`：遍历 meta 成员递归写出
- `glm::quat` / `Transform.rotation` 在 JSON 里用欧拉角（度）

场景资产 `entities[].components` 的 key 即类型名，value 由 meta 自动解析，无需手写 `read_transform` 等。

## 新增 Mesh 资产示例

`assets/mesh/a1b2c3d4e5f60718.json`：

```json
{
  "id": "a1b2c3d4e5f60718",
  "type": "mesh",
  "name": "my_model",
  "source": "models/foo.obj"
}
```

场景里引用：`"mesh": "a1b2c3d4e5f60718"`（16 位十六进制，无 `0x` 前缀）。

## 依赖

- `entt`：registry + meta
- `nlohmann-json`：资产 JSON
- `yaml-cpp`：仅 `app.yaml`
