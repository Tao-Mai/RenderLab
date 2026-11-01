# PBRT 资产转换与离线渐进渲染

导入阶段通过固定版本的 PBRT 前端读取场景，转换为 RenderLab 资产，并生成标记为 Offline 的 SceneAsset。运行时读取转换结果，准备独立的离线材质与纹理指令，再进行渐进式 diffuse 路径追踪。

## 数据组织

| 数据 | 内部表示 |
| --- | --- |
| 场景与实例 | SceneAsset、AStaticMesh、共享 MeshAsset，变换保存为 TRS × base |
| 材质 | PbrtMaterial 按模型保存参数，标量与光谱输入通过 ID 引用纹理节点 |
| 纹理函数 | PbrtTextureAsset 保存模型 variant、输入引用和坐标映射 |
| 图像 | TextureAsset 保存线性 RGBA32F 与 mip，SamplerAsset 保存基础采样设置 |
| 光源 | 解析光源组件、环境光组件，以及绑定子网格的 MeshAreaLightComponent |
| 表面 | RenderComponent.offlineSurface 保存 alpha、朝向、内外介质和逐面索引 |
| 介质 | PbrtMediumAsset 保存均匀、标量网格、RGB 网格、云和 NanoVDB 模型 |

PbrtSceneAsset 保存场景设置、来源和几何描述，引用转换后的材质、纹理、介质及内部场景。相机、形状和实例采用起始变换。几何转换覆盖 trianglemesh、PLY、sphere、cylinder 和 disk；三角网格保存为 MeshGeometry，内置形状保存生成参数。PLY 四边形三角化和退化三角形移除时同步维护来源面的索引。

## 纹理转换

纹理模型包含 constant、imagemap、scale、mix、directionmix、bilerp、checkerboard、dots、fbm、wrinkled、windy、marble 和 ptex。导入时落实固定版本的默认参数，解析 Float / Spectrum 引用，检查输入类型与引用循环。

坐标映射包含 UV 缩放偏移、球面、柱面、平面和三维空间映射，矩阵转换到引擎世界坐标。图像保持解码后的行方向，纹理求值时统一处理 PBRT 的 V 方向。

imagemap 按 encoding 解码为线性浮点图像，支持 linear、sRGB 和显式 gamma，保留 HDR 数值与 RGB 色度坐标。EXR 读取 chromaticities 元数据；图像入口支持灰度 EXR、16 位整数图像和 PFM。相同文件与解码参数共享像素资产，各节点分别保存映射、wrap、filter、maxAnisotropy、scale 和 invert。

法线图、环境图和面积光图像共用图像导入入口。环境光保留 PBRT 等面积投影。Ptex 保存包内文件、编码和缩放参数，表面保存逐面索引。

## 保存与重载

元数据进入 Scene、Mesh、Material、Texture、Sampler、PbrtTexture、PbrtMedium 和 PbrtScene 目录。图像与几何使用现有二进制存储；原始依赖和超过 64 KiB 的数组进入 binary/pbrt/&lt;场景ID&gt;，逐面索引始终保存为实例共享的二进制块。文件引用相对资产根目录，重载通过资产 ID 和包内路径访问资源。

导入顺序为读取、解析引用与依赖、转换资产和准备图像，最后保存依赖、子资产及场景。入口为：

```text
RenderLab --import-pbrt-scene <source>
```

## 离线路径

Renderer 读取 PbrtMaterial，复用网格、图像、采样器和全局图像描述符。OfflineScene 分别准备纹理表达式与材质预览数据。纹理编译为后缀指令，由 offline_texture 求值；金属、次表面、毛发等颜色近似由 offline_material 处理，混合材质按子材质在前的顺序求值。纹理与材质混合各使用 64 项值栈，指令、映射类型与栈容量由 CPU 和 shader 共享定义。当前全局图像表上限为 128，采样器表上限为 32。材质与纹理表在内容变化时重新准备与上传。

shader 每个样本循环追踪多次反弹，统一使用 diffuse BRDF，材质 RGB 求值结果作为反照率。point / spot、distant 和 area 分别使用蓄水池，area 与 BRDF 采样执行 MIS；路径未命中时读取环境光。独立反弹配置默认 2 次，修改时重新累积。网格实例共享 BLAS，TLAS 保持每帧完整构建。每个样本始终使用 Sobol 序列进行像素内抖动；每帧样本做算术平均，修改相机、几何、材质或光源时重新累积。各类 PBRT 光源已进入按类型划分的离线 GPU 数组，发光几何体按三角形展开，详见 [光源](pbrt/光源.md)。

| 内容 | 当前预览处理 |
| --- | --- |
| RGB 光谱 | 直接使用 RGB 数值，色域信息保存在资产中 |
| 采样光谱、黑体 | 在 610、550、460 nm 取样或计算相对值 |
| 命名光谱、外部光谱文件 | 中性白色占位 |
| 程序纹理 | 简化图案和噪声，噪声最多执行 12 个 octave |
| Ptex | 逐三角形检查格占位，完整资源保存在资产中 |
| 实测 BRDF、具名散射 | 中性表面颜色占位 |
| 次表面、毛发 | 从现有参数生成表面颜色近似 |
| 光源 | point / spot / distant / area 参与直接光，环境光通过路径未命中求值，projection / goniometric 继续保存并上传 |
| 介质、alpha、法线图 | 保存资产数据，待后续渲染接入 |
| 过滤 | 从基础 mip 求值，完整过滤参数继续保存 |

当前路径使用材质颜色近似进行 diffuse 光照。后续可逐步接入完整 PBRT 材质散射、精确光谱求值、纹理足迹、Ptex 解码和体积传播。

优化点包括纹理图公共子表达式复用、表的增量更新，以及图像存储精度和过滤实现。
