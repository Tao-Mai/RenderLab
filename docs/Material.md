# StandardMaterial

材质资产、GPU 材质表和 Shader 统一使用 Falcor StandardMaterial 的表面模型。实现对应 `StandardMaterial.slang`、`StandardMaterialInstance.slang` 和 `BSDFs/StandardBSDF.slang`，采用参考代码默认的 Frostbite diffuse、相关 Smith GGX 和球冠 VNDF 采样。

## 资产与纹理槽

[StandardMaterial](../src/asset/StandardMaterial.h) 保存在 `assets/Material/`，子网格通过 `StandardMaterial::ID` 引用。所有持久化字段必须存在；未绑定的纹理槽写为 `null`。旧材质结构不再参与读取。

| 字段 | 含义 |
| --- | --- |
| shadingModel | `MetalRough` 或 `SpecGloss` |
| baseColor | MR 底色或 SG 漫反射颜色，Alpha 为不透明度 |
| specularParams | MR：G 为感知粗糙度、B 为金属度；SG：RGB 为镜面颜色、A 为光泽度 |
| indexOfRefraction | 材质折射率，默认 1.5 |
| transmissionColor | 透射颜色 |
| diffuseTransmission / specularTransmission | 漫透射与镜面透射的混合权重 |
| thinSurface | 薄表面直接使用透射颜色；厚表面每次交点使用其平方根 |
| emissiveColor / emissiveFactor | 发光颜色与独立强度；仅正面发光 |
| doubleSided | 是否镜像背面的着色法线；透射材质自动启用双面求值 |
| alphaMode / alphaCutoff | `Opaque` 或 `Mask` 与遮罩阈值 |
| normalScale | 法线贴图的切线空间 XY 缩放 |

五个纹理槽为 `baseColorTexture`、`specularTexture`、`emissiveTexture`、`normalTexture`、`transmissionTexture`。与 Falcor 一样，绑定的纹理替代对应常量，发光纹理的结果仍乘 `emissiveFactor`。Shader 以显式 LOD 采样，BC5 法线贴图重建 Z。CPU/GPU 共用的参数编码通过 Slang property 具名访问。

[材质表](../src/render/resources/MaterialTable.cpp)只上传已校验的 StandardMaterial 数据并分配纹理、采样器索引。[GPU 布局](../src/render/resources/StandardMaterialData.h)与[Shader 布局](../src/render/impl/Scene/Material/StandardMaterialData.slang)保持一致。

## 表面求值与散射

[StandardMaterial](../src/render/impl/Rendering/Materials/StandardMaterial.slang)采样纹理并构造实例。MR 使用 `diffuse = baseColor × (1−metallic)`、`specular = lerp(F0, baseColor, metallic)`，其中 `F0 = ((ior−1)/(ior+1))²`。SG 直接使用 diffuse、specular 和 `roughness = 1−glossiness`，金属度按 Falcor 的 `getMetallic()` 估计。两种模型都不预先扣除旧 glTF BRDF 的 Fresnel 能量。

[StandardBSDF](../src/render/impl/Rendering/Materials/BSDFs/StandardBSDF.slang)包含漫反射、漫透射、GGX 反射、GGX 透射及理想反射、理想透射六个瓣。`alpha = roughness²`；当 `alpha < 0.0064`，镜面瓣切换为 delta。粗糙度为零时不会钳制成有限宽高光。

`eval()` 包含出射方向的余弦。采样遵循 Falcor 的瓣权重与选中瓣吞吐量，另返回完整连续混合 PDF；delta 事件通过显式标记继续传播，并以权重 1 处理发光命中 MIS。透射采用精确介电 Fresnel、Snell 折射、全反射和辐射传输的 eta² 权重。当前交界面外侧折射率为 1，不维护嵌套介质栈，也不积分体散射或体吸收。

[StandardMaterialInstance](../src/render/impl/Rendering/Materials/StandardMaterialInstance.slang)负责世界空间与局部着色坐标变换。Surface 检查几何侧与着色侧的一致性，并将透射射线起点偏移到表面另一侧。Minimal 和 ReSTIR 共用这套材质求值。

## 导入

- glTF MR 直接导入；`KHR_materials_pbrSpecularGlossiness` 直接写入 SG 参数，覆盖来源文件的 MR 参数。
- glTF 的颜色、金属度、粗糙度、SG 与发光乘法系数在导入时烘焙进纹理；没有纹理时使用常量。源颜色纹理先按 sRGB 解码，Alpha 保持线性，生成结果采用线性 RGBA16F 并重新生成 Mipmap。单位系数复用原纹理，包括 DDS 压缩纹理。
- `KHR_materials_specular` 转换为 SG 的漫反射、镜面反射和光泽度纹理。参与合成的纹理须使用同一采样器。
- IOR、发光强度、常量镜面透射和常量漫透射映射到 StandardMaterial。Volume 的厚度决定薄表面标志。
- Assimp 按 Falcor 的来源格式约定导入：FBX 等默认使用 MR，Specular 贴图按线性 RGB 打包参数读取；OBJ 使用 SG，将 Phong 指数转换为光泽度。纹理直接替代常量，不烘焙 FBX/OBJ 颜色系数。外部 DDS 保留 BC 压缩块与 Mipmap。材质名中的 `.DoubleSided` 标记、Alpha 遮罩、折射率及不透明度对应的镜面透射同步导入。
- glTF `BLEND` 按 alpha test 导入并给出日志。透射混合遮罩、体吸收、厚度贴图以及 Clearcoat、Sheen、Anisotropy、Iridescence、Dispersion、Unlit 不在当前 StandardMaterial 表面实现中，导入时明确记录限制，不保存无着色用途的扩展字段。
