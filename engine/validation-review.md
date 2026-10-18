# Engine 校验责任与精简记录

本记录对应 2026-10-14 的工作树增量。以本轮开始时的源码快照为基线，保留此前所有已暂存、未暂存及未跟踪的开发内容；没有提交或改动暂存区。

2026-10-15 补充：移除为保证后续分配不失败而提前扩容或占位的步骤；实际插入失败直接返回错误，人物执行器停止普通推进，不为分配失败新增重试或回滚。

## 校验放置原则

- 外部参数、存档内容、可写业务状态、资源失败、容量及算术范围由真正接收或使用它们的边界负责。
- 同次同步操作中已经验证、且未发生修改的结果直接向下传递，内部函数通过明确前置条件使用。
- 跨线程、跨等待或重新取得写入权后，身份、版本、授权及状态可能变化，仍须重新检查。
- 人物与 data 数组在执行期保持稳定。任务池扩容或交换删除后，不能沿用任务指针或哈希槽位，须保存 ID 后重新定位。
- 32 位累计未冻结现实毫秒契约保持不变，0 和 UINT32_MAX 都有效；绝对时刻加法及时间先后检查保留。

## 调用链与责任分配

| 调用链 | 本轮精简 | 仍负责的关键检查 |
| --- | --- | --- |
| `EngineInit → 各模块 Init → storage load → nav/time after` | 初始状态只判断一次；串行生命周期直接发布 INITIALIZING；nav after 不重复检查已初始化 | 公共路径、重入、资源创建、存档、导航完整性及时间采样错误 |
| `EngineRun → main loop → human/timer start → thaw` | 删除重复表示启动完成的三个布尔标志，以生命周期状态决定资源阶段 | STARTING→RUNNING 的 CAS 保留，防止覆盖后台失败；启动回执、恢复检查及失败传播保留 |
| `Freeze/Exit → freezeWorld → timer barrier → human settlement → stop/join` | 内部成功启动后不逐层复查模块是否已启动；仅 RUNNING 路径释放控制与队列 | 冻结屏障、合法结算时刻、执行线程停写、join、保存前快照条件 |
| `EnginePushMsg → queue → main loop → dispatch` | dispatch 使用成功出队的消息，不再次判空 | 队列入口指针、容量、停止状态、锁、消息 ID 上限；dispatch 消息类型 |
| `Authorize/Submit → applyCommand → SleepTask → Store` | 复用已定位人物/data；入池直接接收已验证草案；任务提交后实际写入许可，删除提前扩容、空占位及占位回滚 | 人物、许可、建筑、活动槽、任务容量、版本、时间边界；分配失败先停止执行，再回复命令 |
| `planner request → draft → executor result → Commit` | 私有规划 inbox 使用唯一生产者的固定消息契约；避免二次释放结果载荷 | 混合 inbox 消息形态、请求身份、owner、通道、版本、当前资格及完整草案语义 |
| `advanceHuman → Start/AdvanceSleepTask → needs → wakeup` | 复用人物、活动任务、许可和 data；同刻结算后预测阈值不重新扫描需求；规则时长上限以静态断言保证 | 可写任务/行为语义、需求范围、活动形态、时间先后、绝对到期溢出、执行版本 |
| `StoreRemoveTask → hash delete → vector swap-delete` | 复用首次定位的槽位与下标；删除后仅对换入任务重新查哈希 | 终态、父子/停止引用、归属、版本上限及槽位关联 |
| `wakeup Schedule → timer ScheduleAt → command queue → timer thread → Consume` | 复用人物指针；删除关联表提前扩容，实际 put 失败直接返回；timer 入口统一判空 | 实际分配失败、容量/时间溢出、取消、异步事件身份和版本、生命周期内 timer ID 唯一 |
| `time Freeze → version window → publish` | Freeze 已开启写入窗口，发布阶段不重复置奇数 | 原子字段、版本前后校验、采样窗口与 32 位业务毫秒范围 |
| `storage Init → meta → module/shard → deserialize → reference validate/bind` | 元数据只解析一次计数；统一实体零初始化；删除加载前的数组和 ID map 预留，按需插入，data 指针在对应加载完成后绑定 | JSON 类型/范围、实际分片编号、数量、重复 ID、跨模块引用、分配及 IO 错误 |
| `storage Exit → shards → module meta → root meta → transaction` | 模块 meta 直接返回摘要供根 meta 使用；删除缓存再反查及固定注册表的重复键扫描；内部不重复检查已验证路径 | 停写前提、路径追加长度、磁盘状态、CRC/size、JSON 序列化、事务 IO 失败 |
| `Navigation → segment → path planning → cache → route fill` | 复用已定位分段；完整缓存下删除辅助函数判空和不可达空结果；销毁交由支持 null 的容器接口 | 可写实体状态/分组/下标、邻接一致性、顶点上限、矩阵分配、不可达路径及输出容量 |
| `LogVa/LogMsgN → module filter → private queue → writer` | 将过滤时得到的模块下标放入私有队列，writer 不再扫描模块表；固定内部缓冲不重复判空 | 公共空实例、等级/模块、正文、格式化、资源/IO/同步与停止错误 |
| `common/host/social → getters / persistence references` | common 空表判断由 khashl 承担；social 保存用一次指针身份比较；嵌套反序列化只清零一次 | ID 语义、实际分配、实体身份及可变引用；host/social 生命周期无必要改动 |

## 重点不变量

1. 私有 logger 模块表初始化后只读，writer join 后才销毁，队列中的模块下标在整个消费期有效。
2. 存档加载先完成全部 meta 解析与摘要登记，再读取分片；保存先写全部分片，再按同一注册表生成 meta，期间世界停止写入。
3. Human/Data 两表同序配对；冻结边界确认 `human->data` 正是 StoreValidate 已检查的同索引对象。
4. 同步自建睡眠草案由内部构造函数建立完整有效记录；异步草案重新执行记录和当前资格校验。Store 仍承担实际入池失败。
5. wakeup map 按需插入并检查分配错误；失败交由执行器停止普通推进，不回滚已提交 timer。timer ID 在队列生命周期内不回绕，唯一性仍为内部不变量。
6. nav 缓存只在分配和填充成功后提交；借用者不得修改缓存数组，实体搬迁前仍执行缓存失效。

## 覆盖与复查

实现按十个互不重叠的文件范围并行开展，另行检查 engine 公共入口和 client-hub/engine-remote 的实际调用。模块负责人完成调用点自查后，安排第一轮核心、human、storage、nav/social 交叉复查，再由不同 agent 进行第二轮运行链和持久化链复查，最后由主 agent 对最终增量和跨文件签名核对。

queue、nav 的 graph/mutation/persistence/Floyd、host/social 生命周期等区域已检查；没有依据证明可删的保护保持原样。没有为减少分支而删除同步或外部数据检查。

验证限于当前源码、调用点、快照差异、格式与空白检查。没有编译、构建、运行、测试或 Sanitizer；以上记录不代表性能、并发行为或崩溃恢复已经得到运行验证，也不表示基线已有的其他问题已全部修复。
