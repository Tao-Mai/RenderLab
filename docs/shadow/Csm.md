# 稳定 CSM：精度、稳定性与接缝

[阴影概览](../Shadow.md) · [项目首页](../../README.md)

Forward / Deferred 共用方向光级联阴影，支持 1～4 级。目标是在有限分辨率下把精度优先给近处，并减少相机移动引起的阴影闪烁。

![CSM 测试场景](../images/csm.png)

## 从视锥到阴影图

1. 用逆 VP 还原相机视锥角点，将远端截断到最大阴影距离。
2. 按相机空间深度划分 Cascade，为每段获得独立角点。
3. 为各段建立光源方向的正交投影，绘制遮挡物深度。
4. 着色时根据接收点的相机深度选择 Cascade，再进行阴影比较。

深度指相机前向轴上的距离，不是到相机的欧氏距离。超过最大阴影距离不计算该方向光阴影。

代码：[ShadowData.cpp](../../src/render/feature/shadow/ShadowData.cpp)、[ShadowPass.cpp](../../src/render/feature/shadow/ShadowPass.cpp)、[scene_lighting.slang](../../shaders/scene_lighting.slang)。

## 精度分配：Uniform 与 Practical

透视相机中近处占据更多屏幕像素。均匀划分世界深度时，第一层通常过宽，近处细阴影仍会明显走样。

设近端 n、远端 f、层数 m，第 i 个分界为：

- Uniform：`n + (f - n) × i / m`。
- Logarithmic：`n × (f / n)^(i / m)`。
- Practical：在两者之间按 lambda 混合；lambda 为 0 等同 Uniform。

编辑器可切换 Uniform / Practical。划分不随当前物体分布变化，避免物体进出视野直接扰动分界。CSM 只是重新分配精度，不能消除有限分辨率。

代码：[splitFrustum / ShadowData.cpp](../../src/render/feature/shadow/ShadowData.cpp)。原理参考：[Microsoft CSM](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps)。

## 稳定投影：包围球与 Texel Snapping

紧贴视锥角点构造光源空间 AABB 时，相机旋转会改变投影宽高，导致世界空间 Texel 大小变化。

当前先用角点均值与最远距离构造保守包围球，再使用球的光源方向外接正方体。相机投影和分界不变时，旋转不再让覆盖尺寸持续变化。它不是最小包围球，会牺牲一些利用率来换取稳定性。

尺寸固定后，将光源空间 XY 中心对齐到 Texel 网格，使投影随整数 Texel 更新，而不是连续滑动。半径轻微向上量化；投影自动预留滤波核与对齐所需边界，不再叠加手动 Padding。

这不是时域滤波：跨对齐阈值、改变 FOV、光源方向或分辨率时，阴影仍会更新。

代码：[boundFrustumCorners / makeDirectionalShadow](../../src/render/feature/shadow/ShadowData.cpp)。原理参考：[以 Texel 为单位移动投影](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps#moving-the-light-in-texel-sized-increments)。

## 遮挡物覆盖：Depth Clamp 与五平面剔除

能遮住接收点的物体，可能处在光源近裁剪面之前。仅按 Cascade 的六面视锥剔除，会提前丢掉这些上游遮挡物。

方向光阴影启用 Depth Clamp，CPU 对应忽略光源 Near Plane，仅使用四个侧面和 Far Plane。投影与剔除均包含级联重叠区和滤波边界；主相机与其他阴影仍使用正常六面剔除。

代码：[ShadowPass.cpp](../../src/render/feature/shadow/ShadowPass.cpp)、[Bounds.cpp](../../src/core/math/Bounds.cpp)。

## 边缘与接缝：PCF 和 Cascade Blending

- **PCF**：8 点泊松分布，默认半径 2 Texel，可调半径与逐像素旋转。比较后平均 Visibility，而不是平均深度。
- **过滤模式**：Nearest / Linear 控制单次比较，独立于多点 PCF。相同 Texel 半径在不同 Cascade 对应不同世界宽度。
- **级联混合**：每级末端建立过渡区，默认占深度区间的 10%；后一级投影向前扩展以覆盖该区域。
- **混合方式**：过渡区内分别采样相邻两级，再按深度线性插值 Visibility；不混合原始深度或投影矩阵。
- 最后一级目前不向无阴影区域淡出。PCF 不是物理软阴影，也不能代替合理的精度分配。

代码：[directionalShadowVisibility / scene_lighting.slang](../../shaders/scene_lighting.slang)、[sampling.slang](../../shaders/sampling.slang)。

## 测试场景

选择 `csm_test`：地面、沿纵深排列的方块 / 球体、近处细柱与连续横梁，以及一盏方向光。场景不会自动修改全局渲染配置。

建议先用 4 级、50 m、512 分辨率，关闭 Blend 与 PCF，使用 Nearest 对比：

- Uniform / Practical：观察近处细柱阴影的精度损失。
- Blend 从 0 调到 0.1：观察横梁连续阴影的级联接缝。
- 慢速平移和旋转相机：观察采样网格的稳定性与量化更新。
- PCF 半径从 0 调到 2：区分边缘平滑与实际分辨率。
- 远处几何体：观察精度下降和最终覆盖截止。

这里没有注入噪声或特定场景分支，测试用于区分精度分配、投影稳定、滤波和混合各自解决的问题。
