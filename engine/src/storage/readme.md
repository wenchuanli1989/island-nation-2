# Storage 子系统说明

本文档说明 `engine/src/storage` 的当前实现。Storage 子系统负责把引擎内存态保存到 `g_domino_engine_launch_config.storage_path` 指向的存档目录，并在引擎启动时从该目录恢复。

内存分配失败按现有接口契约简洁处理，保留必要检查、错误传播和已有清理，不额外引入强制终止、恢复重试或复杂回滚。

## 模块边界

对外入口在 `entry.c`：

- `dominoStorageModuleInit()`：加载存档。
- `dominoStorageModuleExit()`：保存存档。

内部按职责分层：

| 文件 | 职责 |
|------|------|
| `entry.c` / `entry.h` | 检查 `storage_path`，编排加载与保存事务。 |
| `transaction.c` / `transaction.h` | 生成 staging 目录，并用 `.prev` 备份目录提交替换。 |
| `meta.c` / `meta.h` | 读写根 `meta.json` 与各模块 `{type}.meta.json`，编排 regions 与注册表实体。 |
| `io.c` / `io.h` | 构建分片路径，读写 JSON 文件，计算 CRC32C，登记或读取分片摘要。 |
| `sub_module.c` / `sub_module.h` | 维护加载/保存过程中的模块 meta 表与分片摘要表。 |
| `serde.c` / `serde.h` | JSON 读写辅助、分片根校验、名称/描述字符串恢复、各 serde 函数声明。 |
| `serde_registry.c` / `serde_registry.h` | 注册可分片实体类型，并用宏生成统一 save/load 循环。 |
| `serde_host.c` | `regions.json`、`movable_object`、`island`。 |
| `serde_human.c` | `human` 与 `DominoHumanData`。 |
| `serde_human_ai.c` | 按人物归组的嵌套任务／行为记录；人物日程与需求状态由 `serde_human.c` 保存。 |
| `serde_social.c` | `org`、`building`、`city`、`country` 与各自 data，以及 `asset`。 |
| `serde_nav.c` | `fork_road`、`road`、`road_line` 的权威实体数据；不保存运行期索引与导航缓存。 |

## 保存流程

`dominoStorageModuleExit()` 执行完整保存：

1. `storage_path` 为空时返回 `ERR_INVALID_PARAM`；当前 `dominoEngineInit()` 会把该错误视为初始化失败。
2. `storageSubModuleReset()` 清空本轮保存的模块 meta 缓存与分片摘要缓存。
3. `txnPrepareSaveParent()` 递归创建目标父目录，`txnBuildStagingPath()` 再生成隐藏 staging 路径：`.{base}.__txn__.{ms}-{pid}`。
4. `saveAllShards(staging_path)` 创建 staging 目录，并按 `g_storage_module_types[]` 写所有模块数据分片；`regions` 是注册表首项，对应固定单文件 `regions.json`。
5. `metaSave(staging_path)` 写所有 `{type}.meta.json`，然后写根 `meta.json`。
6. `txnCommitReplace(staging_path, save_path)` 先刷 staging 目录，再提交 staging；若原存档存在，先移动到 `{save_path}.prev`，每次 rename 后都刷父目录，提交失败时恢复旧存档。
7. active rename 已确认后删除 `.prev`；备份清理失败不会否定已完成的提交，下一次 Init/保存会再次收敛该状态。

保存期间任一步失败都会删除 staging 目录，避免半成品目录被当成有效存档。
错误会原样返回给 `dominoEngineExit()`；Engine 保持 `STOPPING`、冻结时间和完整内存世界。调用方处理外部 IO 问题后再次调用
`dominoEngineExit()` 时会直接重试未完成的保存，不会重复停止线程或结算时间；但会重新检查 Nav 是否仍需压实运行期增量尾，
确保重试写出的实体与 `data_segment` 属于同一布局。

## 加载流程

`dominoStorageModuleInit()` 执行完整加载：

