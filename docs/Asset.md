# 资产模块

[项目首页](../README.md) · [资产目录与使用](../assets/README.md)

资产模块是底层 CPU 数据工具：负责描述、导入、持久化与缓存。

## 资产类型

| 类型                        | 内容                                                               |
|-----------------------------|--------------------------------------------------------------------|
| MeshAsset                   | 几何二进制引用、Submesh 材质、局部 AABB、导入来源与比例            |
| MaterialAsset               | 多态材质基类，GltfMaterial / PbrtMaterial 共用 ID 空间 |
| TextureAsset / SamplerAsset | 像素、Mipmap、色彩空间与采样参数                                   |
| ShaderAsset                 | Shader 程序与构建产物描述                                          |
| SceneAsset                  | Actor / Component、编辑器相机、环境图                              |
| EnvironmentMapAsset         | Radiance、Irradiance、GGX Prefiltered 纹理引用                     |
| PbrtSceneAsset / PbrtTextureAsset / PbrtMediumAsset | PBRT 场景、纹理节点、介质模型、设置与输入依赖 |

`AssetID<T>` 用类型区分引用，以字符串作为持久化 ID。每个 Submesh 持有自己的 Material ID。

GltfMaterial 保存 PBR 参数与 glTF 扩展。PbrtMaterial 按离线材质模型保存明确字段，输入保留光谱、色彩空间及材质和纹理引用，导入时转换并保存为内部资产。

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

## 导入与持久化

- 统一 `importMesh` / `importScene`，按格式选择 TinyGLTF 或 Assimp。
- Mesh 预览与保存分离，导入比例烘焙进几何；运行时 Transform 只表示实例变换。
- 场景导入展开节点世界变换、复用共享网格、保留材质并生成编辑器相机。
- glTF 场景保留 `KHR_lights_punctual` 的方向光、点光源和 Spot，导入颜色、照度 / 光强、范围及锥角。节点缩放影响灯的位置，导入比例同时缩放有限范围，光强保持源值。
- 场景相机的可选 `editorCamera.exposure` 保存手动 / 自动模式、EV100、曝光补偿和自动测光参数；设为 `null` 时使用全局曝光，设置后覆盖全局配置，并随场景保存和切换。
- 缺失切线时生成 MikkTSpace 切线；普通图像生成完整 Mipmap。
- DirectXTex 读取单张二维 BC1–BC7 DDS。纹理资产保存源压缩块和全部 Mipmap，Vulkan 按块上传，采样时由 GPU 解码。单独导入时默认采用 DDS 标记的色彩空间，glTF 通过 `MSFT_texture_dds` 选择外部 DDS，并按材质槽位选择线性或 sRGB 采样。
- BC5 法线贴图保存 X、Y 两个分量，材质求值从这两个分量重建正半球的 Z。
- 环境图导入生成辐照度与 GGX 预过滤结果，可异步准备并取消。
- `importPbrtScene` 复用固定版本的官方读取前端，保存场景描述并打包原始依赖；详见 [PBRT 导入](Pbrt.md)。

JSON 读写复用 [Serializer.h](../src/core/Serializer.h) 的 C++26 静态反射。多态类型使用稳定的注册名称，完整描述要求字段明确，空 optional 保存为 null。

材质覆盖复用 GltfMaterial 的 optional 字段，通过静态反射合并有值字段。

## 内置资源

BRDF / Sheen LUT 与 fallback 纹理通过类型化资产 ID 获取。原始 PNG 位于 [builtin/data/](../src/asset/builtin/data/)，编译时嵌入，按需解码。

实现入口：[AssetManager](../src/asset/AssetManager.h)、[AssetDataManager](../src/asset/AssetDataManager.h)、[AssetImporter](../src/asset/AssetImporter.h)、[BuiltinAssets](../src/asset/BuiltinAssets.h)。
