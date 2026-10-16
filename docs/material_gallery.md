# 材质球展示场景

在场景下拉框选择 `material_gallery`。16 个展示物按下面的顺序排成 4×4，点击后在右侧 Materials 中调整参数。

| | 第 1 列 | 第 2 列 | 第 3 列 | 第 4 列 |
|---|---|---|---|---|
| 第 1 行 | 基础 PBR 非金属 | 基础 PBR 金属 | Clearcoat | 各向异性 GGX |
| 第 2 行 | Sheen | Transmission | Volume | IOR |
| 第 3 行 | Specular | Iridescence | Dispersion | Emissive Strength |
| 第 4 行 | Unlit | Diffuse Transmission | Alpha Blend | Alpha Mask |

- 除 Sheen 使用褶皱布料外，其余使用内置球，均支持逐物体材质覆盖；材质排列、参数、灯光与相机只在场景资产中定义，运行时代码不按场景 ID 分支。
- 所有展示物的 Base Color RGB 统一为线性灰色 `(0.5, 0.5, 0.5)`，透明模式只保留 Alpha 差异，避免底色干扰材质比较。
- `05 Sheen Cloth` 使用官方 [SheenCloth](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/SheenCloth) 的静态褶皱网格，缩放至约 2.2 单位并朝向相机，开启双面材质。只使用几何体，不使用原资产依赖纹理变换的织物贴图；保留 gallery 的灰色底色与 Sheen 参数。
- 场景包含点光源和方向光，IBL 关闭时也可观察基础光照；环境图使用已有的 `newport_loft`。
- 推荐使用 Forward 查看已有 PBR / Clearcoat 效果；不自动改变全局渲染路线、IBL 或曝光。
- Sheen 已接入 Forward / Deferred，可调整颜色与独立粗糙度观察变化。
- 尚未实现的扩展在属性栏标记为 `Data only - shading not implemented`，当前仍按基础材质渲染，调节这些参数暂时不改变画面。
- 各向异性旋转以角度编辑、弧度保存；强度和比例限制在 0～1，薄膜厚度使用 nm，Volume 厚度与衰减距离使用 m。
- Volume 可关闭有限衰减距离，保存为 null；不使用无穷浮点值。
- Alpha Mask 球无透明度纹理，调整 Base Color 的 Alpha 和 Alpha Cutoff 可检查整球的遮罩判定。
- 材质参数仍走现有 override 与场景保存流程；修改后手动保存场景，切换场景会丢弃未保存修改。

场景资产：[material_gallery.json](../assets/Scene/material_gallery.json)。
