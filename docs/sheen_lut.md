# Sheen 方向反照率 LUT

- 官方来源：[Khronos glTF Sample Renderer / lut_sheen_E.png](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/506f05c3ff6deaa202a5384ba1ab9891b3c7f210/assets/images/lut_sheen_E.png)。
- 固定版本：`506f05c3ff6deaa202a5384ba1ab9891b3c7f210`。
- 源文件：[lut_sheen_E.png](../src/asset/builtin/data/lut_sheen_E.png)，1024×1024、RGB、每通道 16 位整数；编译时嵌入程序。
- 源文件 SHA-256：`7f21d7754dd3a2a972d9d1298ee3e67e20c5b2f21969095d322a1bc20f8b2f04`。
- 许可证：[Apache-2.0 原文](../src/asset/builtin/data/lut_sheen_E.LICENSE.md)，随源文件保留。

## 内置资产

- 类型化 ID：`BuiltinAssets::Texture::kSheenLut`。
- 描述与像素由 [BuiltinTexture.cpp](../src/asset/builtin/BuiltinTexture.cpp) 按需提供，不依赖资产目录中的 JSON 或二进制缓存。
- 格式：`RGBA32F`、`Linear`、2D、单级 mip。16 位整数直接除以 65535 转为浮点，不经 8 位中间格式或 sRGB 解码；保留 RGB，Alpha 为 1，像素行不翻转。
- 采样使用现有 `linearClamp`，UV 为 `(NdotV, sheenRoughness)`，读取 R 通道；粗糙度不在采样前平方。坐标约定见官方 [albedoSheenScalingLUT](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/506f05c3ff6deaa202a5384ba1ab9891b3c7f210/source/Renderer/shaders/material_info.glsl#L388)。参数空间 LUT 不生成普通贴图的 mip 链。
- 用途是 Sheen 层的方向反照率及底层能量缩放；不是 `lut_charlie.png` 的 Sheen IBL 积分表，两者不能互换。官方用法见 [pbr.frag](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/506f05c3ff6deaa202a5384ba1ab9891b3c7f210/source/Renderer/shaders/pbr.frag#L238)。

SceneBindings 通过现有 AssetManager / RenderResourceManager 链路绑定内置 LUT，用于 Sheen 分层的底层能量缩放；不再提供渲染配置入口或单独导入工具。完整链路见 [sheen.md](sheen.md)。
