# 阴影

[项目首页](../README.md) · [稳定 CSM](shadow/Csm.md)

Forward / Deferred 共用 ShadowPass 与阴影采样函数。不同光源使用适合其投影的深度图；实时 / 离线光追则通过可见性射线求遮挡，不依赖这些 Shadow Map。

## 算法概览

| 光源 | 阴影表示 | 过滤 |
| --- | --- | --- |
| 方向光 | 1～4 级正交 CSM | 8 点泊松 PCF、级联混合 |
| 点光源 | 六面 Cubemap，Multiview 绘制 | 16 点泊松 PCF |
| 聚光灯 | 单次透视投影 | 16 点泊松 PCF |
| 矩形面光源 | 光源中心透视近似 | 矩形 Blocker Search 与 PCSS |

### PCF

对邻域中的阴影深度分别比较，再平均可见度，缓和边缘锯齿。Nearest / Linear 决定每一次比较采样的过滤，采样半径决定整个核的覆盖范围；可选逐像素旋转减弱固定图案。

### 面光源 PCSS

先搜索遮挡物并估计平均深度，再根据光源尺寸与接收点 / 遮挡物距离估计半影范围，最后在矩形区域中比较采样。矩形采样分别考虑宽高，不将面光源当作圆形光源。

这是基于一张中心投影图的软阴影近似，不等同于对整个发光面的精确可见性积分，复杂遮挡与大半影仍有误差。

### 稳定 CSM

按相机深度分配精度；包围球稳定投影尺寸，Texel Snapping 稳定采样网格，级联混合缓和交界。方向光配合 Depth Clamp 与五平面剔除保留上游遮挡物。

![稳定级联阴影测试场景](images/csm.png)

## 配置与资源

- 编辑器可调整分辨率、Bias、采样半径、过滤模式、旋转与级联参数。
- 阴影开关通过重建图控制，不在 ShadowPass 内用开关提前返回。
- 阴影数据在帧准备阶段计算；只为启用且投射阴影的光源分配索引。
- 固定预留点 / 矩形 / 聚光阴影各 4 盏、方向光 1 盏；超出容量明确报错。
- 深度图跨在途帧共享，由 Graph 同步上一帧最后访问与下一帧首次访问。

代码：[ShadowData](../src/render/feature/shadow/ShadowData.cpp)、[ShadowPass](../src/render/feature/shadow/ShadowPass.cpp)、[采样与比较](../shaders/scene_lighting.slang)、[采样分布](../shaders/sampling.slang)。
