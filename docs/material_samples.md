# 材质示例资产

[材质展示](material_gallery.md) · [资产说明](../assets/README.md)

示例来自 [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)，用于导入验证和材质对比。源文件与各模型许可保存在本地 `resource/material_samples/<模型名>/`。

## 资产列表

| 扩展 | 模型 / Mesh ID | 当前着色 |
| --- | --- | --- |
| Clearcoat | [ClearCoatTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/ClearCoatTest) | Forward |
| Sheen | [SheenTestGrid](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenTestGrid) | Forward / Deferred |
| Anisotropy | [AnisotropyBarnLamp](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AnisotropyBarnLamp) | 仅数据 |
| Transmission | [TransmissionRoughnessTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/TransmissionRoughnessTest) | 仅数据 |
| Volume | [AttenuationTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AttenuationTest) | 仅数据 |
| IOR | [IORTestGrid](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IORTestGrid) | 仅数据 |
| Specular | [SpecularTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SpecularTest) | 仅数据 |
| Iridescence | [IridescenceLamp](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IridescenceLamp) | 仅数据 |
| Dispersion | [DispersionTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DispersionTest) | 仅数据 |
| Emissive Strength | [EmissiveStrengthTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/EmissiveStrengthTest) | 光栅 / 实时光追 / 离线 |
| Unlit | [UnlitTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/UnlitTest) | 仅数据 |
| Diffuse Transmission | [DiffuseTransmissionTeacup](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DiffuseTransmissionTeacup) | 仅数据 |

“仅数据”表示参数与纹理引用可导入、持久化、编辑，但 Shader 尚未计算对应扩展，画面仍使用基础 PBR。不能以成功加载模型代替材质效果验证。

## 导入约定

- 每份示例模型导入为一个 MeshAsset，保留各 Submesh 的材质关联，最长轴统一为 3 m。
- PbrMaterialAsset 保存扩展参数与纹理引用；按用途区分 sRGB / Linear，并生成 Mipmap。
- 当前使用 TEXCOORD_0，不支持 KHR_texture_transform；不支持的坐标需求在导入时明确检查。
- Volume 的无限衰减距离保存为 null，不向 JSON 写入无穷值。
- 示例资产不会自动改变全局曝光、渲染路线或默认场景。

材质展示中的褶皱布料另取自 [SheenCloth](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenCloth)。各模型遵循其来源目录中的独立许可，源模型及生成缓存未完整随仓库提交。
