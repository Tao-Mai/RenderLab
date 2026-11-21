# SVGF

实时图的 `svgf` 参数控制 `RayTracingPass` 的 `splitLighting` 属性和 `SVGFPass` 的接入。`SVGFPass` 内部依次执行重投影和五级滤波，由最后一级合成 `Filtered image` 输出。

ReSTIR 的 `color` 连接到 `SVGFPass.Color`。启用 SVGF 时，两层纹理分别存储直接光和间接光。关闭 SVGF 时，`color` 使用单层纹理输出合并后的颜色。其余输入为 `LinearZ`、`WorldNormal`、`Emission` 和 `Albedo`。

实现依据：[Schied et al., Spatiotemporal Variance-Guided Filtering, HPG 2017](https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/)
。直接光与间接光独立处理，共享深度、法线与几何权重。

## Albedo 解调

光追 Pass 在 SVGF 开启时输出主命中表面的 `saturate(diffuseColor + specularF0)`，背景与直接可见的面光源使用中性白色。重投影先将当前
direct / indirect 除以该像素的 albedo，颜色累积、亮度一阶 / 二阶矩、时域 / 空间方差及后续五级滤波均在解调后的空间中计算。7×7
空间方差使用每个邻域像素各自的 albedo。

最后一级将两路滤后结果相加，乘回当前像素的 albedo，再直接添加当前帧 emission。颜色历史保存解调后的结果。

黑色 / 极暗通道使用最小值 0.01，除法和乘回统一采用 `factor = max(albedo, 0.01)`，保留这些通道中的镜面光照。最终颜色为
`emission + (filteredDirect + filteredIndirect) * factor`，使用当前帧 albedo 恢复纹理细节。

## 重投影与统计

- 根据当前线性深度重建世界位置，投影至上一帧；每个双线性采样点单独校验深度、法线和 viewport，再对有效权重归一化。没有有效双线性采样点时，在
  3×3 邻域查找匹配表面；仍无匹配则丢弃历史。
- 直接光、间接光分别累积颜色与亮度的一阶 / 二阶原始矩，共用几何判定和样本数。样本数不超过 `historyLength` 时使用 `1 / sampleCount`，超过后使用
  `Alpha`，首次或历史失效时使用权重 1。
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

图内的历史资源为 `Previous Linear Z and Packed Normal`、`Previous Lighting` 和 `Previous Moments`。深度使用 32 位浮点，法线沿用 16 位浮点量化。每帧的两路颜色与亮度矩各占一个数组层，时域历史在两组层之间交替写入。`Previous Moments` 使用 RG32F，颜色与方差使用 RGBA32F，最终合成输出 RGBA16F。

SVGF 在编译时创建样本数历史和中间纹理。滤波交替复用重投影结果与一组临时纹理，每轮计算前同步本轮读写。

需要检查重投影原始输出时，在第二级覆盖该组贴图前读取。滤波总耗时由同一帧的五级计时相加得到。

图重建或路径反弹次数变化时清空历史。重投影面向静态场景，支持相机运动。

几何一致性检查基于深度和几何法线。运动物体、相近深度的表面切换，以及由法线贴图 / 粗糙度产生的高频反射细节，可能出现拖影或过度平滑。
