# Sheen 材质

按 [KHR_materials_sheen](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_materials_sheen/README.md) 接入光栅路线。实时光追和离线路线仍使用各自已有材质模型，本轮未扩展它们。

## 材质与分层

- Sheen Color 使用线性颜色系数乘 sRGB 颜色贴图；Sheen Roughness 使用独立系数乘线性粗糙度贴图的 Alpha 通道。
- 缺失贴图绑定现有白色 fallback，shader 不判断贴图是否存在。颜色为零时，Sheen 不改变底层材质。
- Charlie 分布和配套的 fitted visibility 都使用 `alpha = sheenRoughness²`，不套用 GGX 的最低粗糙度，也不额外乘 Fresnel。
- 直接光的底层 BRDF 乘 `1 - max(sheenColor) × max(E(NdotV), E(NdotL))`，再加上 Sheen BRDF；方向反照率来自已有官方 LUT。
- 分层顺序为基础 PBR → Sheen → Clearcoat。保留现有 Clearcoat 行为；延迟路线仍不处理 Clearcoat。
- 自发光保留现有处理方式，AO 仅作用于环境光。

代码：[BRDF](../shaders/brdf.slang)、[贴图采样](../shaders/mesh_surface.slang)、[光照分层](../shaders/scene_lighting.slang)。

## 环境光

- 使用现有 radiance cubemap 对 Charlie BRDF 做 64 点、余弦加权半球数值积分，`NdotL / PDF` 抵消为 PI。不会把 GGX 预过滤结果当作 Charlie 的预过滤结果，也不把方向反照率 LUT 当成环境光颜色。
- 环境底层使用 `1 - max(sheenColor) × E(NdotV)` 缩放。关闭 IBL 后沿用原有环境系数，只缩放底层，不采样环境图。
- 当前方法是有限采样的参考实现，强高频 HDR、低粗糙度或掠射角下可能有积分误差，开销也高于预过滤方案；尚未增加 Charlie 预过滤 cubemap、IBL LUT 或时域滤波。

## GPU 数据与编辑器

- Material UBO 增加 Sheen Color / Roughness；材质 set 1 增加两组贴图与 sampler。
- Scene set 0 绑定内置 `BuiltinAssets::Texture::kSheenLut` 和 `linearClamp` sampler，不通过渲染配置选择。
- 前向场景 shader 按 Sheen 有无编译分支；IBL、Clearcoat 与 Sheen 的组合均参与变体选择。
- G-buffer 增加线性 RGBA16F attachment，保存 Sheen RGB 和 Roughness；延迟光照复用同一套 BRDF 与环境积分。
- 编辑器现有 Sheen 颜色和粗糙度控件立即更新 GPU；`material_gallery` 中的 `05 Sheen Cloth` 使用静态褶皱布料观察着色变化。仍按现有流程手动保存场景。

LUT 来源、格式和坐标约定见 [sheen_lut.md](sheen_lut.md)。

## 验证

- C++ / Slang Material UBO 布局和新增 descriptor 绑定一致。
- GPU 验证：零 Sheen 与基础材质一致；Forward / Deferred 在 IBL 开关下匹配；粗糙度 0/1 无非有限值。
