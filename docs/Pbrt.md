# PBRT-v4 资产导入

[资产模块](Asset.md) · [上游版本与修改记录](../src/asset/pbrt/upstream/UPSTREAM.md)

导入经过“官方读取前端 → RenderLab 资产转换 → 自包含持久化”。读取目标独立编译，使用双精度和串行解析；Tokenizer、参数语法、Include / Import 与场景状态处理取自固定版本的 pbrt-v4。

入口为 `AssetImporter::importPbrtScene`，输入源路径和可选 ID，返回 `PbrtSceneAsset::ID`。CLI 示例：

```text
RenderLab --import-pbrt-scene <source>
```

## 保存内容

`PbrtSceneAsset` 保存相机、Film、Sampler、Integrator、PixelFilter、Accelerator、Option，以及形状、光源、介质、对象定义和实例。变换保留双精度完整矩阵、运动端点与时间区间，并保存 PBRT 的渲染坐标空间和 renderFromWorld。

`PbrtMaterial` 沿用 MaterialAsset 的 ID 与多态存储；`PbrtTextureAsset` 保存 Float / Spectrum 纹理节点和映射变换。实体保存有效色彩空间，模型参数保留原始声明类型、值、各自的色彩空间和源位置。纹理及混合材质引用解析为资产 ID，介质和实例引用解析为场景内索引。省略参数的默认语义由资产中的 readerRevision 指定。

几何与纹理以数据描述保存：包括解析曲面、网格属性、光谱、程序纹理、Ptex 和环境图投影参数。面积光通过形状记录绑定。后续离线渲染可据此构建计算资源。

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

## 验证

`PbrtImportTests` 覆盖读取状态、压缩文件、属性继承、材质和纹理引用、运动矩阵、光谱与大型数组往返，并比较依赖原始字节。搬运测试删除源目录，将资产目录移动后重新初始化 AssetManager，检查场景与依赖重载。