1. `storage_path` 为空时返回 `ERR_INVALID_PARAM`；当前 `dominoEngineExit()` 会先检查路径，空路径时直接跳过保存，不调用该入口。
2. `txnRecoverSave()` 在读取 meta 前收敛 active 与 `.prev` 的事务状态，避免提交中途崩溃被误判成新世界。
3. `storageSubModuleReset()` 清空上一次加载/保存留下的模块记录。
4. `metaLoad()` 读取根 `meta.json`。文件不存在时返回 `ERR_FILE_NOT_FOUND`，入口将其视为新存档并返回 `CODE_OK`。
5. `metaLoad()` 校验根对象、`version`、`info`、`runtime_option`、`runtime_data.game_date_time_ns`、`runtime_data.global_increment_id` 与 `sub_module`，并恢复 `account_id`、`run_mode`、累计未冻结现实纳秒基准与全局自增 ID。
6. 加载流程使用根 `meta.json.integrity_verify` 作为该存档的校验策略；运行时配置只决定下一次保存策略。
7. `loadAllSubModule()` 先按 `g_storage_module_types[]` 加载所有模块 meta，建立内存中的模块 meta 表与分片摘要表。
8. 按注册表顺序读取各模块数据；`regions` 与 `human_ai` 是 required 模块，先读取 `regions.json`，普通实体 `shard_count == 0` 时跳过。即使世界没有人物，也必须有 `human_ai` 模块 meta。
9. 所有实体主表和 ID map 加载完成后，先统一调用 `validate_references_fn` 只读校验跨模块 ID，再调用不可失败的 `bind_references_fn` 绑定指针；反序列化函数不得依赖其他模块的加载顺序。

实体分片加载完成后，宏生成的 load 函数会校验实际加载数量等于模块 meta 中的 `total_count`，并拒绝重复实体 ID。

## 事务崩溃恢复

`txnRecoverSave()` 使用以下确定性规则处理上一次提交可能留下的状态：

| active | `.prev` | 恢复动作 |
|--------|---------|----------|
| 不存在 | 不存在 | 新世界，不做处理 |
| 不存在 | 目录 | 将 `.prev` rename 回 active，并刷父目录 |
| 目录 | 不存在 | 正常已提交状态 |
| 目录 | 目录 | active 表示 staging rename 已完成；先刷父目录，再删除旧 `.prev` |
| symlink/普通文件 | 任意 | 返回错误，不加载、不删除、不按新世界启动 |

提交点是 staging 成功 rename 为 active。staging 在 rename 前会刷目录；active、`.prev` 的 rename 和备份删除后都会刷同父目录，使文件内容和目录项的持久化顺序一致。

递归删除使用 `lstat()`：遇到目录内或根路径上的 symlink 时只 unlink 链接本身，不进入链接目标。事务层还会拒绝把 active、staging 或 `.prev` symlink 当作存档目录。

该协议按单写者设计：同一个 `storage_path` 不允许多个 Domino 进程并发加载/保存；跨进程互斥不属于当前事务层能力。

## 文件布局

典型存档目录：

```text
{save_path}/
├── meta.json
├── regions.meta.json
├── asset.meta.json
├── building.meta.json
├── city.meta.json
├── country.meta.json
├── fork_road.meta.json
├── human.meta.json
├── human_ai.meta.json
├── island.meta.json
├── movable_object.meta.json
├── org.meta.json
├── road.meta.json
├── road_line.meta.json
├── regions.json
├── asset_0.json
├── building_0.json
├── city_0.json
├── country_0.json
├── fork_road_0.json
├── human_0.json
├── human_ai_0.json
├── island_0.json
├── movable_object_0.json
├── org_0.json
├── road_0.json
└── road_line_0.json
```

`regions` 是单文件，路径固定为 `{save_path}/regions.json`。其他实体使用 `{type_name}_{shard_id}.json`。模块 meta 文件名与分片前缀都由注册表的稳定 `type_name` 推导，不再作为可变存档数据。

## 根 meta.json

根 `meta.json` 结构：

```text
root
├── version
├── integrity_verify
├── info
│   └── account_id
├── runtime_option
│   └── run_mode
├── runtime_data
│   ├── game_date_time_ns
│   └── global_increment_id
└── sub_module
    ├── regions
    ├── asset
    ├── building
    ├── city
    ├── country
    ├── fork_road
    ├── human
    ├── human_ai
    ├── island
    ├── movable_object
    ├── org
    ├── road
    └── road_line
```

