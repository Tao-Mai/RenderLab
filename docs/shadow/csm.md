# CSM：实现、优化与测试

本文说明当前方向光级联阴影的算法选择、解决的问题和验证方法，不展开 Vulkan 资源组织或接口细节。Forward / Deferred 共用该算法，目前支持 1～4 级 cascade。

## 为什么使用 CSM

透视相机下，近处物体占据更多屏幕像素。若整个可见范围共用一张阴影图，近处容易出现一个阴影 texel 覆盖多个屏幕像素的透视走样。

CSM 将相机视锥按深度分段，每段分配同样分辨率的阴影图，让近处使用较小的世界空间覆盖范围，以获得更高的阴影精度。它重新分配精度，并不消除分辨率限制。[原理参考：Microsoft CSM](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps#cascaded-shadow-maps-and-perspective-aliasing)。

## 基本过程

1. 从相机视投影还原视锥角点，将远端截断到最大阴影距离。
2. 按相机空间深度划分 cascade，得到各段角点。
3. 为每段构造方向光正交投影，分别渲染遮挡物的深度。
4. 着色时按接收点的相机深度选择 cascade，比较阴影深度，得到 visibility。

这里的深度是沿相机前向轴的距离，不是接收点到相机的欧氏距离。超过最大阴影距离时，不再计算方向光阴影。

代码：[视锥还原与投影计算：ShadowData.cpp](../../src/render/feature/shadow/ShadowData.cpp#L64)、[阴影绘制：ShadowPass.cpp](../../src/render/feature/shadow/ShadowPass.cpp#L155)、[级联选择：scene_lighting.slang](../../shaders/scene_lighting.slang#L126)。

## 深度划分

设近端为 n，阴影远端为 f，共 m 级，第 i 个分界取以下策略。

- **Uniform**：均匀分配深度区间，分界为 `n + (f - n) · i / m`。计算简单，但近处第一段往往过宽，阴影精度分配不符合透视投影的需求。
- **Practical**：将均匀分界与对数分界 `n · (f / n)^(i / m)` 混合。lambda 越大，越偏向对数划分、越强调近处精度；lambda 为 0 时等同均匀划分。

划分依据相机参数和固定阴影距离，不随当前物体分布动态调整，避免可见物体变化直接引起分界抖动。编辑器可切换策略；Uniform 不使用 lambda。

代码：[splitFrustum：ShadowData.cpp](../../src/render/feature/shadow/ShadowData.cpp#L86)。

## 稳定正交投影

### 包围球与外接正方体

直接在光源空间紧贴角点计算外接长方体，虽然覆盖紧凑，但相机旋转时，其宽高会随角点方向变化。阴影图的世界空间 texel 大小也随之变化，导致边缘闪烁。

当前先以角点均值为球心、最远角点距离为半径构造保守包围球，再按光源方向建立坐标系，生成球的外接正方体并做正交投影。固定相机投影与分界时，刚体旋转不改变球半径，从而避免投影尺寸随视角变化而“呼吸”。

球不是最小包围球，正方体也比紧贴角点的长方体宽松：代价是损失一些有效分辨率，换取更稳定的覆盖尺寸。

代码：[boundFrustumCorners：ShadowData.cpp](../../src/render/feature/shadow/ShadowData.cpp#L47)、[正方体投影：makeDirectionalShadow](../../src/render/feature/shadow/ShadowData.cpp#L127)。

### Texel snapping

稳定尺寸后，投影中心若仍连续移动，阴影采样网格仍会在物体上滑动。因此把光源空间 XY 中心对齐到 texel 网格，使投影位置只按整数 texel 更新；深度方向不做这种对齐。

世界空间 texel 大小由投影边长除以阴影图分辨率得到。半径轻微向上量化，减少浮点扰动造成的网格尺度变化；投影自动留出 PCF 核、中心对齐偏移与比较过滤所需的边界，避免对齐或滤波损失覆盖，不需要手动 padding。

这解决的是采样网格稳定性，不是时域滤波。跨过对齐阈值时投影仍会更新；改变光源方向、FOV、分界或分辨率也会改变网格。[原理参考：以 texel 为单位移动投影](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps#moving-the-light-in-texel-sized-increments)。

代码：[投影尺寸与中心对齐：ShadowData.cpp](../../src/render/feature/shadow/ShadowData.cpp#L166)。

## 遮挡物覆盖与剔除

接收点位于相机视锥内，不代表为它投影的遮挡物也在视锥内。尤其是方向光上游的物体，可能落在 cascade 的光源近裁剪面之前，却仍能遮住接收点。

方向光阴影使用 depth clamp，让这些物体保留并将超出近端的深度钳制到近端。CPU 必须采用一致的覆盖规则：cascade 只用四个侧面和 far plane 剔除，忽略光源 near plane，不能在提交绘制前就丢掉上游遮挡物。far plane 仍用于排除下游、无法遮挡该覆盖区的物体。

主相机与其他阴影类型仍使用各自正常的六面剔除。cascade 的剔除范围包含 blending 重叠区和滤波边界。

代码：[方向光 depth clamp：ShadowPass.cpp](../../src/render/feature/shadow/ShadowPass.cpp#L58)、[cascade 五面剔除：RenderPreparation.cpp](../../src/render/RenderPreparation.cpp#L40)、[平面测试：Bounds.cpp](../../src/core/math/Bounds.cpp#L15)。

## PCF：改善阴影边缘走样

单次深度比较只能给出遮挡或未遮挡。PCF 在接收点附近做多次深度比较，再平均 visibility，改善阴影边缘；不是先平均深度再比较。

当前使用独立的 **8 点圆盘分布**，半径默认 **2 texel**，可在编辑器调整，设为 0 回到单次比较采样。复用逐像素旋转开关，以减弱固定采样图案；越界样本视为未遮挡。

Nearest / Linear 控制每一次比较采样的过滤方式，PCF 控制多次采样的分布与平均。Nearest 也可以做 PCF，但多个采样点仍可能落到同一 texel。扩大半径会扩大滤波区域，减少采样数则降低开销，也可能使结果更粗糙。

相同 texel 半径在不同 cascade 中对应不同的世界空间宽度；PCF 不能替代合理的分界，也不是基于光源尺寸的物理软阴影算法。

代码：[sampleDirectionalCascade：scene_lighting.slang](../../shaders/scene_lighting.slang#L90)、[8 点采样分布：sampling.slang](../../shaders/sampling.slang#L6)。

## Cascade blending：减弱级联接缝

相邻 cascade 的 texel 密度和滤波宽度不同，直接切换 visibility 会暴露接缝。因此在每级深度区间的末尾建立过渡区，默认占该区间的 10%。

后一级投影向前扩展，覆盖前一级的过渡区；否则第二张阴影图可能没有该区域的数据。过渡区内分别计算两级经过 PCF 的 visibility，再按深度线性插值；区外只采当前级。混合的是 visibility，不是原始深度或投影矩阵。

设过渡起点为 a、级联上限为 b，接收深度为 d，权重为 `(d - a) / (b - a)`。混合比例为 0 时关闭；最后一级不向无阴影区域淡出。

代码：[重叠范围：splitFrustum](../../src/render/feature/shadow/ShadowData.cpp#L86)、[visibility 混合：directionalShadowVisibility](../../shaders/scene_lighting.slang#L126)。

## 测试场景

在左侧场景列表选择 [csm_test](../../assets/Scene/csm_test.json)。场景包含地面、沿纵深布置的方块与球体、近处细柱、连续横梁和一盏方向光，相机已对准测试区域。

### 对比基线

使用 Forward 或 Deferred，开启阴影，先设置：

- 4 级 cascade、最大距离 50 m、分辨率 512。
- Nearest、bias 0.0005、lambda 0.5。
- Blend ratio 0、Directional PCF radius 0，先观察未混合、未多点过滤的结果。

这些是手动对比参数，切换场景不会自动改写全局渲染配置。编辑器中的选择正常关闭时持久化。

### 观察方法

- **划分策略**：固定相机与分辨率，切换 Practical / Uniform，观察最前方六根细柱的阴影。near 为 0.1 m 时，混合第一层约到 6.5 m，均匀第一层约到 12.6 m；均匀划分覆盖更大，近处细阴影更容易变粗或断续。
- **级联交界**：沿横梁的连续阴影观察。混合分界约为相机深度 6.5 / 13.6 / 24.0 / 50 m，均匀约为 12.6 / 25.1 / 37.5 / 50 m。将 Blend ratio 从 0 调到 0.1，对比精度与滤波宽度跳变是否缓和。
- **移动稳定性**：按住右键，缓慢左右移动并轻微旋转视角，观察细柱和横梁阴影。包围球稳定尺寸，texel snapping 避免网格连续滑动；跨量化边界、逐像素旋转以及其他采样走样仍可能造成可见变化，不应视为已实现时域抗锯齿。
- **PCF**：把 Directional PCF radius 从 0 调到 2，并比较 Nearest / Linear 与逐像素旋转开关。观察边缘平滑程度和采样颗粒，不把软化程度误认为分辨率提高。
- **远处与覆盖截止**：后方较大的几何体用于观察远级联精度；最后一组在初始最大阴影距离外，用于观察覆盖截止。当前没有末级距离淡出。

场景没有注入噪声或针对场景 ID 的特殊渲染逻辑。测试目标是区分精度分配、投影稳定性、滤波和级联混合各自解决的问题。

配置与编辑器入口：[RendererConfig.h](../../src/render/RendererConfig.h)、[RendererSettings.cpp](../../src/editor/RendererSettings.cpp)。
