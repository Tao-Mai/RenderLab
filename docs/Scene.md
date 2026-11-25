# 场景

[项目首页](../README.md)

| 类型 | 职责 |
| --- | --- |
| Scene | 持有场景 ID、相机、对象列表、解析光源列表和可选环境图 |
| Object | 持有名字、变换和网格引用 |
| AnalyticLight | 持有名字、变换、类别、开关及光照参数 |
| Camera | 持有位置、朝向、投影与编辑器导航参数 |

JSON 根字段为 `id`、`camera`、`objects`、`analyticLights` 和可选的 `environmentMap`。对象通过 `meshId` 引用网格，渲染使用网格中各子网格绑定的材质。

SceneManager 从资产载入预览场景。GpuScene 从当前场景准备子网格、材质、解析光源和加速结构数据。

场景列表以只读方式显示对象和光源名称。相机导航作用于当前预览。
