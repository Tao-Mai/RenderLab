# Sheen：绒布反射与材质分层

[材质展示](material_gallery.md) · [方向反照率 LUT](sheen_lut.md)

光栅路线实现 glTF Sheen：使用 Charlie BRDF 表现布料等材质的掠射反射，并衰减底层能量。Forward / Deferred 共用着色函数；实时 / 离线光追暂未加入该扩展。

## 材质输入

- Sheen Color：线性颜色系数 × sRGB 颜色贴图。
- Sheen Roughness：粗糙度系数 × Linear 贴图的 Alpha。
- 缺失贴图绑定白色 fallback，不在 Shader 判断纹理有无。
- Sheen 颜色为黑时贡献与底层衰减均为零，不需要独立的 Strength 参数。

## BRDF 与能量分层

Charlie 分布与配套 Visibility 使用 `alpha = sheenRoughness²`，不额外乘 Fresnel。材质叠层顺序为：

```text
基础 Metallic-Roughness → Sheen → Clearcoat
```

使用方向反照率 E 近似补偿底层能量：

- 直接光缩放：`1 - max(sheenColor) × max(E(N·V), E(N·L))`。
- 环境光缩放：`1 - max(sheenColor) × E(N·V)`。

方向反照率来自内置 LUT，而不是简单根据颜色亮度遮挡底层。Clearcoat 当前仍只在 Forward 计算；AO 仅作用于环境项。

代码：[brdf.slang](../shaders/brdf.slang)、[mesh_surface.slang](../shaders/mesh_surface.slang)、[scene_lighting.slang](../shaders/scene_lighting.slang)。

## IBL 的处理

GGX 预过滤环境不能直接用于 Charlie 分布。当前 Sheen 对原始 Radiance Cubemap 进行 **64 点余弦加权半球积分**，再与经过能量缩放的基础 IBL 叠加。

这是便于比较的有限采样实现，不是专用 Charlie 预过滤方案；高频 HDR、低粗糙度和掠射角可能出现积分误差，成本也高于预过滤查询。方向反照率 LUT 只负责能量缩放，不提供彩色环境光积分结果。

关闭 IBL 时沿用可调环境系数，不采样环境图。当前没有 Sheen 专用环境预过滤或时域滤波。

## 渲染与观察

- Forward 按 Sheen / Clearcoat / IBL 组合选择 Shader 变体。
- Deferred 通过额外 G-buffer 保存 Sheen RGB 与 Roughness，光照阶段复用同一实现。
- 编辑器可调颜色与粗糙度；`material_gallery` 使用静态褶皱布料观察掠射区域，不进行物理模拟。

规范来源：[KHR_materials_sheen](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_sheen/README.md)。
