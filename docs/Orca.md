# NVIDIA ORCA 资产

本地已从 [NVIDIA ORCA](https://developer.nvidia.com/orca) 获取全部五组原始资产，保存于 `resource/orca/`。共 25 个 FBX，导入为 9 个完整场景和 16 个模型预览，关联 Mesh 与 StandardMaterial 资产。场景下拉框仅列出完整场景。

| 资产组 | 原始压缩包 | FBX / Scene 数 | 场景 ID |
| --- | --- | --- | --- |
| [Amazon Lumberyard Bistro](https://developer.nvidia.com/orca/amazon-lumberyard-bistro) | Bistro_v5_2.zip | 3 | `orca_bistro_exterior`、`orca_bistro_interior`、`orca_bistro_interior_wine` |
| [NVIDIA Emerald Square](https://developer.nvidia.com/orca/nvidia-emerald-square) | EmeraldSquare_v4_1.zip | 2 | `orca_emerald_square_day`、`orca_emerald_square_dusk` |
| [SpeedTree](https://developer.nvidia.com/orca/speedtree) | SpeedTree_v2.zip | 16 | `orca_speed_tree_*`，8 种植物各含高、低模 |
| [UE4 Sun Temple](https://developer.nvidia.com/ue4-sun-temple) | SunTemple_v4.zip | 1 | `orca_sun_temple` |
| [BEEPLE Zero-Day](https://developer.nvidia.com/orca/beeple-zero-day) | ZeroDay_v1.zip | 3 | `orca_zero_day_measure_one`、`orca_zero_day_measure_seven`、`orca_zero_day_measure_seven_colored_lights` |

五份压缩包共 3,094,979,394 字节，位于 `resource/orca/downloads/`。Bistro 复用了相邻 Falcor 工作区中与 NVIDIA 当前下载版本、文件大小相符的原始包，复制到此目录后解压；其余四组通过 NVIDIA 官方下载入口获取。各包的 LICENSE、README 和原始场景脚本保留在解压目录中。

## 导入结果

- 25 个 Scene 资产：9 个 `Environment`、16 个 `ModelPreview`，共 40,774 个对象实例。只有 `Environment` 进入场景下拉框。
- 36,378 个 Mesh，保留源文件的实例引用与节点变换。
- 1,237 个 StandardMaterial，均采用 ORCA 的 MR 参数编码。
- 引用 2,706 个纹理资产，包括环境图；DDS 保留 BC 压缩格式和原有 Mipmap。

下载来源、大小、SHA-256 记录在 `resource/orca/downloads.json`。每个 FBX 的来源、场景 ID、材质 ID 和导入统计记录在 `resource/orca/imports.json`；汇总见 `resource/orca/summary.json`。

## FBX 导入约定

参考 Falcor AssimpImporter，FBX 默认使用 MetalRough；BaseColor 和 Emissive 按 sRGB 读取，Specular 和 Normal 按线性读取。Specular 的 G 为粗糙度，B 为金属度，纹理替代相应常量值。材质名中的 `.DoubleSided` 标记开启双面，颜色 Alpha 用于遮罩；OBJ 单独采用 SpecGloss 与 Phong 指数转换。

外部纹理共用普通图像 / DDS 导入流程。无效数据先由 Assimp 清理，零面积三角形在导入入口移除，再生成缺失的面法线并合并相同顶点。只含退化面的网格不创建实例，有效小三角形仍保留。切线由 MikkTSpace 生成，反射变换同步调整绕序。

导入静态初始姿态，保留源相机的视场和世界朝向。方向光、点光及聚光灯的局部坐标先与节点世界变换相乘，再写入项目的解析光源数据。

## 随包场景设置

本次批量导入还读取了源 `.pyscene` 中的环境图、发光倍率和可支持的表面材质参数，将结果写入普通资产：

- Bistro Exterior：`san_giuseppe_bridge_4k.hdr`，环境强度 10 烘焙到该环境资产。
- Bistro Interior Wine：发光倍率 1000，写入玻璃、水、冰和酒液的粗糙度、金属度、IOR、镜面透射及双面参数。
- Emerald Square Day / Dusk：分别使用 `symmetrical_garden_4k.hdr`、`night_sky.hdr`；Dusk 发光倍率为 5。
- Sun Temple：`SunTemple_Skybox.hdr`，发光倍率 1000。

环境导入记录见 `resource/orca/environments.json`。Wine 脚本中的体吸收和嵌套介质优先级仍超出当前渲染器能力，具体未应用属性记录在对应导入条目中。SpeedTree 原始文件不含相机和光源，其预览明确标为 `ModelPreview`；预览相机由包围盒拟合，植物 Mesh 可直接放入已有照明场景。

## 验证

Release 构建与 glTF / OBJ 材质检查通过。全部 ORCA 场景的网格、材质、纹理引用和二进制尺寸均已校验。Bistro Exterior、Emerald Square Dusk、Sun Temple、Zero-Day Measure One 通过 MinimalPathTracer 无窗口渲染检查。