`runtime_data.game_date_time_ns` 保存 time 模块的累计未冻结现实纳秒基准，以 `uint64_t` 纳秒保留精度，未应用游戏日历倍率。它不是业务毫秒字段；业务时刻和时长固定为 `domino_runtime_ms_t`（`uint32_t`）现实毫秒，由 `dominoTimeGetRuntimeMs(&now_ms)` 校验范围后输出，超出范围报错。加载新会话不清零累计基准，纳秒存储位宽不能作为扩宽业务毫秒的依据。`global_increment_id` 当前保存：`human`、`task`、`org`、`country`、`city`、`island`、`building`、`fork_road`、`road_line`、`asset`、`movable_object`、`name`、`description`。`task` 对应 `DominoGlobalIncrementID.task_increment_id`，沿用其他实体计数器的原子读写方式保存和恢复，表示 32 位下一候选任务 ID。0 是合法初值或回绕值，取号会跳过 0，并由任务池单写线程跳过现有 ID map 中仍占用的任务 ID；该值不要求大于已有任务 ID，任务提交失败留下的 ID 空洞不回退。加载严格检查 `task` 为 `uint32_t` 范围内的无符号整数，不按任务最大 ID 改写计数器。

`sub_module.{type}` 结构：

```text
{
  "crc32c": 0 或模块 meta 文件 CRC32C,
  "size": 0 或模块 meta 文件字节数
}
```

根 `meta.json` 本身不做 CRC 校验。

## 模块 meta

每个模块 meta 文件命名为 `{type}.meta.json`，结构：

```text
root
├── type
├── total_count
├── shard_count
├── data_segment        # 仅 fork_road / road
│   └── [{
            "init_index": uint,
            "init_count": uint,
            "increment_index": uint,
            "total_count": uint,
            "region_index": uint,
            "road_network_type": uint
        }]
└── shard
    └── [{ "crc32c": uint, "size": uint }]
```

### 导航 data segment 持久化

`fork_road.meta.json` 和 `road.meta.json` 通过 `serdeNavWriteModuleMetaExtraFields()` / `serdeNavReadModuleMetaExtraFields()`
持久化 `data_segment`。它是与当次存档实体排列绑定的 canonical 布局索引，用于在下次启动时直接恢复
`region_index + road_network_type` 分组，避免再次排序和重建 segment。

加载完成后，Nav 会长期维护“基础段 + 增量尾”运行期布局，而不是每次增删或寻路时重新排序：

- `init_index/init_count` 固定描述上次 canonical 存档形成的基础区间；删除基础实体只写 tombstone，不移动区间。
- 新实体只追加到 fork road / road 全局数组尾部；已有分组保持 `increment_index` 不变，只增减 `total_count`。
- 新分组以第一次追加实体的位置作为 `increment_index`，并以 `init_index=0、init_count=0` 表示尚无持久化基础段。
- 运行期新分组直接追加到 segment 数组尾部，按 key 线性查找，不维护分组记录的排序；保存前从实体数组统一重建有序分组。
- 多个分组可以交错追加；读取增量尾时按 `region_index + type + status` 过滤，因此无需把尾部再次分组排序。
- 计数归零的分组在运行期继续保留，后续新增仍复用原增量边界；直到保存压实时才删除空分组。
- 道路增删在修改点检查分组和计数范围，实体、ID map 和邻接关系修改成功后统一增减计数；新分组才在此时追加。
- 新增分组时不扫描全局实体数组验证分组是否遗漏；加载时校验完整布局，保存前校验实体关系并从实体数组重建分组。
- 增删会使受影响的 Floyd 缓存失效，并标记布局需要整理；导航直接消费当前基础段和增量尾。

保存前 `dominoNavModuleExitBefore()` 会先整理 fork road/road 数组：活跃实体按 `region_index + type + id` 排序并位于数组
前缀，逻辑删除项位于后缀。排序后单遍扫描活跃前缀，用 `kv_push` 生成分组，再统一填写增量边界；不再为容量或活跃数量预扫实体。
整理完成后直接刷新已有 ID map 的下标；实体和映射的一致性在排序前校验，刷新只保留内部断言。因此落盘的每个 segment 必须满足：

- `init_index/init_count` 无缝覆盖该分组在活跃前缀中的连续区间。
- `increment_index` 等于整个活跃前缀长度；当前保存时已将运行期增量整理进基础段。
- `total_count == init_count`，只统计未删除实体。
- segment key 按 `region_index + road_network_type` 严格递增，不允许重复。

存档边界为：

| 状态 | 性质 | 恢复来源 |
|------|------|----------|
| `fork_road` / `road` 实体字段 | 权威世界数据 | 直接从实体分片加载 |
| `DominoNavDataSegment` 的六个协议字段 | 当次存档的布局索引 | 从 fork/road 模块 meta 加载并与实体校验 |
| Fork/Road ID map | 运行期查询索引 | 扫描实体数组的 ID 重建 |
| `to_fork_road_id` / `to_fork_road_count` | 运行期邻接表 | 由活跃 `road.id` 的起点、终点与可通行权重重建 |
| Floyd 路径矩阵 | 运行期计算缓存 | 首次寻路时由当前路网按需生成 |

