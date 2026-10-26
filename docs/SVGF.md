# SVGF

开启 `feature.rayTracing.svgf` 时执行下面的链路，关闭时直接使用光追合并颜色。

`RayTracingPass → SVGFReprojection → SVGFFilter × 5（最后一级直接合成）`

实时光追 Shader 使用 `SVGF=0/1` 选择合并输出或 SVGF 所需的分离输出。

实现依据：[Schied et al., Spatiotemporal Variance-Guided Filtering, HPG 2017](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/)
。直接光与间接光独立处理，共享深度、法线与几何权重。

## Albedo 解调

光追 Pass 在 SVGF 开启时输出主命中表面的线性 `baseColor`，包含材质纹理和颜色因子；背景与直接可见的面光源使用中性白色。重投影先将当前
direct / indirect 除以该像素的 albedo，颜色累积、亮度一阶 / 二阶矩、时域 / 空间方差及后续五级滤波均在解调后的空间中计算。7×7
空间方差使用每个邻域像素各自的 albedo。

最后一级将两路滤后结果相加，乘回当前像素的 albedo，再直接添加当前帧 emission。颜色历史保存解调后的结果。

黑色 / 极暗通道使用最小值 0.01，除法和乘回统一采用 `factor = max(albedo, 0.01)`，保留这些通道中的镜面光照。最终颜色为
`emission + (filteredDirect + filteredIndirect) * factor`，使用当前帧 albedo 恢复纹理细节。

## 重投影与统计

- 根据当前线性深度重建世界位置，投影至上一帧；每个双线性采样点单独校验深度、法线和 viewport，再对有效权重归一化。没有有效双线性采样点时，在
  3×3 邻域查找匹配表面；仍无匹配则丢弃历史。
- 直接光、间接光分别累积颜色与亮度的一阶 / 二阶原始矩。样本数不超过 `historyLength` 时使用 `1 / sampleCount`，超过后使用
  `emaAlpha`；首次或历史失效时使用权重 1。
- 时域方差为 `max(E[L²] - E[L]², 0)`。历史不足 4 帧时，改用当前帧 7×7 深度 / 法线双边邻域估计方差，并按 `4 / sampleCount`
  增强刚暴露区域的滤波。空间统计先减去中心亮度再计算矩，避免 HDR 近似常量区域的大数相减产生假方差。
- 空间方差读取当前帧的原始输入，时域统计读取上一帧历史。

## 多级滤波

- 五级 à-trous 采用步长 1、2、4、8、16 的 5×5 B3-spline 核。
- 以线性深度的局部梯度、世界法线和亮度差控制边缘停止权重；深度梯度优先选择跳变较小的单侧差分，避免把轮廓当作平滑斜面。
- 每级先对方差做 3×3 Gaussian 预滤波，用于亮度权重，几何边缘由后续 5×5 双边滤波保护。传至下一级的方差使用
  `Σ(w² × variance) / (Σw)²`，颜色使用 `Σ(w × color) / Σw`。
- 颜色历史由第一级滤波回写，亮度矩历史由重投影阶段更新。
- 最后一级在同一 dispatch 中完成滤波、乘回 albedo 并直接添加 emission。
- 邻域采样限制在当前 viewport 和图像范围内，表面滤波使用深度有效的像素。

## 资源与限制

两路颜色历史、两路亮度矩 / 样本数历史及共享深度 / 法线历史各使用双层 ping-pong。颜色与方差使用 RGBA32F（RGB 为颜色，A
为方差），避免 HDR 方差超出半精度范围；最终合成输出 RGBA16F。

中间颜色 / 方差交替复用两组贴图，其中一组为重投影输出，通过 RenderGraph 资源版本表达覆盖依赖。亮度矩输出直接引用双层历史。

需要检查重投影原始输出时，必须在第二级覆盖该组贴图前读取；测试将原始统计和最终颜色分阶段回读。`RayTracingPerformance` 在
SVGF 开启时测量整条链路，滤波总耗时由同一帧的五级计时相加得到。

图重建或路径反弹次数变化时清空历史。重投影面向静态场景，支持相机运动。

几何一致性检查基于深度和几何法线。运动物体、相近深度的表面切换，以及由法线贴图 / 粗糙度产生的高频反射细节，可能出现拖影或过度平滑。

针对性的 GPU 检查位于 [SVGFReprojectionCheck.cpp](../test/SVGFReprojectionCheck.cpp)
，覆盖时域统计、方差切换、噪声降低、颜色历史回写、几何 / viewport 边缘、emission 直接合成、多帧在途、albedo 解调 /
乘回、黑色通道及高频纹理恢复。
