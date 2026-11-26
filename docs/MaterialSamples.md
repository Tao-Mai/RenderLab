# 材质示例资产

[材质展示](MaterialGallery.md) · [资产说明](Asset.md)

示例来自 [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)，用于导入验证和材质对比。源文件与各模型许可保存在本地 `resource/material_samples/<模型名>/`。

## 资产列表

| 扩展 | 模型 / Mesh ID | 当前着色 |
| --- | --- | --- |
| Anisotropy | [AnisotropyBarnLamp](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AnisotropyBarnLamp) | 导入时提示不支持 |
| Transmission | [TransmissionRoughnessTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/TransmissionRoughnessTest) | 常量透射与粗糙透射 |
| Volume | [AttenuationTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AttenuationTest) | 仅薄 / 厚表面标志，无体吸收 |
| IOR | [IORTestGrid](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IORTestGrid) | StandardMaterial |
| Specular | [SpecularTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SpecularTest) | StandardMaterial |
| Iridescence | [IridescenceLamp](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IridescenceLamp) | 导入时提示不支持 |
| Dispersion | [DispersionTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DispersionTest) | 导入时提示不支持 |
| Emissive Strength | [EmissiveStrengthTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/EmissiveStrengthTest) | MinimalPathTracer |
| Unlit | [UnlitTest](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/UnlitTest) | 导入时提示不支持 |
| Diffuse Transmission | [DiffuseTransmissionTeacup](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DiffuseTransmissionTeacup) | 常量漫透射与透射颜色 |

所有材质导入为 StandardMaterial。扩展支持范围与纹理合成规则见[材质工作流](Material.md)。

## 导入约定

- 每份示例模型导入为一个 MeshAsset，保留各 Submesh 的材质关联，最长轴统一为 3 m。
- StandardMaterial 保存着色所需的标准参数与纹理槽；源乘法系数在导入阶段合成。
- 当前使用 TEXCOORD_0，导入时检查 UV 通道及纹理变换要求。
- 不保存未实现扩展的附加数据；导入日志明确说明相应限制。

材质展示中的褶皱布料另取自 [SheenCloth](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenCloth)。各模型遵循其来源目录中的独立许可，源模型及生成缓存未完整随仓库提交。