当前 Nav 加载流程为：

```text
从 fork_road.meta.json / road.meta.json 读取 data_segment
    -> 加载 fork_road / road 实体
    -> dominoNavModuleInitAfter()
    -> 校验 segment 边界、分组、计数与实体排列完全一致
    -> 保留已加载的 segment，只重建 ID map 和邻接表
    -> 首次寻路时按需生成 Floyd 矩阵
```

任何 segment 字段缺失、整数溢出、越界、重复 key、区间缺口或与实体属性不一致，都会使
`dominoEngineInit()` 失败并回滚，不会静默改为运行时派生。Floyd 矩阵、顶点指针表和顶点数位于独立的运行期
path cache sidecar 中，不属于 `DominoNavDataSegment`，也不写入存档。

`regions.meta.json` 的 `shard_count` 固定为 `1`，对应 `regions.json`。

## 分片实体

普通分片 JSON 结构：

```text
root
├── type
├── shard_id
└── entities
```

分片内不保存 `total_count` 或 `shard_count`，这些值只以模块 meta 为准。

注册表顺序固定为：

```text
regions -> asset -> building -> city -> country -> fork_road -> human -> human_ai -> island -> movable_object -> org -> road -> road_line
```

分片大小：

| 类型 | 每片上限 |
|------|----------|
| `human`、`org`、`building`、`city`、`country` | `DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA`，8000 |
| `human_ai` | `DOMINO_STORAGE_SHARD_HUMAN_AI`，128；每条记录内嵌本人物的任务与行为 |
| `asset`、`fork_road`、`road`、`road_line`、`island`、`movable_object` | `DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA`，100000 |

`STORAGE_DEFINE_SHARDED_SIMPLE` 生成无嵌套 data 的实体保存/加载函数。`STORAGE_DEFINE_SHARDED_WITH_DATA` 额外预留 data 向量容量，具体 data push 与 data id map 维护由对应 `deserializeX()` 完成。human 使用专用保存／加载函数维护必需的同序 data 与共用人物 ID map，不套用可选 data 的宏。

## 完整性校验

保存时的完整性开关来自 `g_domino_engine_launch_config.storage_integrity_verify`，并写入根 `meta.json.integrity_verify`。加载时使用存档自身声明的策略，因此可用不同的下一次保存策略打开存档。

开启完整性校验时：

- `storageWriteShardFile()` 写分片后计算整文件 CRC32C 与 size，并登记到分片摘要表。
- `metaSave()` 写模块 meta 时把每个分片摘要写入 `shard[]`，同时把模块 meta 自身 CRC32C 与 size 写入根 `meta.json` 的 `sub_module`。
- `loadSubModuleMeta()` 读取模块 meta 时校验模块 meta 文件 CRC32C/size。
- `storageReadShardFile()` 读取分片时根据内存分片摘要表校验分片 CRC32C/size。

关闭完整性校验时：

- `shard[].crc32c` 与 `shard[].size` 写为 `0`。
- 分片与模块 meta 直接解析，不做 CRC/size 比对。

单个 JSON 文件大小上限为 `DOMINO_STORAGE_SHARD_FILE_MAX_BYTES`，当前为 100 MiB。

## serde 辅助约定

- `serdeWriteU32Array()` 与 `serdeWriteU8Array()` 遇到元素 `0` 时提前停止，适用于有效前缀数组。
- `serdeWriteU8FixedArray()` 与 `serdeWriteI8FixedArray()` 固定长度写出，包含零值。
- `serdeReadU32Array()`、`serdeReadU8Array()`、`serdeReadFloatArray()` 只接受 JSON 数组，非数组时保持目标内存当前状态。
- `serdeReadU8FixedArray()` 与 `serdeReadI8FixedArray()` 会先清零目标数组，再按 JSON 顺序覆盖。
- `serdeValidateShardRoot()` 校验分片根对象的 `type`、`shard_id` 与 `entities`。
- `serdeWriteNameDescription()` 保存名称/描述字符串，不保存原始 ID。
- `serdeReadNameDescription()` 加载时重新分配 `name_id` 与 `description_id` 并维护全局映射。

## 实体覆盖

`serde_host.c`：

- `regions.json` 保存 `DominoRegion` 数组。
- `movable_object` 保存位置、方向、速度、空间信息。
- `island` 保存 ID、类型、状态、地域索引。

