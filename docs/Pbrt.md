# PBRT-v4 资产导入

[资产模块](Asset.md) · [上游版本与修改记录](../src/asset/pbrt/upstream/UPSTREAM.md)

[资产转换](PbrtConversion.md) · [材质](pbrt/材质.md) · [光源](pbrt/光源.md)

导入经过“官方读取前端 → RenderLab 资产转换 → 自包含持久化”。读取目标独立编译，使用双精度和串行解析；Tokenizer、参数语法、Include / Import 与场景状态处理取自固定版本的 pbrt-v4。

入口为 `AssetImporter::importPbrtScene`，输入源路径和可选 ID，返回 `PbrtSceneAsset::ID`。CLI 示例：

```text
RenderLab --import-pbrt-scene <source>
```

## 保存内容

导入同时生成 `SceneAsset`，其 ID 记录在 `PbrtSceneAsset.scene`。内部场景标记为 `Offline`，载入后自动使用离线路线；实时场景的路线选择包含 Forward、Deferred 和实时光追。

当前转换透视相机的位置、yaw/pitch 和垂直 FOV，保存镜头半径与对焦距离；景深暂未参与渲染。PBRT 世界坐标转换时翻转 Z 轴，移动相机采用起始姿态，原始运动数据继续保存在描述资产中。源画幅比例仅用于 FOV 换算，显示比例使用引擎窗口，取景范围使用引擎投影。

形状和对象实例在导入时生成 `AStaticMesh`，同一对象的实例共享 MeshAsset。当前采用起始变换，交给 `TransformComponent` 分解为 TRS 与 base，运动端点继续保存在描述资产中。

面积光使用静态网格上的 `MeshAreaLightComponent` 绑定子网格，保存双面发光与朝向标记；点光、聚光、方向光使用现有组件，另有环境光、投影光和配光组件。`PbrtLightEmission` 保存光谱、scale、power / illuminance 和包内图像引用。环境图保留 PBRT 等面积映射，portal 顶点转换到引擎世界坐标。

场景提取将离线光源写入独立的 `OfflineLightData`，面积光引用已有 DrawItem，使用完整的 TRS × base。当前离线渲染默认追踪两次散射。point / spot、distant、area 分别使用蓄水池，area 与 BRDF 采样做 MIS；路径未命中时显示环境光。每个样本始终进行像素内抖动，并做渐进累积，详见 [光源](pbrt/光源.md)。

`PbrtSceneAsset` 保存相机、Film、Sampler、Integrator、PixelFilter、Accelerator、Option，以及形状、光源、介质、对象定义和实例。变换保留双精度完整矩阵、运动端点与时间区间，并保存 PBRT 的渲染坐标空间和 renderFromWorld。

材质资产、GPU 准备及各类散射实现见 [材质](pbrt/材质.md)。

`PbrtTextureAsset` 保存结构化的 Float / Spectrum 纹理节点和静态映射矩阵。图像转换为 TextureAsset，节点保留编码与过滤参数。纹理、混合材质和介质关联解析为资产 ID，实例引用对象定义。原始来源保存在打包源文件中。

`trianglemesh` 和 `plymesh` 在导入时转换为现有 `MeshGeometry`，通过 `MeshAsset` 和 geometry 二进制文件保存；形状的 `mesh` 引用对应资产。sphere、cylinder、disk 转成原生内置几何参数，保存尺寸、裁剪范围与细分参数，读取时由通用内置生成器生成 `MeshGeometry`。网格保持形状的局部坐标，世界坐标转换在实例落入内部场景时处理。

原始几何、纹理与光谱描述继续保存，包含程序纹理、Ptex 和环境图投影参数。面积光通过形状记录绑定。

## 文件布局

```text
assets/
  PbrtScene/<场景ID>.json
  Material/<场景ID>_material<N>.json
  PbrtTexture/<场景ID>_texture<N>.json
  binary/pbrt/<场景ID>/
    files/       入口、Include、Import 与全部原始输入文件
    arrays.bin   超过 64 KiB 的数值数组
```

输入文件按模型及参数用途收集，覆盖图像、PLY、光谱、Ptex、测量 BRDF、镜头、体数据与参考图。相对依赖统一相对入口场景目录解析；Film 文件名和参考误差输出文件名保留为输出配置。

数组记录标量类型、元素数量和字节范围，`readPbrtArray` 按范围读取。原始文件按字节复制；源位置指向包内源文件。重载通过 AssetManager 读取转换后的描述，二进制路径相对资产根目录，因此整个资产目录可以一起搬运。
