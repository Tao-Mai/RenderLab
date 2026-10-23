# Descriptor Set 管理

## DescriptorPool

渲染 Set 统一从 Device Pool 分配，容量如下：

| maxSets | Sampled Image | Sampler | Uniform Buffer | Storage Buffer | Storage Image | Acceleration Structure |
|--------:|--------------:|--------:|---------------:|---------------:|--------------:|-----------------------:|
|     128 |           512 |     128 |             32 |             64 |            32 |    8（启用光追支持时） |

* Set 由持有者以 RAII 管理，Pool 开启 `eFreeDescriptorSet`，支持单独释放。
* Device 在 shutdown 时销毁 Pool。
* ImGui 的 Pool 由 backend 管理，容量参数为 64。

实现：[Device.cpp](../src/render/device/Device.cpp)。

## DescriptorSetLayout

描述各 Binding 的资源类型、数量和 Shader 阶段。

公共 Layout 在 Device 初始化时预创建并持有；私有Layout 由 Pass 在 setup 或其管线初始化函数中创建并持有。

公共布局集中于 [DeviceLayouts.cpp](../src/render/device/DeviceLayouts.cpp)
，详细定义见 [RenderLayout.md](RenderLayout.md)。

## DescriptorSet

公共 Set 按 FIF 分配，供多个 Pass 复用。

| 公共 Set    |            编号 | 资源                                                                                          |
|-------------|----------------:|-----------------------------------------------------------------------------------------------|
| Scene       |               0 | Binding 0：`SceneUniforms` UBO，包含相机矩阵和相机位置                                        |
| Lighting    |               1 | 光照参数 UBO、光源 SSBO、ShadowData与ShadowMap、比较与线性采样器、环境图、BRDF LUT、Sheen LUT |
| Material    |               2 | Binding 0：全局材质 SSBO；Binding 1：128 个纹理；Binding 2：32 个独立索引 sampler             |
| Editor View | 0（编辑器布局） | Binding 0：View UBO，供光源标记、拾取和编辑器深度绘制使用。                                   |

私有 Set 由各 Pass 持有

Scene、Lighting、Editor View Set 由 FrameData 持有并写入，FrameData 只保留逐帧 UBO 和绑定状态。Renderer 管理光源 Buffer handle、环境纹理、LUT 引用和阴影 sampler；ShadowPass 管理阴影 CPU 数据和 Buffer handle。图编译后写入共享 Buffer 与阴影视图绑定，环境图切换时更新纹理绑定。

## Descriptor Set 写入时机

| 时机              | 写入内容                                                                                                                                                                  |
|-------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 初始化            | Scene、Lighting、Editor View 的 UBO 与 Material 的 fallback 数组；Graph compile 分配共享 SSBO 后写入 Lighting Buffer 绑定，私有 Set 调用 `writeDescriptorSet()` 写入。 |
| 渲染 Feature 改变 | 阴影尺寸、过滤模式、渲染路线等配置改变时，更新受影响的 Binding；Graph 重建后重新写入私有 Set。窗口尺寸引起的资源重建同样处理。                                            |
| 准备场景          | 新纹理或 sampler 加入材质数组、环境图切换、Buffer 扩容、TLAS 替换时，更新对应 Binding。                                                                                   |

`MaterialTable` 用持久的 `AssetID → 数组索引` 哈希表分别记录纹理和 sampler，命中时复用索引，缺失时由
`RenderResourceManager` 加载并追加槽位。

材质 SSBO 索引同样持久缓存：普通材质以 AssetID 为键；override 材质以原始 AssetID 和 Object ID 为键，并保留组件与 submesh 序号以区分独立 override。参数变化时更新原槽位，各 FIF 使用相同索引。

`RenderData.materialIndices` 按 DrawItem 顺序记录对应的 SSBO 索引；`MaterialTable` 按该索引提供 CPU 绘制信息（透明模式、双面、Shader 及材质扩展开关）。DrawItem 列表通过只读 span 引用上层提交。

各 FIF 的 Material Set 在自身 Fence 完成后补写新增槽位；材质参数、相机和光源参数通过 Buffer 内容更新。

材质、灯光、阴影与光追命中 SSBO 由 Graph 跨帧共享，内容经各 FIF 的 staging pool 上传。普通内容更新保持 Descriptor 不变；容量增长后，各 Set 根据 Buffer 的资源版本更新绑定。