`serde_human.c`：

- `human` 顶层对应 64 字节的 `DominoHuman`，保存 `human_id`、状态、移动对象引用、地域／空间；请求、计时登记及就绪标志只存在运行内存。
- 非移动对象人类会保存 `position_x/y/z`。
- 每个人物必须提供独占的嵌套 `data` 对象，对应 4096 字节的 `DominoHumanData`；`data.human_id` 必须与人物的非零 `human_id` 一致。身高、体重、需求、情绪、财富、组织关系、当前位置和导航地址等属性均位于其中，人物 `task_id_list`、`active_task_id_list`、`schedule_revision`、`needs_updated_at_ms` 和 `random_state` 也由此保存。
- 六项需求 `energy/food/water/hygiene/recreation/social` 保存 `uint8_t` 的 0～100 整数分，`DOMINO_HUMAN_NEED_MAX=100`，不保存缩放值或独立小数余量；例如 `energy=20` 表示 20 分精力。六项需求和 `height/weight` 必须存在、为无符号整数且在各自范围内；后两者范围仍为 `0..UINT8_MAX`。`needs_updated_at_ms` 保留累计未冻结现实毫秒，后续自然变化仅补结算完整分钟，存档保留未满一分钟的待结算时间，不将结算基准强制推进到保存时刻。
- 人物与 data 数组严格一一对应且同序，共用 `human_id -> array index` 映射，不维护独立 data／AI 索引。加载先登记数据，统一校验后绑定 `human->data`；保存核对两数组数量、同索引 ID 及指针一致。人物 data 不允许缺失或为 null。指针、预留 `other_info`、人物核心调度标识及 data 的 `pending_plan_reasons/pending_due_ms` 不进入存档；加载只重置运行字段，不清空持久事实。
- `data.task_id_list` 只保存有效的持有任务 ID，保留提交顺序；JSON 数组长度恢复为 `uint8_t task_count`，不另存数量，不写未使用尾部。数组必须存在，长度最多 32，元素为非零的 32 位无符号整数且不能重复；保存前核对有效数量与 ID；有效前缀之外的尾部不属于持久数据。三个活动槽完整写出，包括零值；加载严格校验数组长度为 3 和无符号整数范围，不使用遇 0 终止或超长截断的普通前缀数组辅助函数。

`serde_human_ai.c`：

- 每个人物恰有一条 `{human_id, tasks}` 记录，人物任务嵌套在 `tasks` 数组中，每个任务再嵌套自己的行为块；`tasks` 按 `data.task_id_list` 的有效前缀顺序保存；加载按 ID 导入任务，统一校验归属集合一致，不要求嵌套任务记录逐项同序，不重新排序或补齐人物任务清单。本模块按 128 人分片，不重复保存人物需求或随机状态。
- 任务与行为保存状态、计划／实际时间、有效进度、行为游标、父子关系及停止意图。任务另保存 `channel_index`（0 为主通道，1、2 为同级次通道），三者均允许任意已定义任务类型，不通过类型推断所属通道。所有业务时间均为 32 位累计未冻结现实毫秒；0 和 `UINT32_MAX` 都是合法数值，由状态和显式标志区分是否启动或结束。
- `DominoTask` 直接保存 `behaviors_ptr` 和 `execution_version`，serde 逐字段写入任务事实，并将指针指向的行为内容写入嵌套数组，不写裸指针或执行版本。加载重新分配连续行为块，填入任务的 `behaviors_ptr` 后交给 `dominoHumanStoreImportTask()`；导入只检查任务池已初始化、任务及所属人物 ID 非零、行为块非空且数量非零、任务 ID 唯一和池容量，不逐条重复完整语义检查。成功后由任务池拥有该独占块，返回错误时仍由加载方释放。运行期 `dominoHumanStoreAddTask()` 则在提交前先完整校验草案的任务与行为记录，再检查人物容量和目标建筑等条件。每次导入将该任务的 `uint64_t execution_version` 初始化为 1，新建任务同样从 1 开始，随后重置人物运行态不修改任务版本。后续由执行线程在检查 `UINT64_MAX` 后直接递增本任务版本，达到上限则返回 `ERR_OUT_OF_RANGE` 且版本不变；不同任务无需保持版本不同，也不增加全局版本计数器。执行器和异步结果消费仍待实现。
- 完整世界加载后，`human_ai` 的校验入口先调用 `serdeValidateHumanReferences()` 检查人物/data 同序身份、索引、容量及位置引用，再调用 `dominoHumanStoreValidate()`；注册表不另行重复人物校验。store 沿人物清单复用任务查询结果，统一检查任务与行为语义、唯一归属、父子及待交接关系（同一人物、同一通道，最多 4 层）、任务 `channel_index` 与三槽匹配、行为游标与进度，不再预扫描全任务池。通过后才能绑定或使用；人物保存仍检查自身前置条件，AI 保存复用完整入口。挂起父任务、活动子任务、待交接任务及未回收终态均在持有数组中；遗漏、重复、跨人物引用或孤立任务记录都报错。持有数组只表达提交顺序，不校验计划开始时刻非递减，不按任务 ID 排序；同时间任务的先后由原数组位置保留。任务不再保存 `prev_task_id/next_task_id`，人物不再保存日程首尾。人物规划／唤醒及任务执行身份不写入 JSON。
- JSON 解析保留字段存在、类型、整数范围及分配前数组长度检查；六项需求的 0～100 语义只在 store 校验。任务／行为未启动时的开始与结算时刻、非终态的结束时刻、非等待状态的等待截止点不要求清零；它们不参与当前状态的恢复判断。有效进度、已生效时间先后、状态与游标、等待原因、停止意图及关键引用仍必须一致。睡眠不校验未使用的离散效果提交标志；目标时长和完成量约束保留。
- 已定义的任务／行为类型统一保存和加载，任务可包含多个行为，校验公共状态、时间和游标关系。`parameters` 按行为类型读写：睡眠保存 `target_active_ms`，其余类型目前保存空对象，随专有参数定义扩充。执行器、地点服务资格、统一冻结结算和计时唤醒续接仍待实现。

