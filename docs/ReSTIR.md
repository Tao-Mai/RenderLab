# ReSTIR DI

[渲染模块](Render.md) · [资源布局](RenderLayout.md)

实时光追使用 2020 年的 [Spatiotemporal Reservoir Resampling](https://research.nvidia.com/labs/rtr/publication/bitterli2020spatiotemporal/)。初始 RIS、时域复用、空间复用分别对应原论文的 Algorithm 3、5、6，采用带支持域和可见性校正的归一化。实现参考了 [RTXDI 的 reservoir](https://github.com/NVIDIA-RTX/RTXDI-Library/blob/main/Include/Rtxdi/DI/Reservoir.hlsli) 与 [初始采样](https://github.com/NVIDIA-RTX/RTXDI-Library/blob/main/Include/Rtxdi/DI/InitialSampling.hlsli) 接口。

## 执行流程

`RayTracingPass` 顺序发起三次 RayGen dispatch，dispatch 之间显式同步存储读写：

1. `raygenMain` 追踪路径，为主命中保存表面信息和初始 reservoir，输出间接光与相机可见发光。
2. `temporalMain` 将当前初始 reservoir 与重投影位置的上一帧 reservoir 合并。
3. `spatialMain` 合并当前像素与随机邻居的时域结果，计算最终直接光，保存下一帧历史。

空间阶段只读本帧时域结果，写入独立的历史层，因此相邻像素之间没有原地读写竞争。SVGF 继续接收 direct、indirect、emission 和 albedo；关闭 SVGF 时，第三阶段直接把直接光加到合并颜色上。

时空复用作用于主命中的直接光。更深层表面使用初始 RIS 估计该处直接光，再乘路径吞吐量计入间接光。路径通过 BRDF 采样继续传播，管线递归深度保持为 1。

## 完整光源样本

reservoir 保存光源种类、索引和二维采样坐标。对于面积光，这些坐标确定光源上的具体位置；在所有复用阶段保持同一位置。

| 光源 | 样本表示与求值 |
| --- | --- |
| 发光三角形 | 三角形索引与均匀面积采样坐标；由重心坐标插值位置、UV，读取材质发光纹理 |
| 矩形面光 | LightData 索引与矩形内二维坐标 |
| 点光 / 聚光 | LightData 索引；计算距离衰减、range 和聚光内外锥角衰减 |
| 方向光 | LightData 索引；使用固定入射方向 |
| 环境 | 球面方向的二维坐标；读取 Radiance Cubemap，受 IBL 开关控制 |

初始光源提议在非空的解析光源、发光三角形和启用的环境三组中等概率选组，再在组内均匀选光源。面积光在表面上均匀采样，环境在球面上均匀采样。三角形仍由 `RayTracingScenePass` 按实例展开，点光、聚光、方向光和矩形继续使用现有 LightData。

一次表面求值生成配置指定的 M 个光源候选，以及一个 BRDF 方向候选。BRDF 射线命中发光三角形、矩形或环境时，将命中转换为相同的光源样本表示；命中普通表面则贡献零权重。两类候选采用 balance MIS 后一起进入初始 reservoir。

## 初始 RIS 的权重

采用“离散光源索引 × 单位正方形采样坐标”作为统一积分域。面积光在这个域的 RGB 被积函数为：

`F = Le × BRDF × 接收面余弦 × 发光面余弦 × 面积 / 距离²`

环境对应 `F = Le × BRDF × 接收面余弦 × 4π`。点光、聚光和方向光使用各自的离散直接光贡献。目标函数 `h` 取 F 的亮度，包含实际采样位置的发光、BRDF 和几何项。遮挡在 reservoir 选出样本后查询。

令 `qL` 为选组、选光源的概率，`qB` 为转换到相同积分域的 BRDF 密度：

`q = (M × qL + qB) / (M + 1)`

面积光的 `qB = BRDF立体角PDF / 面积采样立体角PDF`；环境采用相同的换元，delta 光源的 qB 为零。每个候选以 `w = h / q` 加入 reservoir，累计 S，再以 `w / S` 的概率替换已选完整样本。零权重候选仍计入样本数。

初始阶段结束时：

`W = S / ((M + 1) × h(选中样本))`

随后发射可见性射线，遮挡样本的 W 置零、计数保留。最终颜色为 `F × W × 可见性`。光源提议与 BRDF 提议已在初始 reservoir 中联合估计，路径续射线命中的发光由上一顶点的估计覆盖；相机直接可见的发光单独保存在 emission。

## 时域复用

当前世界位置经上一帧 viewProjection 投影到上一帧视口，在邻近 3×3 像素中选择最近的有效历史。检查对象索引、几何法线和预期线性深度：法线点积至少 0.9，深度差不超过预期深度的 10%，并保留 0.001 的绝对容差。

上一帧 reservoir 的有效计数最多取 `当前初始计数 × maxHistoryLength`，默认上限为 20 倍，限制旧样本的影响。这里只截断计数，保留来源 reservoir 的归一化权重。

合并来源 reservoir i 时，在当前表面重新计算它保存的完整光源样本 yᵢ：

`wᵢ = h当前(yᵢ) × Wᵢ × Mᵢ`

流式选择后，重新检查最终样本在各来源表面的目标值和可见性，只累计支持该样本的来源计数得到 Z：

`W = Σwᵢ / (h当前(y) × Z)`

这个校正使用来源表面的 BRDF、法线、位置和可见性，处理邻居朝向或遮挡不同导致的支持域差异。输出前检查当前表面的可见性，被遮挡的结果置零后再提供给下一阶段。

## 空间复用

每个像素在圆盘内随机选取配置数量的邻居，默认 3 个、半径 30 像素。越界、背景、自身以及深度 / 法线不匹配的候选跳过。邻居来自独立的本帧时域缓冲，按照同一合并公式与 Algorithm 6 的 Z 归一化处理。

合并完成后查询最终样本的可见性并着色，遮挡结果的 W 置零，再将 reservoir 写入当前历史层。可见性校正会额外发射来源表面的阴影射线，这是当前版本的主要复用成本。

## 资源与历史生命周期

| 资源 | 每像素大小 | 用途 |
| --- | --- | --- |
| 表面缓冲，两份 | 64 B × 2 | 位置、深度、压缩法线与视线方向、统一 BRDF、材质和对象索引 |
| 初始 / 时域 reservoir | 32 B | 初始结果由时域阶段就地更新，随后供空间阶段只读 |
| reservoir 历史，两层 | 32 B × 2 | 上一帧读取、当前帧写入 |

表面 BRDF 颜色使用 Half 打包，方向使用 Octahedral 16×2 打包，roughness 保留 FP32。每像素 reservoir 保存完整光源样本、归一化 W、选中样本的目标值及候选计数。ReSTIR 专用缓冲合计 224 B / 像素，由 Graph 持有；图重建时重新创建并重置历史。

SVGF 开启 / 关闭各自持有匹配的 Pipeline、Descriptor Set Layout 和 SBT，在首次设置时创建并缓存。开关切换复用对应变体；图重建时释放旧描述符集，再按当前布局重新分配并绑定新资源。关闭模式的布局只声明该模式实际使用的资源。

相机移动保留 reservoir，并通过重投影匹配。场景、网格实例及其变换、材质、光源、环境、IBL、反弹次数和 ReSTIR 配置变化时清空历史。目前运动物体使用这种重置策略；相机运动沿用静态世界位置重投影。

## 配置

所有参数位于 `feature.restir`，编辑器实时光追的 ReSTIR 面板可以调整。参数修改复用已有 Pipeline，并重置 ReSTIR 与 SVGF 历史。

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| candidateCount | 32 | 每次表面求值的光源候选数，另加一个 BRDF 候选 |
| temporalReuse | true | 开启上一帧 reservoir 复用 |
| spatialReuse | true | 开启本帧邻居复用 |
| maxHistoryLength | 20 | 上一帧计数相对当前初始计数的上限倍数 |
| spatialSampleCount | 3 | 一轮空间复用尝试的邻居数量 |
| spatialRadius | 30 | 屏幕空间的邻居搜索半径，单位为像素 |

关闭两种复用时保留初始 RIS，可用于比较。SVGF 单独控制颜色的时域累积与滤波；ReSTIR 历史保存采样结果，两者各自维护自己的资源。

实现入口：[初始采样与求值](../shaders/raytracing/realtime/restir.slang)、[时空复用](../shaders/raytracing/realtime/restir_reuse.slang)、[GPU 数据](../shaders/raytracing/realtime/restir_data.slang)、[CPU 调度](../src/render/raytracing/realtime/RayTracingPass.cpp)。

## 验证

使用固定曝光、关闭 Bloom 与 SVGF 的 128×128 连续帧，采集曝光前 RGBA16F 结果。每组渲染 96 帧，前 32 帧用于建立历史，后 64 帧统计；对比相同 M 下的初始 RIS 与时空复用。

小场景包含两组面积和发光强度不同的三角形光源。全复用与初始 RIS 的平均亮度差约 0.14%，加入遮挡后差约 0.21%，同时检查了移动相机、有限权重和计数上限。Bistro 保留 76,248 个发光三角形和局部光源、关闭太阳光，M=32；测试视角的接收面区域时域方差降低约 98.8%，全图相邻帧平均差异降低约 94.7%。这些数值对应当前测试视角和分辨率。

空光源、仅环境、仅点光源，以及 512×512、七次散射、开启 SVGF 的 Bistro 均通过命令行 GPU 验证；两种实时 Shader 变体通过 SPIR-V 校验。
