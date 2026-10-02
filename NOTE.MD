不要修改，这是我自己的笔记

UBO

1. 存储很小，直接每个frame in flight一份
2. 一个CPU最新版，然后每帧copy最新的给当帧UBO
3. 变化时才更新，TODO

SSBO

1. 现在light也做成每个frame in flight一份，之后再考虑
2. CPU最新版，每帧copy全部。之后可能考虑部分版本更新..

绘制，CPU Driven

1. 现在目前是在fence之后再prepare数据，然后scene pass draw，之后可以在fence之前做，放到ecs的render system
2. 透明物体绘制：绘制时根据相机位置从远到近排序再绘制。 TODO 待优化


input处理
回调函数生成input event数组
tick时
1. 遍历input event数组，press/release会将目标命令置位，并且input set会记录最后一个action
2. 遍历hold command，对应input set为press的则置位




