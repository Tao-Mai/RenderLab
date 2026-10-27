# PBRT 读取前端来源

固定版本：[mmp/pbrt-v4 b4ce9687e6c695f5582997c61b0c66cf064bdb4a](https://github.com/mmp/pbrt-v4/tree/b4ce9687e6c695f5582997c61b0c66cf064bdb4a)。许可证保存在 [LICENSE.txt](LICENSE.txt)。

## 提取范围

- `Parser.h/.cpp` 来自 `src/pbrt/parser.h/.cpp`：Tokenizer、参数解析、指令分派、ParseFiles 与 ParseString。
- `Scene.h/.cpp` 来自 `src/pbrt/scene.h/.cpp`：场景实体与 BasicSceneBuilder，保留属性作用域、变换、资源命名、实例和 Import 的状态处理。
- `Support.h/.cpp` 提供 CPU 宿主绑定：双精度 GLM 变换、参数容器、文件读取、gzip、UTF-8 名称与诊断。参数优先级遵循 `paramdict.h`；相机中间时刻的变换计算遵循 `cameras.cpp` 和 `util/transform.cpp`。
- `NamedSpectra.inc` 提取自同版本 `src/pbrt/util/spectrum.cpp` 的 `namedSpectra` 表。

## RenderLab 修改记录

- BasicScene 接收端收集完整数据描述，替换渲染器、材质、纹理和几何工厂调用。
- 移除格式化、旧格式升级、命令行渲染配置、GPU、统计输出和并行任务依赖。Import 保留独立 builder 与合并语义，改为同步解析。
- 解析会话统一拥有 ParsedParameter；BasicScene 持有 Import builder，使转换缓存存活到导出结束。文件位置拥有文件名字符串；实体直接拥有名称字符串。
- Option 保留原始名称、值与位置；新增 renderFromWorld 导出。ParameterDictionary 保留显式参数及继承参数的官方优先级。
- 移除 MergeImported 中与 lock_guard 重复的手动 unlock；清理合并后的对象状态。
- Tokenizer 使用可移植文件读取和 zlib gzip 解压。依赖搜索目录保持为入口文件目录，源文件列表由宿主读取函数收集。
- 修正 Windows 32 位 long 对大整数形式浮点数的截断；允许完整 int32 负数范围；拒绝未完整解析或非有限的数值。
- 变换缓存按矩阵位模式复用对象；导出时检查有限矩阵、可逆性和运动时间顺序。资源引用名称按 NFC 规范化，参数值保留原始字符串。
- 诊断经回调交给 RenderLab CHECK / 日志。上游宏和类型仅在独立 CPU 目标内部使用。

## 随附依赖

| 目录 | 上游与固定版本 | 许可证 |
| --- | --- | --- |
| `double-conversion` | [google/double-conversion 101e1ba89dc41ceb75090831da97c43a76cd2906](https://github.com/google/double-conversion/tree/101e1ba89dc41ceb75090831da97c43a76cd2906) | [BSD](double-conversion/LICENSE) |
| `utf8proc` | [JuliaStrings/utf8proc d7bf128df773c2a1a7242eb80e51e91a769fc985](https://github.com/JuliaStrings/utf8proc/tree/d7bf128df773c2a1a7242eb80e51e91a769fc985) | [MIT / Unicode](utf8proc/LICENSE.md) |

依赖源码保持原样。gzip 使用项目构建环境提供的 zlib。
