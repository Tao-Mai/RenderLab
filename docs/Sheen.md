# Sheen：绒布反射与材质分层

[材质展示](MaterialGallery.md) · [方向反照率 LUT](SheenLut.md)

Forward / Deferred 共用 glTF Sheen 着色函数，使用 Charlie BRDF 表现布料等材质的掠射反射，并衰减底层能量。

## 材质输入

- Sheen Color：线性颜色系数 × sRGB 颜色贴图。
- Sheen Roughness：粗糙度系数 × Linear 贴图的 Alpha。
- 缺失贴图绑定白色 fallback。
- Sheen 颜色同时控制反射强度与底层衰减，黑色对应零贡献。

## BRDF 与能量分层

Sheen BRDF 由 Charlie 分布与配套 Visibility 构成，使用 `alpha = sheenRoughness²`。材质叠层顺序为：

```text
基础 Metallic-Roughness → Sheen → Clearcoat
```

使用方向反照率 E 近似补偿底层能量：

- 直接光缩放：`1 - max(sheenColor) × max(E(N·V), E(N·L))`。
- 环境光缩放：`1 - max(sheenColor) × E(N·V)`。

方向反照率来自内置 LUT。Clearcoat 在 Forward 计算，AO 作用于环境项。

代码：[brdf.slang](../shaders/brdf.slang)、[mesh_surface.slang](../shaders/mesh_surface.slang)、[scene_lighting.slang](../shaders/scene_lighting.slang)。

## IBL 的处理

Sheen 对原始 Radiance Cubemap 进行 **64 点余弦加权半球积分**，再与经过能量缩放的基础 IBL 叠加。

有限采样在高频 HDR、低粗糙度和掠射角处可能产生积分误差，成本高于预过滤查询。方向反照率 LUT 用于底层能量缩放，环境颜色由半球积分得到。

关闭 IBL 时使用可调环境系数。

## 渲染与观察

- Forward 按 Sheen / Clearcoat / IBL 组合选择 Shader 变体。
- Deferred 通过额外 G-buffer 保存 Sheen RGB 与 Roughness，光照阶段复用同一实现。
- 编辑器可调颜色与粗糙度；`material_gallery` 使用静态褶皱布料观察掠射区域。

规范来源：[KHR_materials_sheen](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_sheen/README.md)。
