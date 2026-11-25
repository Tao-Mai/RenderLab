# 材质展示场景

[项目首页](../README.md) · [示例资产](MaterialSamples.md)

以下是一个 4 × 4 的材质对比排列示例。

## 排列与状态

| | 第 1 列 | 第 2 列 | 第 3 列 | 第 4 列 |
| --- | --- | --- | --- | --- |
| 第 1 行 | PBR 非金属 | PBR 金属 | Clearcoat | Anisotropy * |
| 第 2 行 | Sheen 布料 | Transmission * | Volume * | IOR |
| 第 3 行 | Specular | Iridescence * | Dispersion * | Emissive Strength |
| 第 4 行 | Unlit * | Diffuse Transmission * | Alpha Blend | Alpha Mask |

Clearcoat、Sheen 和带 * 的扩展可从模型导入并保存到材质资产。

- 基础 PBR、法线映射和自发光参与着色。
- MR / SG 的统一参数、Specular 与 IOR 处理见[材质工作流](Material.md)。

## 如何比较

所有展示物的 Base Color 统一为线性灰色 `(0.5, 0.5, 0.5)`，避免底色掩盖材质差异。除 Sheen 外使用内置球体；Sheen 使用静态褶皱布料，更容易观察掠射反射。

- 比较绑定了不同 Metallic / Roughness 材质的模型，观察高光颜色、宽度和漫反射。
- 比较采用不同 Alpha 与 Cutoff 的模型，观察遮罩判定。Mask 球使用统一 Alpha，可能整球通过或被丢弃。


## 几何与保存

Sheen 网格采用 [Khronos SheenCloth](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenCloth) 的静态褶皱几何，约 2.2 m，开启双面。
