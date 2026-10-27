# 资产与运行数据

[项目首页](../README.md) · [资产模块](../docs/Asset.md) · [材质示例](../docs/MaterialSamples.md)

资产描述、导入后的二进制与源模型分开保存，运行时读取导入后的资产。

## 目录约定

```text
assets/
  Mesh/ Material/ Texture/ Shader/ Scene/ EnvironmentMap/ Sampler/
  PbrtScene/ PbrtTexture/
  binary/
    geometry/     网格顶点与索引
    texture/      像素、Cubemap 与 Mipmap
    pbrt/         按场景打包的原始依赖与大型参数数组
resource/         模型、纹理与来源许可
```

- JSON 目录由各资产类型显式声明；同一类型内 ID 唯一。
- 资产引用使用类型化 ID，JSON 形式为 `{"value": "..."}`。
- 网格保存局部 AABB 与 Submesh 的材质引用；几何数据独立存储。
- TextureBinding 同时引用 Texture 和 Sampler。
- 几何、fallback 纹理、Sampler、BRDF / Sheen LUT 通过内置资产 ID 获取。

## 导入

编辑器提供模型预览、尺寸 / 缩放与 ID 调整；确认时才查重并自动追加序号。完整场景导入将模型节点转换为静态物体，保留实例变换、共享网格和子网格材质，并设置编辑器相机。

底层统一使用 AssetImporter：glTF / GLB 由 TinyGLTF 解析，其余受支持模型格式由 Assimp 解析，导入内容为静态几何、材质和纹理。

也可使用以下命令，`<RenderLab.exe>` 替换为实际构建产物路径：

```text
<RenderLab.exe> --import-mesh <model>
<RenderLab.exe> --import-texture <image>
<RenderLab.exe> --import-environmentmap <image>
<RenderLab.exe> --import-pbrt-scene <source>
```

CLI 相对源路径以仓库为基准，默认 ID 为文件名去掉扩展名；完整场景通过编辑器或 API 导入。

普通贴图生成完整 Mipmap；sRGB 颜色在线性空间过滤，数据贴图保持 Linear。环境导入生成 Radiance、Irradiance 和 GGX Prefiltered Cubemap。

PBRT 导入保存独立场景、共享材质与纹理图，并将所有输入依赖复制到资产目录，详见 [PBRT 导入](../docs/Pbrt.md)。

## 场景与配置

- [config/config.json](../config/config.json) 指定资产根目录、初始场景及其他配置路径。
- 资产根目录相对配置目录解析；资产中的数据路径相对资产根目录。
- Shader 二进制由 CMake 生成，描述中的路径相对运行工作目录。
- 场景保存 Actor / Component、材质覆盖、独立编辑器相机与环境图。
- 场景内容通过显式保存写入；切换场景会丢弃未保存修改，关闭时记录默认场景选择。

运行前需准备展示场景所用的模型与导入资产，并设置有效的初始场景。

## 来源与许可

外部模型和纹理的许可随源文件保留。Sibenik 来自 [McGuire Computer Graphics Archive](https://casual-effects.com/g3d/data10/index.html)，采用 CC BY-NC；Khronos 示例资产的来源见 [材质示例](../docs/MaterialSamples.md)。内置 Sheen LUT 的来源与许可证见 [LUT 说明](../docs/SheenLut.md)。
