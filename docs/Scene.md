# 场景

[项目首页](../README.md)

| 类型 | 职责 |
| --- | --- |
| Scene | 持有场景 ID、相机、对象列表、解析光源列表和可选环境图 |
| SceneLayer | 按场景 ID 关联基础场景，提供额外的对象和解析光源 |
| Object | 持有名字、变换和网格引用 |
| AnalyticLight | 持有名字、变换、类别、开关及光照参数 |
| Camera | 持有位置、朝向、投影与编辑器导航参数 |

JSON 根字段为 `id`、`camera`、`objects`、`analyticLights`，以及可选的 `environmentMap` 和 `defaultLayer`。对象通过 `meshId` 引用网格，渲染使用网格中各子网格绑定的材质。

解析光源的 `Point` 类型同时表示全向点光源与聚光灯。`openingAngle` 是弧度制的外锥半角，π 表示覆盖整个球面；`penumbraAngle` 是锥边缘向内过渡的角宽，0 表示硬边。两个角度字段都必须写入资产。点光源按逆平方衰减，不使用距离截断。

SceneManager 从资产载入预览场景。GpuScene 从当前场景准备子网格、材质、解析光源和加速结构数据。

场景列表以只读方式显示对象和光源名称。相机导航作用于当前预览。

场景层的 JSON 放在资产目录的 `SceneLayer/` 下，字段为 `id`、`baseScene`、`name`、`objects` 和 `analyticLights`。在编辑器的 `Scene layer` 中切换方案会替换当前层并保留相机。编辑器配置按场景记录选中的层，重新打开场景时优先恢复记录，首次打开时使用场景的 `defaultLayer`。未配置默认层的场景可用 `No layer` 清除当前层及其记录。
