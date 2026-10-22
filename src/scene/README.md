# 场景模块

[项目首页](../../README.md) · [项目架构](../../docs/Architecture.md)

场景使用 Actor 持有 Component 的对象模型。SceneManager 管理运行时场景；编辑器相机独立于 Actor / Component。

## 对象与组件

- AActor 持有组件数组，默认 Tick 遍历组件，子类可覆盖自身行为。
- 支持多个同类型组件；`getComponent<T>()` 返回首个，`getComponents<T>()` 返回全部。
- 构造函数用 `addDefaultComponent` 组装默认组件，Actor 初始化后统一绑定 Owner 并检查依赖。
- 运行时 `addComponent` 添加后立即初始化，Owner 为非拥有指针。
- AStaticMesh 组合 Transform 与 RenderComponent；ALight 使用具体的灯光组件。
- Point / Directional / RectArea / Spot 分别使用子类，仅在提交渲染数据时映射为 GPU 光源类型。

编辑器相机的位置与投影保存在 SceneAsset 中，但相机实例与导航状态归 Editor，不占用场景对象。

## 反射与持久化

SceneManager 初始化时注册 [SceneTypes.h](SceneTypes.h) 中的类型；Engine 进入 Run 时，由 SceneManager 读取配置中的默认场景 ID 并加载。

- Actor / Component 通过 `ReflectField` 标记持久化字段，继承类沿用该字段选择策略。
- 多态指针保存为 `{"TypeName": {...}}`，空指针为 null；不保存内存地址或编译器 RTTI 名称。
- Owner、拾取 ID、导航状态等运行时数据不参与序列化。
- 场景从资产快照恢复为独立对象，避免运行时修改直接污染资产缓存。
- 切换场景丢弃未保存修改；显式保存才写入场景内容，关闭时只记录默认场景选择。

## 向渲染层提交

Engine 调用 RenderSystem 遍历逻辑上需要渲染的对象，提取资产 ID、变换、材质覆盖与灯光，生成 SceneRenderData。

RenderSystem 不做视锥剔除，不加载 GPU 资源。Renderer 根据主相机与阴影配置计算可见性，RenderResourceManager 再将快照转换为 GPU 渲染表示。下层不接触 Component，新增渲染路线无需修改场景对象体系。

编辑器的组件属性控件、Transform Gizmo 与材质覆盖 UI 留在 editor 模块。

实现入口：[SceneManager](SceneManager.cpp)、[AActor](AActor.h)、[Component](Component.h)、[RenderSystem](system/RenderSystem.cpp)。
