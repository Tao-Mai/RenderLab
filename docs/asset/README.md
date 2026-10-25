# 资产模块

[项目首页](../../README.md) · [资产目录与使用](../../assets/README.md)

资产模块是底层 CPU 数据工具：负责描述、导入、持久化与缓存，不创建 GPU 对象，也不决定场景如何运行。

## 资产类型

| 类型 | 内容 |
| --- | --- |
| MeshAsset | 几何二进制引用、Submesh 材质、局部 AABB、导入来源与比例 |
| MaterialAsset | 多态材质基类，PbrMaterialAsset / OfflineMaterialAsset 共用 ID 空间 |
| TextureAsset / SamplerAsset | 像素、Mipmap、色彩空间与采样参数 |
| ShaderAsset | Shader 程序与构建产物描述 |
| SceneAsset | Actor / Component、编辑器相机、环境图 |
| EnvironmentMapAsset | Radiance、Irradiance、GGX Prefiltered 纹理引用 |

`AssetID<T>` 用类型区分引用，以字符串作为持久化 ID。Submesh 持有 Material ID，而不是整个 Mesh 绑定单一材质。

PBR 材质支持基础参数与 glTF 扩展描述。OfflineMaterialAsset 为 Diffuse / Conductor / Dielectric 等独立离线模型预留数据表达，当前不代表已完成其 Shader；现有路线仍使用 PBR 着色。

## 数据流

```text
源模型 / 图像
  → AssetImporter
  → AssetManager：JSON 描述
  → AssetDataManager：几何 / 像素二进制

渲染时：RenderResourceManager → 读取描述与二进制 → 缓存 GPU 对象
场景加载：SceneManager → 读取 SceneAsset → 创建运行时对象
```

- AssetManager 提供类型化查询、保存与 CPU 缓存。
- AssetDataManager 统一处理文件或内置二进制来源。
- BuiltinAssets 被动提供基础网格、纹理、材质、Sampler 与 LUT。
- GPU 资源创建与场景实例化不进入资产管理器。

## 导入与持久化

- 统一 `importMesh` / `importScene`，按格式选择 TinyGLTF 或 Assimp；不会在 glTF 解析失败后隐式换后端。
- Mesh 预览与保存分离，导入比例烘焙进几何；运行时 Transform 只表示实例变换。
- 场景导入展开节点世界变换、复用共享网格、保留材质并生成编辑器相机。
- 缺失切线时生成 MikkTSpace 切线；普通图像生成完整 Mipmap。
- 环境图导入生成辐照度与 GGX 预过滤结果，可异步准备并取消。

JSON 读写复用 [Serializer.h](../../src/core/Serializer.h) 的 C++26 静态反射。多态类型使用稳定的注册名称，不使用 RTTI 名称；完整描述要求字段明确，空 optional 保存为 null，不保留旧格式解析。

材质覆盖复用 PbrMaterialAsset 的 optional 字段，合并逻辑使用静态反射；仅覆盖有值字段，不重复编写字段复制。

## 内置资源

BRDF / Sheen LUT 与 fallback 纹理同样通过类型化资产 ID 获取。原始 PNG 位于 [builtin/data/](../../src/asset/builtin/data/)，编译时嵌入，按需解码；无需配置路径或导入后的 JSON / bin。

实现入口：[AssetManager](../../src/asset/AssetManager.h)、[AssetDataManager](../../src/asset/AssetDataManager.h)、[AssetImporter](../../src/asset/AssetImporter.h)、[BuiltinAssets](../../src/asset/BuiltinAssets.h)。
