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
，详细定义见 [render_layout.md](render_layout.md)。

## DescriptorSet

公共 Set 按 FIF 分配，供多个 Pass 复用。

| 公共 Set    |            编号 | 资源                                                                                                                |
|-------------|----------------:|---------------------------------------------------------------------------------------------------------------------|
| Scene       |               0 | Binding 0：`SceneUniforms` UBO，包含相机矩阵和相机位置                                                              |
| Lighting    |               1 | 光照参数 UBO、光源 SSBO、点光 / 聚光 / 面光 / 方向光阴影数据与阴影图、比较与线性采样器、环境图、BRDF LUT、Sheen LUT |
| Material    |               2 | Binding 0：全局材质 SSBO；Binding 1：128 个纹理；Binding 2：32 个独立索引 sampler                                   |
| Editor View | 0（编辑器布局） | Binding 0：View UBO，供光源标记、拾取和编辑器深度绘制使用。                                                         |

私有 Set 由各 Pass 持有

## Descriptor Set 写入时机

| Set | 写入时机 | 写入入口 |
| --- | --- | --- |
| Scene、Editor View | FrameData 初始化；光源 Buffer 扩容后重写 | `FrameData::bindBuffers()` |
| Lighting：Buffer 绑定 | FrameData 初始化；光源 Buffer 扩容后重写 | `FrameData::bindBuffers()` |
| Lighting：阴影图与采样器 | Graph compile 完成资源分配后；阴影过滤模式改变时 | `SceneBindings::writeDescriptorSet()` |
| Lighting：环境图与 LUT | SceneBindings setup；环境图切换或刷新绑定时 | `SceneBindings::bindEnvironmentTextures()` |
| Material | Set 首次创建、材质 Buffer 扩容、纹理或 sampler 数组绑定变化时 | `MaterialTable::upload()` |
| 私有 Set | Graph compile 完成资源分配后 | `writeDescriptorSet()` |

私有 Set 的动态绑定随 Pass 执行更新。
