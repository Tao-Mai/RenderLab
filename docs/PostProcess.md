# 后处理

在 `Post Process → Post process` 下拉框选择 `RenderLab` 或 `Falcor`。两套参数独立保存在
`config/renderer.json`，默认仍选择 RenderLab；两套流程都由 `Enable HDR` 控制。
后处理读取场景的线性 HDR 颜色，编辑器 UI 在后处理之后绘制。

## Falcor 流程

来源为用户提供的 Falcor 源码，commit `759aad033ff610fb0d82c74f7e0a508d0096d5f2`，
对应 `Source/RenderPasses/SimplePostFX`、`Source/RenderPasses/ToneMapper` 与
`Source/Falcor/Utils/Color/ColorUtils.h`。本项目选定的连接顺序是
`HDR → SimplePostFX → ToneMapper → 屏幕输出`；Falcor 本身允许在渲染图中自由连接这两个 Pass。

| 模块 | 移植内容 |
| --- | --- |
| SimplePostFX Bloom | 固定 8 次降采样与 8 次升采样，RGBA16F 金字塔，原始 5×5 / 3×3 双线性采样核，黑色 Border，逐级原位混合 |
| 星芒 | 第 1、2 级的三方向、六条射线，保留 41 个线采样位置、原始权重与归一化常数 |
| 镜头效果 | 暗角、径向色差、桶形畸变 |
| 调色 | 阴影/中间调/高光饱和度二次曲线，以及颜色、亮度两组 Offset / Power / Scale |
| 对比控制 | Enable、Wipe，保留源图像与 Alpha 的原始行为 |
| 自动曝光 | 对输入重采样至各边向下取整的二次幂尺寸；计算 `log2(max(0.0001, luminance))`，逐级线性降采样，曝光 Key 为 0.042 |
| 手动曝光 | ISO、光圈、倒数快门、EV，光圈优先/快门优先，曝光补偿 |
| 白平衡 | Rec.709 ↔ XYZ ↔ CAT02 LMS，原始色温近似与矩阵系数 |
| 色调映射 | Linear、Reinhard、Reinhard Modified、Heji Hable ALU、Hable UC2、ACES，原始白点参数与 Clamp 开关 |

默认值遵循 Falcor：ACES、手动曝光、ISO 100、f/1、倒数快门 1、曝光补偿 0、白平衡关闭、
SimplePostFX 启用但各镜头效果强度为 0。切换到 Falcor 不会带入 RenderLab 的相机 EV100、
直方图曝光、曝光时间适应或 Bloom 参数。

专用 Shader 与 [FalcorPostProcess](../passes/postprocess/FalcorPostProcess/FalcorPostProcess.h) 一起放在
[passes/postprocess/FalcorPostProcess](../passes/postprocess/FalcorPostProcess/)。参数与主机端计算见
[FalcorPostProcessConfig](../passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h)。

## 保留的源码行为

为了让相同输入与参数对应 Falcor 的结果，以下可疑行为也按原源码保留：

- Modified Reinhard 公式末尾乘以 `(1 + luminance)`。
- Bloom 5×5 核的最外侧权重使用 `(1 + 10) / 512`。
- SimplePostFX 的 identity 判断不包含暗角；只改变暗角且其他参数仍为默认值时，会跳过效果。
- Reinhard 和 Modified Reinhard 对零亮度的除法没有额外保护。
- ACES 自带饱和裁剪，关闭统一 Clamp 也不会移除此裁剪。

## 与 Falcor 框架的差异

- RenderLab 场景没有 Falcor 的 `filmISO`、`fNumber`、`shutterSpeed` 元数据；不会自动导入这三个值。
  使用 Falcor 面板中的对应参数控制手动曝光，原有相机曝光只作用于 RenderLab 模式。
- 输出大小沿用 RenderLab 渲染目标，不提供 Falcor RenderGraph 的独立 `outputSize`、`fixedOutputSize`
  或任意 `outputFormat` 选项。SimplePostFX 与 ToneMapper 之间、ToneMapper 与屏幕输出之间保留 RGBA32F；
  最终屏幕格式由交换链决定，无法用 8 位屏幕目标呈现超出显示范围的数值。
- 自动曝光的 mip 各级用独立图资源表达，保留相同尺寸、R32F 精度和逐级线性采样；最后一级仍为 1×1。
  原输入经过 SimplePostFX 的 RGBA32F 输出，因此使用 Falcor 对应的 R32F 亮度路径。
- RHI 资源绑定、全屏入口与调度接入本项目。屏幕输出对 sRGB 附件使用硬件编码，对 UNORM 附件显式编码，
  避免重复 Gamma。不同图形后端、GPU 和编译器的浮点与纹理过滤结果不保证逐位一致。
- 不包含 TAA、DLSS、NRD、OptiX 等独立的抗锯齿、超分和降噪模块；本次移植范围是 ToneMapper 与 SimplePostFX。

## 验证

`FalcorPostProcessCheck` 使用非二次幂、非工作组整数倍的 35×19 HDR 输入，在 GPU 上检查全部 6 个
ToneMapper、sRGB/UNORM 输出、Alpha、自动曝光、手动曝光、白平衡、Bloom/星芒/镜头调色、Wipe、
禁用效果与两套图替换，并运行主渲染器的 10 轮、40 帧切换检查。输出量化值允许 1 个 8 位单位的舍入差异。

```powershell
cmake --build cmake-build-debug-gcc-16.2 --target FalcorPostProcessCheck
# 沿用项目的依赖 DLL 环境。
Push-Location cmake-build-debug-gcc-16.2
.\FalcorPostProcessCheck.exe
Pop-Location
python test/FalcorPostProcessSourceCheck.py <Falcor源码目录>
```

源码检查直接对照调用者指定的 Falcor checkout，验证移植后的滤波核、镜头效果核心与 ToneMapper
曲线没有改变数值表达式。它不等同于运行完整 Falcor 后逐像素比较同一场景。

移植文件保留 NVIDIA 许可头，完整许可见 [Falcor.txt](licenses/Falcor.txt)。
