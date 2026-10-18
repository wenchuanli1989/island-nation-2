# shared 公共头文件说明

`shared/include` 只放跨模块可见的类型、错误码和通用工具声明。实体数组、ID map、模块生命周期函数等运行时状态由 `engine/src/*/entry.h` 暴露给 engine 内部使用，不属于 shared 的职责。

## 文件职责

| 文件 | 说明 |
|------|------|
| `domino_shared_types.h` | 基础 ID、名称/描述、坐标类型，32 位现实毫秒类型 `domino_runtime_ms_t`，以及核心世界设计约定。 |
| `domino_shared_error_codes.h` | 统一 `DOMINO_CODE` 错误码和错误码字符串转换。 |
| `domino_shared_common.h` | Linux 当前实现下的时间、路径、文件读写、原子写入和递归删除工具。 |
| `domino_shared_host.h` | 地域、岛屿、运动物体、全局自增 ID 结构。 |
| `domino_shared_human.h` | 64 字节人物核心实体 `DominoHuman` 和 4096 字节扩展属性 `DominoHumanData`。 |
| `domino_shared_social.h` | 组织、建筑、城市、国家、资产及其扩展 data。 |
| `domino_shared_behavior.h` | 分离的任务／行为类型与实例；`DominoTask` 同时保存业务事实、独占行为块指针和运行期执行版本。 |
| `domino_shared_nav.h` | fork road、road、road line 和导航边权类型。 |
| `domino_shared_core.h` | 核心公共聚合头占位，当前未导出内容。 |

## 公共数据约定

