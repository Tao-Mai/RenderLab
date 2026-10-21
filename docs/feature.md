# 功能清单

[项目首页](../README.md) · [项目架构](architecture.md)

## 渲染路线

- 前向渲染：深度预通道、不透明 / Alpha Mask、天空盒、透明物体排序绘制。
- 延迟渲染：G-buffer、全屏光照；透明物体仍走前向。
- 实时光追：基本 PBR、多次反弹、直接光采样与可选时域累积。
- 离线路径追踪：独立 Shader、单帧在途、渐进式采样与平均输出。

## 材质

- Metallic-Roughness PBR：Lambert、GGX、Smith、Schlick Fresnel。
- Base Color、Metallic、Roughness、Normal、AO、Emissive 贴图。
- MikkTSpace 切线空间法线映射、可调 Normal Scale。
- 自发光强度、Alpha Mask / Blend、双面材质。
- Clearcoat：独立颜色层之上的涂层反射、粗糙度与法线；前向路线支持。
- Sheen：Charlie BRDF、方向反照率能量缩放、环境光积分；前向 / 延迟支持。
- 材质覆盖、参数编辑、缺失纹理的统一 fallback。

各向异性、Transmission、Volume、IOR、Specular、Iridescence、Dispersion、Unlit、Diffuse Transmission 已支持描述、导入与编辑，尚未实现对应着色。详见 [材质展示](material_gallery.md)。

## 光照与环境

- 方向光、点光源、矩形面光源、聚光灯；按类型分组上传。
- 禁用光源不参与照明上传，编辑器仍保留其标记。
- 局部光源使用发光强度，方向光使用照度；矩形面积改变时保持整体法向发光强度。
- HDR / EXR 环境图导入、Cubemap 转换、漫反射辐照度与 GGX 预过滤。
- IBL 独立开关；关闭后使用可调环境系数，且不绘制天空盒。
- 环境图保存在场景中，BRDF / Sheen LUT 为内置资产。

## 阴影

- 点光源：Cubemap 阴影、16 点泊松 PCF。
- 聚光灯：透视阴影、16 点泊松 PCF。
- 矩形面光源：中心透视近似、矩形 Blocker Search 与 PCSS。
- 方向光：1～4 级稳定 CSM、均匀 / Practical 划分、级联混合、8 点 PCF。
- CSM 稳定化：包围球、Texel Snapping、Depth Clamp、匹配的五平面遮挡物剔除。
- 可调阴影分辨率、Bias、采样半径、Nearest / Linear 与逐像素旋转。

详见 [阴影概览](shadow.md) 与 [CSM](shadow/csm.md)。

## 光追采样与累积

- 漫反射 / GGX 混合 PDF、GGX VNDF 方向采样。
- 点光源：按强度与距离加权，流式选择一个光源并补偿选择概率。
- 面光源：投影面积、强度、距离加权选择，再进行面内采样。
- NEE 与 BRDF 命中面光源的 MIS，三角形发光面参与光追求交。
- 方向光可见性射线，俄罗斯轮盘终止。
- 实时：线性深度重建位置、历史重投影、深度 / 法线拒绝、前期算术平均与后期 EMA。
- 历史资源：颜色均值 / 样本数、深度、法线、亮度一阶 / 二阶矩。
- 离线：Owen 扰乱 Sobol 像素内采样、FP32 总和缓冲、按样本数平均。
- 离线场景、相机、材质或光源变化时重新累积。

当前时域处理仅是 SVGF 的前半部分，未实现方差引导空间滤波；光追尚不支持聚光灯、环境照明及完整材质扩展。

## 后处理

- RGBA16F HDR 颜色缓冲。
- 手动 EV100 曝光与曝光补偿。
- 可切换 Tone Mapping。
- Scattering Bloom：分离式高斯降采样、逐级散射混合，可调强度、Scatter、高斯半径与层数。
- HDR、Bloom 等渲染选项可运行时切换并持久化。

## 资产与场景

- 类型化资产 ID：Mesh、Material、Texture、Shader、Scene、EnvironmentMap、Sampler。
- glTF / GLB 使用 TinyGLTF，其余模型格式使用 Assimp。
- 网格导入与完整场景导入：节点变换、共享网格、子网格材质、相机。
- 导入预览、缩放、ID 编辑与自动查重。
- 普通贴图完整 Mipmap、Linear / sRGB 区分、HDR / EXR。
- 内置立方体、球体、平面、箭头、基础纹理、Sampler 与 LUT。
- 网格局部 AABB；Renderer 进行主视图与各类阴影视图剔除。
- Actor / Component 场景、同类型多组件、C++26 静态反射序列化。
- JSON 场景保存、场景切换丢弃未保存修改、关闭时记录默认场景选择。

## 编辑器与性能观察

- ImGui Dock 布局：场景树、正方形视口、属性与渲染设置。
- GPU ID 拾取、Transform Gizmo、原地重命名、新建 / 删除对象。
- 独立编辑器相机：右键导航、自由飞行模式、速度与投影设置。
- 光源参数与材质覆盖编辑，独立光源标记 Shader。
- 窗口尺寸与渲染配置关闭时持久化，各渲染分组可恢复默认值。
- CPU Scope、GPU Pass 时间戳、Draw / 三角形计数、剔除统计。
