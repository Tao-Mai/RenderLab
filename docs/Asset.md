# 资产模块

[项目首页](../README.md) · [场景说明](Scene.md)

资产模块是底层 CPU 数据工具：负责描述、导入、持久化与缓存。

## 资产类型

| 类型                        | 内容                                                               |
|-----------------------------|--------------------------------------------------------------------|
| MeshAsset                   | 几何二进制引用、Submesh 材质、局部 AABB、导入来源与比例            |
| GltfMaterial                | PBR 参数、贴图与 glTF 材质扩展 |
| TextureAsset / SamplerAsset | 像素、Mipmap、色彩空间与采样参数                                   |
| Scene                  | 网格对象、解析光源、相机和环境图                              |
| EnvironmentMapAsset         | Radiance Cubemap 纹理引用                     |

`AssetID<T>` 用类型区分引用，以字符串作为持久化 ID。每个 Submesh 持有自己的 Material ID。

## 数据流

```text
源模型 / 图像
  → AssetImporter
  → AssetManager：JSON 描述、几何和像素二进制

渲染时：RenderResourceManager → 读取描述与二进制 → 缓存 GPU 对象
场景加载：SceneManager → 读取 Scene → 创建运行时对象
```

- AssetManager 提供类型化查询、保存和 CPU 缓存。网格使用 JSON 描述和预生成的几何二进制，场景通过网格 ID 引用。
- BuiltinAssets 提供基础纹理、材质与 Sampler。

## 导入与持久化

- 统一 `importMesh` / `importScene`，按格式选择 TinyGLTF 或 Assimp。
- 网格准备阶段读取几何并计算包围盒，保存阶段将导入比例烘焙进几何。运行时 Transform 表示实例变换。
- 场景导入展开节点世界变换、复用共享网格、保留材质并生成编辑器相机。
- glTF 场景保留 `KHR_lights_punctual` 的方向光、点光源和 Spot，导入颜色、照度 / 光强、范围及锥角。节点缩放影响灯的位置，导入比例同时缩放有限范围，光强保持源值。
- 缺失切线时生成 MikkTSpace 切线；普通图像生成完整 Mipmap。
- DirectXTex 读取单张二维 BC1–BC7 DDS。纹理资产保存源压缩块和全部 Mipmap，slang-rhi 按块上传，采样时由 GPU 解码。单独导入时默认采用 DDS 标记的色彩空间，glTF 通过 `MSFT_texture_dds` 选择外部 DDS，并按材质槽位选择线性或 sRGB 采样。
- BC5 法线贴图保存 X、Y 两个分量，材质求值从这两个分量重建正半球的 Z。
- 环境图导入生成辐照度与 GGX 预过滤结果，可异步准备并取消。

资产 JSON 通过 reflect-cpp 按 C++ 字段名读写。场景的 `environmentMap` 可以省略。其他资产的持久化字段均须存在，空的 `std::optional` 保存为 `null`。 [JSON 读写](../src/core/Json.h)

## 内置资源

内置纹理通过类型化资产 ID 获取，像素数据直接定义在代码中。 [内置纹理](../src/asset/builtin/BuiltinTexture.cpp)

实现入口包括资产查询、导入以及内置资源。 [AssetManager](../src/asset/AssetManager.h) · [AssetImporter](../src/asset/AssetImporter.h) · [BuiltinAssets](../src/asset/BuiltinAssets.h)
