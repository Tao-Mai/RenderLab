# 功能清单

[项目首页](../README.md) · [项目架构](Architecture.md)

## 渲染

- [MinimalPathTracer](MinimalPathTracer.md) 生成 HDR 场景颜色，由渲染图选择后处理。

## 材质

- Metallic-Roughness 与 Specular-Glossiness 工作流：统一漫反射、GGX、Smith、Schlick Fresnel；支持 Specular 与 IOR。
- Base Color、Metallic、Roughness、Normal、Emissive 贴图。
- MikkTSpace 切线空间法线映射、可调 Normal Scale。
- 自发光强度、Alpha Mask、双面发光。
- 缺失纹理的统一 fallback。

Clearcoat、Sheen、各向异性、Transmission、Volume、Iridescence、Dispersion、Unlit、Diffuse Transmission 支持资产描述与导入。详见 [材质展示](MaterialGallery.md)。

## 光照与环境

- 方向光、点光源、矩形面光源、聚光灯；按实例上传。
- 启用的光源上传供照明使用。可见发光面来自发光几何。
- 局部光源使用发光强度，方向光使用照度；矩形面积改变时保持整体法向发光强度。
- HDR / EXR 环境图导入、Cubemap 转换和辐亮度采样。

## 光追采样

- 每像素每帧追踪一条相机路径。每次散射进行一次普通光源采样和一次 BSDF 采样。
- 几何遮挡通过可见性射线查询。环境光和发光三角形与 BRDF 采样进行多重重要性采样。
- 使用俄罗斯轮盘终止长路径。渲染图输出包含直接光、间接光和相机直接看到的发光。

## 后处理

- RGBA32F 路径追踪输出。
- 手动 EV100 / 亮度直方图自动曝光、明暗适应和曝光补偿。
- 可切换 Tone Mapping。
- [Falcor 后处理](PostProcess.md)：独立可选的 ToneMapper 与 SimplePostFX，含白平衡、星芒、镜头效果和调色。
- Scattering Bloom：分离式高斯降采样、逐级散射混合，可调强度、Scatter、高斯半径与层数。
- HDR、Bloom 等渲染选项可运行时切换并持久化。

## 资产与场景

- 类型化资产 ID：Mesh、Material、Texture、Shader、Scene、EnvironmentMap、Sampler。
- glTF / GLB 使用 TinyGLTF，其余模型格式使用 Assimp。
- 网格导入与完整场景导入：节点变换、共享网格、子网格材质、相机。
- 命令行导入、缩放、ID 与自动查重。
- 普通贴图完整 Mipmap、Linear / sRGB 区分、HDR / EXR。
- 内置立方体、球体、平面、箭头、基础纹理与 Sampler。
- JSON 场景加载、场景切换以及关闭时记录当前场景。
- Scene 直接存储 Object、AnalyticLight 和 Camera。

## 编辑器与性能观察

- 只读列表显示场景的对象和光源。
- ImGui Dock 布局：场景树、正方形视口、属性与渲染设置。
- 预览相机支持右键导航和自由飞行模式。
- 窗口尺寸与渲染配置关闭时持久化，各渲染分组可恢复默认值。
