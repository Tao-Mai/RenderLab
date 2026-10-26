# Sheen 方向反照率 LUT

[Sheen 着色](Sheen.md) · [资产模块](Asset.md)

该 LUT 保存 Charlie Sheen 的方向反照率，用于材质分层时缩放底层能量。

## 来源与许可

- 来源：[Khronos glTF Sample Renderer / lut_sheen_E.png](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/506f05c3ff6deaa202a5384ba1ab9891b3c7f210/assets/images/lut_sheen_E.png)。
- 固定版本：`506f05c3ff6deaa202a5384ba1ab9891b3c7f210`。
- 原图：[lut_sheen_E.png](../src/asset/builtin/data/lut_sheen_E.png)，1024 × 1024、RGB 16-bit。
- 许可证：[Apache-2.0](lut_sheen_E.LICENSE.md)。
- SHA-256：`7f21d7754dd3a2a972d9d1298ee3e67e20c5b2f21969095d322a1bc20f8b2f04`。

## 使用方式

- 通过内置纹理 ID `BuiltinAssets::Texture::kSheenLut` 获取。
- PNG 编译时嵌入，按需解码为 Linear RGBA32F，保留 16-bit 精度。
- 单级 Mip，Linear Clamp 采样；UV 为 `(NdotV, sheenRoughness)`，读取 R 通道。
- LUT 坐标使用原始 Roughness。

实现入口：[BuiltinTexture.cpp](../src/asset/builtin/BuiltinTexture.cpp)、[Renderer.cpp](../src/render/Renderer.cpp)、[FrameData.cpp](../src/render/FrameData.cpp)、[scene_lighting.slang](../shaders/scene_lighting.slang)。