`serde_social.c`：

- `org`、`building`、`city`、`country` 都有可选嵌套 data，保存前会校验 `data_id` 与 data 指针一致。
- `asset` 可引用 `movable_object`；无移动对象时保存 `position_x/y/z`。

`serde_nav.c`：

- `fork_road.meta.json` / `road.meta.json` 通过模块 meta 扩展回调持久化并恢复 `data_segment`。
- `fork_road` 保存 ID、坐标、节点权重、状态、类型、编号；邻接关系由有效 `road.id` 在加载后重建。
- `road` 保存 corner points、距离、耗时、成本、速度、权重、状态、类型、编号。
- `road_line` 保存 fork road 路径、起点/目标/当前位置、进度与成本。

## 增加新实体类型

新增一个可分片实体类型时：

1. 在对应 `serde_*.c` 实现 `serializeX()` 与 `deserializeX()`。
2. 如果实体有独立 data 向量，使用 `STORAGE_DEFINE_SHARDED_WITH_DATA`；否则使用 `STORAGE_DEFINE_SHARDED_SIMPLE`。
3. 在 `serde_registry.c` 增加 `countX()`。
4. 在 `g_storage_module_types[]` 中按 `type_name` 字典序加入一行；`regions` 固定保留在首位。
5. 在 `serde.h` 声明 `serdeSaveX()` 与 `serdeLoadX()`。
6. 如有跨模块 ID 引用，实现并注册只读 `validate_references_fn` 与不可失败的 `bind_references_fn`；两个回调只能依赖已加载的实体主表和 ID map。
7. 如需模块 meta 扩展字段，在 `g_storage_module_types[]` 中配置 `write_meta_extra_fn` / `read_meta_extra_fn`。

## 当前边界

- `metaLoad()` 严格要求当前版本格式；本项目不承担历史存档迁移。
- `regions` 与 `human_ai` 是 required 模块，任一模块在根 `meta.json.sub_module` 中缺失或对应模块 meta 缺失都会导致加载失败；只有整个根 `meta.json` 不存在时才按新存档启动。
- 名称与描述按字符串重新分配 ID，不保证跨 save/load 后数值 ID 稳定。
- 存档版本由 `DOMINO_STORAGE_SAVE_VERSION` 控制，当前为 `12`：人物及其必需的 data 统一使用 `human_id`，六项需求采用 0～100 整数分，人物 data 用 `task_id_list` 保存持有任务的提交顺序，数组长度恢复 `task_count`，并保存活动槽、日程版本及需求事实；移除人物日程首尾及任务前后链接，`human_ai` 依人物 ID 数组顺序保存归组的任务／行为。根 meta 的 `global_increment_id.task` 继续保存全局原子计数器的下一候选 ID。开发样例同步版本 12，不提供旧布局迁移或双版本加载。格式变化必须同步提升版本。
