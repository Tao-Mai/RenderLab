# Asset 模块依赖

```mermaid
flowchart TB
    CLI[Main / Import]
    App[render / editor]
    DescMgr[AssetDescManager]
    DataMgr[AssetDataManager]
    Builtin[BuiltinAssets]
    Serde[JsonIo / ApplyOptionalFields]
    CLI --> DescMgr
    CLI --> DataMgr
    App --> DescMgr
    App --> DataMgr
    App -->|引用 ID| Builtin
    DescMgr --> Builtin
    DescMgr --> Serde
    DataMgr --> Builtin
```

## 模块说明

### AssetID

用字符串表示，并且充当名字和文件名，同一资产类型内唯一
### AssetDesc
描述资产信息

### AssetDescManager

AssetDesc管理，提供查询和保存接口 。启动时扫描所有desc并在内存中缓存。检测到builtinID会从BuiltinAsset中获取Desc

### AssetDataManager

二进制数据层。按 `MeshDesc` / `TextureDesc` 读写几何与贴图像素：`Source::File` 走 `assets/binary/{geometry,texture}/`，
`Source::Builtin` 调 `BuiltinAssets` 的 data 接口。Import 写入、RenderResourceManager 读取，都不绕过它碰磁盘。

### BuiltinAssets

被动内置资产集合。对外只暴露 `Mesh` / `Material` / `Texture` 的 `AssetId`，供场景、Import、灯光标记等引用。desc / data 生成函数为
private，仅 `AssetDescManager` 与 `AssetDataManager` 以 friend 调用；无实例、无 `init` 预注册。实现拆在 `builtin/` 下按类型分文件。

### JsonIo / ApplyOptionalFields

序列化与字段工具，不算业务流程模块。`JsonIo` 给 DescManager 做 JSON 读写；`ApplyOptionalFields` 按 optional 有值才覆盖，给材质
override 合并用。两者都不持有资产状态。

### Main / Import 与 render / editor

Import（经 `AssetImporter`）解码源文件后，向 DataManager 写 binary、向 DescManager `save` 描述。render / editor 只读两侧
Manager；需要默认白贴图、内置网格等时引用 Builtin 的 ID，不调用其生成接口。
