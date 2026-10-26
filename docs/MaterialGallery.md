# 材质展示场景

[项目首页](../README.md) · [示例资产](MaterialSamples.md) · [Sheen](Sheen.md)

选择 `material_gallery`，可在同一光照下比较材质。16 个展示物按 4 × 4 排列，点击对象后在右侧 Materials 调节参数。

## 排列与状态

| | 第 1 列 | 第 2 列 | 第 3 列 | 第 4 列 |
| --- | --- | --- | --- | --- |
| 第 1 行 | PBR 非金属 | PBR 金属 | Clearcoat | Anisotropy * |
| 第 2 行 | Sheen 布料 | Transmission * | Volume * | IOR * |
| 第 3 行 | Specular * | Iridescence * | Dispersion * | Emissive Strength |
| 第 4 行 | Unlit * | Diffuse Transmission * | Alpha Blend | Alpha Mask |

带 * 的扩展支持数据、导入与编辑，画面使用基础 PBR 着色；属性栏显示 `Data only - shading not implemented`。

- 基础 PBR、Normal、AO、自发光已实现。
- Clearcoat 使用 Forward；Sheen 支持 Forward / Deferred。
- Alpha Blend / Mask 用于光栅透明混合与遮罩。

## 如何比较

所有展示物的 Base Color 统一为线性灰色 `(0.5, 0.5, 0.5)`，避免底色掩盖材质差异。除 Sheen 外使用内置球体；Sheen 使用静态褶皱布料，更容易观察掠射反射。

- 改变 Metallic / Roughness：比较高光颜色、宽度及漫反射变化。
- 改变 Clearcoat：观察独立涂层高光与底层材质的叠加。
- 改变 Sheen Color / Roughness：观察褶皱与掠射区域；黑色对应零贡献。
- 调整 Alpha 与 Cutoff：检查混合和遮罩判定。Mask 球使用统一 Alpha，可能整球通过或被丢弃。

场景使用点光源、方向光与 Newport Loft 环境图，建议先用 Forward 比较已实现的材质效果。

## 几何与保存

Sheen 网格采用 [Khronos SheenCloth](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenCloth) 的静态褶皱几何，约 2.2 m，开启双面。

材质参数使用 Override 流程，需显式保存场景；切换场景会丢弃未保存修改。排列、参数和相机均在场景资产中定义。资产准备说明见 [assets/README.md](../assets/README.md)。
