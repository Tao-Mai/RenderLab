# Sheen 方向反照率 LUT

- 官方来源：[Khronos glTF Sample Renderer / lut_sheen_E.png](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/506f05c3ff6deaa202a5384ba1ab9891b3c7f210/assets/images/lut_sheen_E.png)。
- 固定版本：`506f05c3ff6deaa202a5384ba1ab9891b3c7f210`。
- 源文件：[resource/lut_sheen_E.png](../resource/lut_sheen_E.png)，1024×1024、RGB、每通道 16 位整数。
- 源文件 SHA-256：`7f21d7754dd3a2a972d9d1298ee3e67e20c5b2f21969095d322a1bc20f8b2f04`。
- 许可证：[Apache-2.0 原文](../resource/lut_sheen_E.LICENSE.md)，随源文件保留。

## 导入结果

- Texture ID：`lut_sheen_E`。
- 描述：[assets/Texture/lut_sheen_E.json](../assets/Texture/lut_sheen_E.json)。
- 数据：[assets/binary/texture/lut_sheen_E.bin](../assets/binary/texture/lut_sheen_E.bin)。
- 格式：`RGBA32F`、`Linear`、2D、单级 mip。16 位整数直接除以 65535 转为浮点，不经 8 位中间格式或 sRGB 解码；保留 RGB，Alpha 为 1，像素行不翻转。
- 采样使用现有 `linearClamp`，UV 为 `(NdotV, sheenRoughness)`，读取 R 通道；粗糙度不在采样前平方。坐标约定见官方 [albedoSheenScalingLUT](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/506f05c3ff6deaa202a5384ba1ab9891b3c7f210/source/Renderer/shaders/material_info.glsl#L388)。参数空间 LUT 不生成普通贴图的 mip 链。
- 用途是 Sheen 层的方向反照率及底层能量缩放；不是 `lut_charlie.png` 的 Sheen IBL 积分表，两者不能互换。官方用法见 [pbr.frag](https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/506f05c3ff6deaa202a5384ba1ab9891b3c7f210/source/Renderer/shaders/pbr.frag#L238)。

现已通过 `renderer.json` 的 `sheenLut` 绑定接入场景 shader，用于 Sheen 分层的底层能量缩放；完整链路见 [sheen.md](sheen.md)。

## 一次性导入工具

现有普通图片导入器不支持 16 位整数 PNG，因此使用 [test/ImportSheenLut.cpp](../test/ImportSheenLut.cpp) 解码数据，再调用现有 AssetManager / AssetDataManager 持久化，并核对回读内容。工具不初始化 ConfigManager，不改写任何配置；同名资产存在时直接报错，不覆盖已有数据。

在构建目录执行：

```text
test/ImportSheenLut.exe ../resource/lut_sheen_E.png ../assets
```
