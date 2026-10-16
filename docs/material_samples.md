# 材质示例资产

来源：[Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)。
源文件及模型许可保存在 `resource/material_samples/<模型名>/`，导入后的 Mesh ID 与模型名相同。

| 材质扩展 | 模型 / Mesh ID | 官方来源 |
|---|---|---|
| Clearcoat | `ClearCoatTest` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/ClearCoatTest) |
| Sheen | `SheenTestGrid` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenTestGrid) |
| Anisotropy | `AnisotropyBarnLamp` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AnisotropyBarnLamp) |
| Transmission | `TransmissionRoughnessTest` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/TransmissionRoughnessTest) |
| Volume | `AttenuationTest` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AttenuationTest) |
| IOR | `IORTestGrid` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IORTestGrid) |
| Specular | `SpecularTest` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SpecularTest) |
| Iridescence | `IridescenceLamp` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/IridescenceLamp) |
| Dispersion | `DispersionTest` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DispersionTest) |
| Emissive Strength | `EmissiveStrengthTest` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/EmissiveStrengthTest) |
| Unlit | `UnlitTest` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/UnlitTest) |
| Diffuse Transmission（候选扩展） | `DiffuseTransmissionTeacup` | [模型说明](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DiffuseTransmissionTeacup) |

- 每份源模型导入为一个 MeshAsset，保留 submesh 的材质关联；最长轴统一缩放为 3 米。
- 参数与扩展贴图引用存入 PbrMaterialAsset；纹理按属性要求区分 Linear / sRGB，并生成 mipmap。
- Clearcoat 与 Sheen 已接入渲染；其余扩展目前仅导入参数和纹理引用。
- Volume 的 attenuationDistance 为 null 时表示无限衰减距离，而不是将无穷大写入 JSON。
- 本轮没有修改默认场景、曝光或其他全局渲染设置。
- 当前纹理绑定只支持 TEXCOORD_0，扩展贴图的 KHR_texture_transform 暂不支持，导入时明确检查。
- Subsurface 尚无稳定定义，本轮没有自行定义或导入其草案材质。

本轮导入与验证使用 `test/ImportMaterialSamples.cpp` 和 `test/material_samples.json`，不属于应用启动流程。
