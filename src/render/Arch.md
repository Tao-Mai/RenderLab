# RenderGraph

[渲染模块](README.md) · [项目架构](../../docs/Architecture.md)

Renderer 决定加入哪些 Pass，Pass 声明资源访问，Graph 负责依赖、分配、状态转换与执行。它不解释场景、材质或拾取结果。

## Setup → Compile → Execute

### Setup：描述工作

- Renderer 按配置串接具体 Pass，通过类型访问，避免字符串节点和业务类型映射。
- Pass 接收 Image / Buffer Handle 或上游 Output，返回自己的 Output。
- 模板使用 C++26 静态反射收集参数中的资源依赖；Graph 头文件不引入具体 Pass。
- 资源记录生产者。write 创建新版本 Handle 并指向旧版本，同一资源的版本共享底层存储。
- EndPass 声明最终呈现与 CPU 回读出口。

### Compile：建立执行图

- 从终点回溯有效生产者，建立依赖并补充覆盖写之前的读取约束。
- 拓扑排序，剪去未参与最终结果的节点；重复写和循环在所有构建中检查。
- 合并资源 Usage、创建物理资源；资源就绪后调用保留节点的 prepare 完成绑定。
- 保存执行顺序，场景对象变化不要求重新编译图。

### Execute：录制命令

- 按编译顺序处理资源访问、插入 Barrier，再执行 Pass 回调。
- Bloom 由多个滤波节点组成：高斯横向降采样、纵向滤波、逐级上采样和最终合成。中间图由 Graph 统一分配，状态转换与跨帧同步也由 Graph 处理，Bloom 内部不插入 Barrier。
- EndPass 应用 Present / HostRead 等出口状态。
- 场景绘制和计算使用原生 Vulkan 调用；Graph 不包装每一次 Draw。
- 拾取结果由 Renderer / Editor 消费，Graph 只处理图像、回读缓冲与同步。

## 资源与跨帧同步

| 资源 | 管理方式 |
| --- | --- |
| 深度、阴影图、G-buffer、HDR 中间结果 | Graph 持有，全局一份，通过 `getImage(handle)` 访问 |
| CPU 回读缓冲 | 显式 PerFrame，按在途槽位分配 |
| 交换链图像 | Graph 借用，按获取的交换链图像选择 |
| 时域历史 | Pass 持有并导入 Graph，双层 Ping-pong |
| Mesh / Material / Texture | RenderResourceManager 缓存，不作为普通图临时资源 |

Shared 资源的访问状态在帧间保留：下一帧首次访问与上一帧最后的读写建立同步；连续只读访问合并阶段和访问范围。当前运行在同一队列，状态粒度为整个 Image / Buffer。

Image 自己缓存 ImageView，Pass 获取 Image 后创建所需 View；Graph 不维护 View 缓存。借用资源的所有者负责保证 GPU 使用期间的生命周期。

## 重建边界

- 渲染路线、HDR、IBL、阴影开关或资源规格等改变时重建图。
- 普通物体变换、材质参数和灯光变化更新帧数据，不重新组图。
- 场景环境图是资源绑定，不是拓扑条件；IBL 开关才决定是否登记 SkyboxPass。
- Resize 时释放旧图资源与 View，重建交换链，再 Setup / Compile。

当前不实现临时资源别名复用、多队列调度或异步 Compute。实现入口：[RenderGraph.h](feature/RenderGraph.h)、[RenderGraph.cpp](feature/RenderGraph.cpp)。
