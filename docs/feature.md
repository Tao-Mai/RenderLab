# 功能清单

## 渲染

- Vulkan 前向渲染：深度预通道、主场景、天空盒、光源标记、编辑器叠加。
- 不透明、Alpha Mask、Alpha Blend；双面材质与背面剔除。
- 透明物体按距离从后向前绘制，开启混合、不写入深度。
- HDR 链路可开关：RGBA16F 颜色缓冲、曝光、Khronos PBR Neutral / Reinhard 色调映射、sRGB 输出。
- HDR 开关触发 RenderGraph 重建；编辑器 UI 与坐标轴不参与曝光和色调映射。
- 独立硬件光追路线：BLAS / TLAS、相机主光线、命中后输出材质 Base Color；暂不计算光照和多次反弹。

## 材质与光照

- glTF Metallic-Roughness PBR：Base Color、Metallic、Roughness、Normal、AO、Emissive。
- 直接光 BRDF：GGX、Schlick Fresnel、Smith 可见性、Lambert 漫反射。
- 法线贴图：MikkTSpace 切线、TBN 变换、可调 Normal Scale。
- 双面材质：编辑器勾选、关闭背面剔除、背面法线翻转；深度预通道跟随材质剔除设置。
- Clearcoat：三类贴图、独立法线 / 粗糙度、GGX + Kelemen 清漆层、Fresnel 分层、IBL 高光与底层自发光衰减；编辑器调节强度、粗糙度与法线强度。
- AO 贴图与强度：仅作用于环境光；自发光使用贴图与颜色系数。
- 缺失贴图绑定 fallback 纹理；支持每个 Submesh 的材质覆盖与重置。
- 点光源：距离平方衰减、照射范围；方向光：固定方向照明。
- 矩形面光源：按中心方向、投影面积与距离近似照明；Spot 暂时只有参数编辑与标记。
- IBL：漫反射辐照度、GGX 预过滤环境贴图、BRDF LUT；关闭时改用环境光系数，同时关闭天空盒。

## 阴影

- 点光源：Multiview 一次绘制六面 Cube Shadow Map，16 点泊松圆盘 PCF。
- 矩形面光源：中心透视投影近似，矩形采样 PCSS（Blocker Search、半影估计、PCF）。
- 方向光：基础 CSM，支持 1～4 级 Cascade，按相机深度选择阴影层。
- CSM 配置：分辨率、最大距离、级联划分系数、深度扩展范围。
- 阴影设置：总开关（切换时重建图）、每灯 Cast Shadow、各类型分辨率、Bias、Nearest / Linear 过滤。
- 软阴影设置：采样半径、逐像素旋转；共享采样工具保留 16 / 32 点泊松表。
- 阴影容量：4 个点光源、4 个矩形面光源、1 个方向光。

## 资产

- `MeshAsset`：顶点、索引、Submesh 与材质引用，记录导入来源和缩放。
- `MaterialAsset`：PBR 参数、贴图引用、透明模式和双面设置。
- `TextureAsset`：纹理尺寸、格式、色彩空间、Mip 链与像素数据。
- `ShaderAsset`：编译后的 SPIR-V Shader 资源。
- `SceneAsset`：Actor / Component 数据、编辑器相机配置与环境朝向。
- `EnvironmentMapAsset`：环境辐射、漫反射辐照度、预过滤高光贴图。
- `SamplerAsset`：过滤、寻址、Mip / LOD、各向异性、深度比较与边界参数。
- 带类型的资产 ID、JSON 描述与二进制数据持久化；CPU 资产缓存与 GPU 渲染资源缓存。
- 内置几何：平面、正方体、球体、箭头；内置默认材质、采样器与 fallback 纹理。

## 导入

- TinyGLTF 导入 glTF / GLB 静态网格、节点变换、Submesh、PBR 材质及外部贴图。
- 保留已有切线，缺失时生成 MikkTSpace 切线。
- 网格预览：线框、旋转缩放、尺寸显示、1 米参考物；支持导入缩放与目标尺寸。
- 导入 ID 默认使用文件名，可手动填写；重名时自动补序号。
- 普通贴图：常用 8 位图片、HDR、EXR；支持自动 / Linear / sRGB 色彩空间。
- 普通贴图生成完整 Mipmap 至 1×1，支持非方形和非二次幂尺寸；sRGB 在线性空间滤波，处理 Alpha 权重。
- 环境贴图：经纬图 / OpenEXR Cube 输入、Cubemap 转换、Mip 链、辐照度与 GGX 预过滤烘焙。
- 环境导入支持质量配置、后台处理、进度显示和取消；提供编辑器与命令行导入入口。

## 场景与组件

- Actor 持有 Component，默认 Tick 遍历组件；支持多个同类型组件。
- Component 初始化设置 Owner，并检查依赖组件。
- 内置静态网格、光源、自由飞行相机 Actor；Transform、Render、Light、Camera 与移动组件。
- C++26 静态反射注册类型；按 `ReflectField` 序列化 Actor / Component，支持多态指针。
- 启动加载配置中的默认场景；切换场景丢弃未保存修改，不自动保存场景。
- 手动保存场景；关闭时持久化当前默认场景选择、渲染设置，不自动保存场景内容。
- 编辑器相机独立于 Actor / Component，配置记录在场景资产中。

## 编辑器与交互

- ImGui Docking：左侧对象列表、中央视口、右侧属性栏；微软雅黑字体、低频刷新 FPS。
- 对象列表与视口 GPU Picking 选中物体或光源。
- 按继承关系创建 Actor；支持删除、原地重命名与默认名称编号。
- Transform 数值编辑与 ImGuizmo 局部移动 / 旋转 / 缩放，W / E / R 切换工具。
- 按光源类型显示对应参数；矩形面光源显示 Radiance。
- 光源标记：点光源 / Spot 小球、矩形面光源薄盒、方向光箭头；独立编辑器 shader，在 Tone Mapping 后、拾取前叠加，光栅和光追共用编辑器标记逻辑。
- 编辑网格、Submesh 材质覆盖、相机、渲染路线、IBL、HDR 与阴影参数。
- 相机右键控制视角，WASD + Q / E 移动，Shift 加速，滚轮调整 FOV；V 切换隐藏鼠标自由模式。
- 可配置移动速度、灵敏度、FOV、Near / Far 与输入绑定；忽略 Caps Lock / Num Lock，Esc 退出。

## 渲染与平台基础

- RenderGraph：资源读写版本、依赖建图、终点剪枝、拓扑排序、重复写与循环检查、自动 Barrier。
- 按帧管理 Command Buffer、Fence、Semaphore 和渲染数据；窗口调整大小时重建 Swapchain。
- 缓存 Mesh / Material / Texture / Sampler、Shader、Pipeline、Descriptor Layout 与 Image View。
- Slang 构建时生成 SPIR-V 与反射数据，支持 IBL 和阴影类型编译分支。
- GCC 16.2+、C++26 静态反射；反射驱动 JSON 序列化与配置读写。
- 配置分离：引擎、Renderer、Editor、输入命令；窗口按配置创建并默认居中。
- 启动切换英文输入法，关闭恢复原输入法。
