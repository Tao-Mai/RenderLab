graph的三个阶段：setup、compile、execute

setup，graph根据feature，有选择性的调用pass进行setup，pass内部也要根据feature确定输入依赖或输出

compile，graph根据当前所有pass的拓扑图，剪枝，分配资源，不重叠的资源可以重复利用。

execute，根据依赖在特定的地方插入barrier或semaphore就行

所以一个pass的局部连接，以及feature导致的局部连接应该预先知道。 graph解决的是全局上，剔除输出不依赖的那条路径，以及资源分配和重用、barrier插入、状态转换

barrier是根据资源的RAW WAW WAR决定了，和图中是否相邻没有关系

pass和graph不是用户和服务的关系，而是一个整体，graph负责优化而已

PipelineLayout 全局不变，代码写死，预创建好，用的时候再取。pipeline，可以根据自己需要
存一个或者运行时getorcreate，主要看feature变的时候pipeline会不会变就行了

setup 阶段

1. pass内部应该根据输入handle的是否无效情况进行调整
2. use 得到完整access集合 创建node，获取handle依赖
3. handle无效时不会出现依赖，所以前面feature关闭时也不会导致依赖到无效node

compile阶段

1. 从固定输出处，根据依赖建立双向连接
2. 根据write handle将依赖该handle的所有节点作为前置节点建立双向连接。
3. 拓扑排序，获得node的执行顺序，顺便检查是否有环
4. 无连接的节点直接culling
5. merge access 和 推导usage

拓扑排序要按pass为节点排，而不是handle
