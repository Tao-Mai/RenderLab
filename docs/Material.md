# 实时材质工作流

前向、延迟和实时光追共用材质 SSBO、纹理数组、材质求值与基础 BRDF。资产保存来源参数，Shader 在采样后将 Metallic/Roughness（MR）和 Specular/Glossiness（SG）转换为统一反射参数。

## 资产与采样

`GltfMaterial.workflow` 标记工作流。MR 使用 Base Color、Metallic、Roughness，并支持 Specular 和 IOR 扩展；SG 使用 `specularGlossiness` 中的 diffuse、specular、glossiness 系数及贴图。场景材质覆盖中的空工作流表示继承，合并后的材质具有明确工作流。

glTF 导入器读取 `KHR_materials_pbrSpecularGlossiness` 时直接保存 SG 参数。SG 扩展中的颜色覆盖来源文件的 MR 参数。现有 Bistro 资产采用 MR + Specular/IOR，也进入同一套 BRDF。

[材质求值](../shaders/common/material_evaluation.slang)通过 Slang 泛型采样器接收纹理：光栅使用梯度采样，光追使用显式 LOD。SG 贴图的 RGB 按 sRGB 解码，Alpha 作为线性 glossiness，一次采样得到两者。MR 的 Metallic / Roughness 使用同一纹理和采样器时共用采样结果。来源工作流在属性栏只读显示，对应参数沿用材质覆盖与重置流程。

## 统一 BRDF

[PbrBrdf](../shaders/common/pbr_brdf.slang)只保存数值，负责求值、方向采样与 PDF。

| 参数 | 含义 |
| --- | --- |
| diffuseColor | 法线入射时已完成镜面能量分配的漫反射颜色 |
| specularF0 | 法线入射镜面反射率，RGB |
| specularF90 | 掠射角镜面反射率 |
| roughness | 感知粗糙度，GGX 使用其平方 |
| diffuseFresnelWeight | 漫反射随角度变化的衰减权重 |

MR 先计算非金属层 `Fd0 = min(Fior × specularColor, 1) × specularWeight`。`Fior = ((ior−1)/(ior+1))²`，默认 IOR 为 1.5；IOR 为 0 时按无限 IOR 计算 `Fior = 1`。Specular 颜色来自 RGB，强度来自 Alpha。颜色限制在乘强度之前完成。

设 MR 颜色为 C、金属度为 m、`d = max(Fd0)`；SG 采样后的颜色、镜面反射率及光泽度为 D、S、g：

| 参数 | MR | SG |
| --- | --- | --- |
| diffuseColor | `C × (1−m) × (1−d)` | `D × (1−max(S))` |
| specularF0 | `lerp(Fd0, C, m)` | S |
| specularF90 | `lerp(specularWeight, 1, m)` | 1 |
| roughness | MR 粗糙度采样结果 | `1−g` |
| diffuseFresnelWeight | `(specularWeight−d)/(1−d)`，d=1 时为 0 | 0 |

BRDF 使用 `t = (1−abs(V·H))⁵`：漫反射为 `diffuseColor × (1−diffuseFresnelWeight×t) / π`，镜面反射为 `lerp(F0,F90,t) × GGX分布 × Smith可见性`。光追采样根据相同的漫反射和镜面权重选择瓣，返回完整混合 PDF，并补偿选择概率。低于表面的 GGX 样本作为零贡献。

## 渲染路线

前向和实时光追直接使用求值结果。延迟路线通过五张 G-buffer 传递统一参数：

| 附件 | RGB | Alpha | 格式 |
| --- | --- | --- | --- |
| 0 | diffuseColor | AO | RGBA8 sRGB |
| 1 | normal | roughness | RGBA16F |
| 2 | emission | specularF90 | RGBA16F |
| 3 | specularF0 | diffuseFresnelWeight | RGBA16F |
| 4 | sheenColor | sheenRoughness | RGBA16F |

打包与具名访问集中在 [GBufferData](../shaders/raster/gbuffer_data.slang)。相较原布局增加 8 字节/像素。基础材质在三条路线共用；Clearcoat 沿用 Forward 的涂层计算，Sheen 沿用 Forward / Deferred 的计算。

IBL 使用现有 Split Sum LUT，镜面项为 `F0×A + F90×B`，漫反射以 `N·V` 代入统一漫反射函数近似。SVGF 解调反射率为 `saturate(diffuseColor + F0)`，重投影和最终合成都使用同一反射率下限。

PBRT 离线材质由独立的材质描述与 Shader 求值。