- 业务时刻和时长固定使用 `domino_runtime_ms_t`（`uint32_t`）累计未冻结现实毫秒；0 和 `UINT32_MAX` 均合法，越界报错。底层 64 位纳秒、UTC 墙上时间戳和 ID 不是业务毫秒类型；不能由它们推断需要扩宽业务计时。
- `0` ID 保留为无效值。全局自增计数器在 storage 根 `meta.json` 中持久化。任务与人物等实体统一在 `DominoGlobalIncrementID` 中维护；`task_increment_id` 是 32 位原子计数器，表示下一候选 ID，0 是合法初值或回绕值。任务分配复用 `DOMINO_ALLOC_NON_ZERO_ID` 跳过 0，再由任务池单写线程通过现有 ID map 跳过仍占用的 ID；失败取号留下的空洞不回退，ID 大小不表示任务先后。
- 实体数据以扁平数组保存，engine 内部再用 `khashl` 建立 `id -> array index` 映射；shared 只定义实体布局，不保存全局数组或映射。
- 名称和描述使用 `domino_name_id_t`、`domino_description_id_t` 引用运行时字符串表。storage 落盘时保存字符串本身，加载后重新分配运行时 ID，因此名称/描述 ID 不保证跨 save/load 稳定。
- `other_info` 字段是结构体对齐、预留或运行时压缩信息空间。当前 storage serde 通常不读写这些字段，新增持久化字段时需要同步更新对应 `serde_*.c` 和 `engine/src/storage/readme.md`。
- `DominoHuman` 固定为 64 字节，保存必需的 data 指针、`human_id`、状态、运动对象引用或静态坐标、地域／空间，以及 `active_plan_request_id`、`wakeup_timer_id`、`wakeup_version` 和规划待投、执行就绪两个调度标志。请求与计时器 ID 保持 64 位；`wakeup_version` 使用 `uint32_t`，由执行线程按每个人物独立维护，递增禁止回绕，不进入存档。`DominoHumanData` 固定为 4096 字节，以相同 `human_id` 标识，人物与 data 数组同序共用一个人物 ID 索引；不再保留独立 data ID、AI 容器或 AI 索引。data 前 40 字节集中放置随机状态、人物 ID、三个活动槽、日程版本、结算时刻、六项需求及健康／生命值，当前位置、导航地址、财富与调度补充字段紧接其后；128 字节的持有任务 ID 数组排在这些高频字段之后，从预留空间中调整且不扩大结构；其余属性按身体外观、组织职业、身份、家庭社交、资产、能力性格和位置历史组织。字段采用自然对齐，`other_info` 补齐预留空间，不使用 packed 或位域压缩。
- 其他核心实体如 `DominoOrg`、`DominoBuilding`、`DominoCity`、`DominoCountry`、`DominoAsset`、`DominoRoad`、`DominoRoadLine` 为 128 字节；扩展 data 多为 4096 字节。
- 任务和行为拥有独立类型与状态；`DominoTask.type` 使用 `DOMINO_TASK_TYPE`，`DominoBehavior.type` 使用 `DOMINO_BEHAVIOR_TYPE`，对应类型查询接口也使用各自枚举。存档仍写枚举数值，加载按对应枚举范围校验。engine 的全局任务池直接保存 `DominoTask`，由任务 ID map 定位；任务通过 `behaviors_ptr` 独占连续行为块，并保存自己的执行版本、行为游标、计划／实际时间及停止意图，行为保存各自进度。`execution_version` 保持 `uint64_t`，新建和加载导入时初始化为 1，仅表示任务自身版本，不要求跨任务唯一。后续执行线程在需要使旧结果失效时，检查 `UINT64_MAX` 后直接执行 `task->execution_version++`；达到上限则返回 `ERR_OUT_OF_RANGE` 且版本不变。执行版本不持久化，执行器和异步结果消费尚未接入。结构体拷贝不会复制指针指向的行为数组，跨线程快照须独立复制所需行为内容；不再有全局行为索引或人物内联任务。
- `DominoHumanData` 用 `task_id_list[DOMINO_HUMAN_TASK_COUNT_MAX]` 与 `uint8_t task_count` 保存本人全部持有任务，并另存固定三个活动槽位（主通道、次通道 1、次通道 2）。持有数组最多 32 项，包含待启动、运行、挂起父任务、活动子任务、待交接任务及未回收终态；添加时追加，删除后稳定前移，状态及计划时间变化不重排。删除日程首尾和任务 `prev_task_id/next_task_id`，父子及待交接引用只表达业务关系。任务实例的 `channel_index` 保存所属通道并参与存档；三个通道均可承载任意已定义类型，主通道优先于两个同级次通道。父子及待交接链适用于三个通道，同链须为同一人物和通道，最多 4 层，活动叶任务占槽；槽位允许为 0，但非零任务必须归属该人物并处于对应通道的运行状态。六项需求 `energy/food/water/hygiene/recreation/social` 复用原字段，以 `uint8_t` 保存 0～100 整数分，`DOMINO_HUMAN_NEED_MAX=100`；不另存小数余量，规则比较与显示直接使用整数分。自然变化约定按完整未冻结现实分钟结算，`needs_updated_at_ms` 仍为累计业务毫秒，未满分钟的时间留待下次，跨多分钟补结算；这不改变任务、行为和 timer 的毫秒精度，需求结算入口仍待实现。
- 存档版本 12 中，`human.entities[].data` 保存人物属性、日程和需求结算事实；必需的 `human_ai` 模块按 128 人分片，每条记录仅含 `human_id` 与嵌套的任务／行为。根 meta 的 `global_increment_id.task` 保存下一候选任务 ID，不要求大于已保存任务 ID。人物核心中的调度标识、data 中的 `pending_plan_reasons`、任务的 `behaviors_ptr/execution_version` 均不进入存档；加载重建行为块并将每个任务的执行版本初始化为 1，之后清空人物调度运行态时不修改任务版本。JSON `task_id_list` 仅保存有效前缀，加载由长度恢复 `task_count` 并保留原顺序，嵌套任务按同样的 ID 顺序保存，加载只校验其归属集合一致，人物数组是提交顺序的唯一来源。执行器后续按通道、计划开始时间及启动条件筛选，同时间以数组位置决定先后，不依赖任务 ID 数值；待交接子任务不能独立启动，挂起父任务未结束时同通道后续任务不能越过它。已定义的任务／行为类型统一保存和加载，任务持有非空行为序列。行为专有参数按类型读写，目前睡眠保存目标时长，其余类型使用空参数对象；规划和执行回调仍待实现。

## 当前实现边界

- 当前跨平台封装仍以 Linux 实现为主，`domino_shared_common.h` 中的路径、rename、fsync、递归删除等函数未完整覆盖 Windows/macOS 差异。
- `domino_shared_nav.h` 中 `dominoAddRoad()` 与 `dominoRemoveRoad()` 已在 engine nav 模块实现；`dominoUpdateRoadWeight()` 与 `dominoBlockRoad()` 当前只是预留声明。
- 物理、个体智能、群体智能和超级智能阶段仍是模拟流水线的设计目标；当前运行逻辑由消息消费主循环和 human 行为规划工作线程承载。
- 任务模型和最小 AI 存档已接入；当前受限睡眠执行器已接通自主睡眠、计时唤醒及存档续接，普通暂停只在批次边界等待，退出在线程结束后统一结算；通用需求决策和三通道执行仍待实现。
