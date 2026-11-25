# 材质工作流

MinimalPathTracer 使用材质缓冲、纹理数组和 glTF BRDF。资产保存来源参数，Shader 在采样后将 Metallic/Roughness（MR）和 Specular/Glossiness（SG）转换为统一反射参数。

## 资产与采样

`GltfMaterial.workflow` 是必填字段。MR 使用 Base Color、Metallic、Roughness，并支持 Specular 和 IOR 扩展。SG 使用 `specularGlossiness` 中的 diffuse、specular、glossiness 系数及贴图。

glTF 导入器读取 `KHR_materials_pbrSpecularGlossiness` 时直接保存 SG 参数。SG 扩展中的颜色覆盖来源文件的 MR 参数。现有 Bistro 资产采用 MR + Specular/IOR，也进入同一套 BRDF。

[材质求值](../src/render/impl/material/material_evaluation.slang)使用显式 LOD 采样纹理。SG 贴图的 RGB 按 sRGB 解码，Alpha 作为线性 glossiness，一次采样得到两者。MR 的 Metallic / Roughness 使用同一纹理和采样器时共用采样结果。

## 统一 BRDF

[PbrBrdf](../src/render/impl/material/pbr_brdf.slang)只保存数值，负责求值、方向采样与 PDF。

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

## 材质存储

子网格通过 `GltfMaterial::ID` 引用材质。资产管理器保存和加载材质字段，材质表按资产 ID 上传 GPU 数据。 [材质资产](../src/asset/GltfMaterial.h) · [材质表](../src/render/resources/MaterialTable.cpp)
