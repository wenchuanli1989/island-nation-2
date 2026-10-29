# Storage 子系统说明

本文档说明 `engine/src/storage` 的当前实现。Storage 子系统负责把引擎内存态保存到 `g_domino_engine_launch_config.storage_path` 指向的存档目录，并在引擎启动时从该目录恢复。

加载、保存中的 IO、分配、JSON 解析、完整性及容量范围错误在检测处调用 `DOMINO_EXIT_WITH_ERROR`，同步打印上下文和错误码到 stderr，再执行 `exit(EXIT_FAILURE)`。不再逐层返回致命错误或等待宿主终止；失败保留已修改的内存和磁盘步骤，不回滚或重试。成功路径继续释放临时 JSON、缓冲区和索引，并完成正常存档提交。

根 `meta.json` 不存在表示新世界，`metaLoad()` 返回 `nullptr`；已有存档必须完整包含当前注册表的全部 13 个模块 meta。加载时或提交完成后的旧备份清理失败不影响 active 存档，统一使用 `DOMINO_PRINT_MESSAGE` 直接写 stdout，不进入日志队列，输出后继续。再次提交前必须成功清理旧备份并刷新目录；失败由提交边界直接报告致命错误一次，避免先打印非致命告警再打印退出错误。

加载契约明确限定为：当前版本、正确运行数据、同一次完整持久化得到的存档。加载只恢复字段、容器、ID 映射和当前进程的指针，不重新验证字段类型、窄化范围、重复 ID、元数据计数一致性或跨模块关系。实体身份、索引、绑定指针、引用及业务状态的正确性由可信加载及运行时输入、状态变更边界保证，保存前不再重复审计。保存直接序列化当前事实；导航仍按需整理数组、重建分段并刷新 ID 索引，实际写出所需的容量与算术范围检查保留在使用边界。

版本拒绝、可选 CRC32C/size 策略、JSON 解析及构造返回值、真实分配失败、容器容量、分配大小溢出、路径格式化与磁盘操作结果继续检查。JSON 语法错误为 `ERR_INVALID_JSON`，版本不支持为 `ERR_UNSUPPORTED_VERSION`，CRC32C 不匹配为 `ERR_CHECKSUM_MISMATCH`；容量超限、内存大小溢出和路径缓冲不足分别为 `ERR_CAPACITY_EXCEEDED`、`ERR_MEMORY_OVERFLOW` 和 `ERR_BUFFER_TOO_SMALL`。这些错误使用本次操作的结果，不因持久化内容可信而省略。

## 模块边界

供引擎内部调用的入口在 `entry.c`：

- `dominoStorageModuleInit()`：加载存档。
- `dominoStorageModuleDestroy()`：保存存档。

内部按职责分层：

| 文件 | 职责 |
|------|------|
| `entry.c` / `entry.h` | 使用引擎已校验的 `storage_path`，编排加载与保存事务。 |
| `transaction.c` / `transaction.h` | 生成 staging 目录，并用 `.prev` 备份目录提交替换。 |
| `meta.c` / `meta.h` | 读写根 `meta.json` 与各模块 `{type}.meta.json`，编排 regions 与注册表实体。 |
| `io.c` / `io.h` | 构建分片路径，读写 JSON 文件，计算 CRC32C，登记或读取分片摘要。 |
| `sub_module.c` / `sub_module.h` | 维护加载/保存过程中的模块 meta 表与分片摘要表。 |
| `serde.c` / `serde.h` | JSON 读写辅助、名称/描述字符串恢复、各 serde 函数声明。 |
| `serde_registry.c` / `serde_registry.h` | 注册可分片实体类型，并用宏生成统一 save/load 循环。 |
| `serde_host.c` | `regions.json`、`movable_object`、`island`。 |
| `serde_human.c` | `human` 与 `DominoHumanData`。 |
| `serde_human_ai.c` | 按人物归组的嵌套任务／行为记录；人物日程与需求状态由 `serde_human.c` 保存。 |
| `serde_social.c` | `org`、`building`、`city`、`country` 与各自 data，以及 `asset`。 |
| `serde_nav.c` | `fork_road`、`road`、`road_line` 的权威实体数据；不保存运行期索引与导航缓存。 |

## 保存流程

普通 Freeze 由客户端直接固定业务时钟，不通知业务线程；线程在下一批入口自行检查冻结，当前有限业务批次仍可写入，不能直接作为保存屏障。正常退出由客户端先调用 `dominoEngineStop()` 进入 `STOPPING`，冻结并校验业务时刻 T，再唤醒主循环和全部业务线程；依次 join 主循环、human 执行／规划线程和 game_timer 后，统一将人物结算到 T，再排空日志、join writer 并刷新输出，保留队列和同步对象并进入 `STOPPED`。Stop 不保存世界；外部随后调用 `dominoEngineDestroy()` 进入 `DESTROYING`，释放 game_timer 和主循环队列，准备导航并执行存档，再释放 human 运行资源和世界模块。Destroy 只接受 `STOPPED`，不调用任何 Stop，未先 Stop 直接调用会报错退出。主循环只结束当前批次并退出，不承担退出停钟、结算或保存。日志线程已停止，日志文件和队列保留到保存完成后才释放；保存及销毁阶段的非致命诊断统一使用 `DOMINO_PRINT_MESSAGE` 直接写 stdout，不调用日志记录接口。投递者结束前其目标队列保持有效。仅 Init、未 Start 的世界也由外部先 Stop 再 Destroy：此时尚无工作线程，引擎跳过 human／game_timer Stop、全部 join 和人物结算，仍调用 logger Stop 关闭入队并刷新输出；Destroy 保存已加载及配置的事实；本次校验精简不改变存档版本或 JSON 字段。

世界保存与资源清理完成后，引擎才调用 `dominoTimeModuleDestroy()` 将内存时钟恢复为空的冻结快照。存档中的累计纳秒基准保持退出时刻，下一次 Init 仍按文件恢复；common、host、social 等世界容器及索引在各自 Destroy 返回前立即清空，不依赖下次加载覆盖。

storage 是引擎内部的 Init → Destroy 两阶段模块，加载与保存顺序由引擎对外生命周期入口保证，不额外维护初始化状态或重复校验。加载／保存各自的临时索引仍由调用方按 Init → Destroy 成对管理，完成后清空资源，供下一轮使用。加载入口依赖引擎已创建 ID map 和空目标容器，固定注册表保证每轮只加载一次，不在逐类加载中重复检查这两项前置条件；加载复用可信存档已经保证的字段、数量、唯一性与引用关系，继续检查 IO、容量和真实资源失败。

`dominoStorageModuleDestroy()` 执行完整保存：

1. 使用引擎初始化时已校验并持有的 `storage_path`。
2. `storageSubModuleInit()` 分配本轮保存的临时分片摘要缓存；空的模块记录、计数及校验策略由静态初值或上一轮 Destroy 保证。
3. `txnPrepareSaveParent()` 递归创建目标父目录，`txnBuildStagingPath()` 再生成隐藏 staging 路径：`.{base}.__txn__.{ms}-{pid}`。
4. `saveAllShards(staging_path)` 创建 staging 目录并按 `g_storage_module_types[]` 写所有模块数据分片；`regions` 是注册表首项，对应固定单文件 `regions.json`。
5. `metaSave(staging_path)` 写所有 `{type}.meta.json`，然后写根 `meta.json`。
6. `txnCommitReplace(staging_path, save_path)` 先恢复并持久化 active，清理已有旧备份，再刷 staging 目录并提交；若原存档存在，先移动到 `{save_path}.prev`，每次 rename 后都刷父目录。
7. active rename 并刷父目录成功后，删除 `.prev` 并再次刷父目录；这两个清理步骤失败仅通过 `DOMINO_PRINT_MESSAGE` 直接向 stdout 输出非致命诊断，已完成的提交保留，交给下一次 Init 收敛状态。

提交完成前的致命失败立即打印到 stderr 并 exit，不删除 staging、不立即恢复 `.prev`，保留本轮已完成的磁盘步骤。
未提交的 staging 不参与加载，也不会覆盖 active；新进程只通过 `txnRecoverSave()` 处理 active 与 `.prev`。

## 加载流程

`dominoStorageModuleInit()` 执行完整加载：

1. 使用引擎初始化时已校验并持有的 `storage_path`。
2. `txnRecoverSave()` 在读取 meta 前收敛 active 与 `.prev` 的事务状态，避免提交中途崩溃被误判成新世界。
3. `storageSubModuleInit()` 分配本轮加载的临时分片摘要缓存；上一轮加载／保存结束时已由 Destroy 清空模块记录，不重复重置。
4. `metaLoad()` 读取根 `meta.json`。仅目录项查询返回 `ENOENT` 时返回 `nullptr`，入口将其视为新世界；权限、IO、路径过长及实际读取失败直接打印到 stderr 并 exit，悬空符号链接也不会按新世界启动。
5. `metaLoad()` 拒绝非当前 `version`，直接读取可信的 `info`、`runtime_option`、`runtime_data` 和 `sub_module`，恢复 `account_id`、`run_mode`、累计未冻结现实纳秒基准与全局自增 ID。
6. 加载流程使用根 `meta.json.integrity_verify` 作为该存档的校验策略；运行时配置只决定下一次保存策略。
7. `loadAllSubModule()` 先按 `g_storage_module_types[]` 加载所有模块 meta，建立内存中的模块 meta 表与分片摘要表。
8. 按注册表顺序读取各模块数据，先读取 `regions.json`；普通实体 `shard_count == 0` 时没有分片文件，模块 meta 仍存在。
9. 所有实体主表和 ID map 加载完成后，统一调用不可失败的 `bind_references_fn` 绑定指针并重置不持久化的运行字段。加载不另行扫描跨模块引用关系。

实体分片的数量与 ID 唯一性由可信存档契约保证；宏生成的 load 函数保留数组分配大小、哈希容量和实际插入失败检查。
加载致命失败时立即终止，不撤销已导入的实体、字符串、任务、容器长度或 ID 映射，也不释放当前解析文档和临时行为块。完整加载成功或确认没有存档的新世界启动，才释放本轮临时文档与摘要索引。

## 事务崩溃恢复

`txnRecoverSave()` 使用以下确定性规则处理上一次提交可能留下的状态：

| active | `.prev` | 恢复动作 |
|--------|---------|----------|
| 不存在 | 不存在 | 新世界，不做处理 |
| 不存在 | 目录 | 将 `.prev` rename 回 active，并刷父目录 |
| 目录 | 不存在 | 正常已提交状态 |
| 目录 | 目录 | active 表示 staging rename 已完成；先刷父目录，再删除旧 `.prev` |
| symlink/普通文件 | 任意 | 打印致命错误并 exit，不加载、不删除、不按新世界启动 |

提交点是 staging 成功 rename 为 active。staging 在 rename 前会刷目录；active、`.prev` 的 rename 和备份删除后都会刷同父目录，使文件内容和目录项的持久化顺序一致。

失败或崩溃遗留的 staging 目录会保留，不自动提升为 active，也不由恢复流程扫描或清理。已有存档在 active 移到 `.prev` 后、staging 提交前失败，新进程恢复 `.prev`；staging 提交后失败，新进程保留 active 并清理旧备份。首次保存若尚未提交，active 与 `.prev` 都不存在，新进程仍以新世界启动，未提交 staging 仅作为遗留文件保留。恢复 active 所需的 IO 或路径检查失败立即打印到 stderr 并 exit；加载时 active 已确认后的备份清理失败只输出非致命诊断，再次提交前的旧备份清理失败直接终止。

递归删除先移除非根路径的末尾斜杠，再复用 `lstat()` 的类型结果：非目录（含根 symlink）直接删除，真实目录才进入 `nftw` 后序遍历；目录内的 symlink 也只删除链接本身，不进入链接目标。事务层还会拒绝把 active、staging 或 `.prev` symlink 当作存档目录。

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

`runtime_data.game_date_time_ns` 保存 time 模块的累计未冻结现实纳秒基准，以 `uint64_t` 纳秒保留精度，未应用游戏日历倍率。它不是业务毫秒字段；业务时刻和时长固定为 `domino_runtime_ms_t`（`uint32_t`）现实毫秒，由 `dominoTimeGetRuntimeMs(&now_ms)` 校验范围后输出，超出范围报错。加载新会话不清零累计基准，纳秒存储位宽不能作为扩宽业务毫秒的依据。`global_increment_id` 当前保存：`human`、`task`、`org`、`country`、`city`、`island`、`building`、`fork_road`、`road_line`、`asset`、`movable_object`、`name`、`description`。`task` 对应 `DominoGlobalIncrementID.task_increment_id`，沿用其他实体计数器的原子读写方式保存和恢复，表示 32 位下一候选任务 ID。0 是合法初值或回绕值，取号会跳过 0，并由任务池单写线程跳过现有 ID map 中仍占用的任务 ID；该值不要求大于已有任务 ID，任务提交失败留下的 ID 空洞不回退。加载直接恢复 `task`，不按任务最大 ID 改写计数器；导航的 fork road 与 road line 游标也使用完整 meta 中的值，不重新扫描最大 ID 修复。

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
- 新增分组时不扫描全局实体数组验证分组是否遗漏；加载直接恢复可信布局，保存前从实体数组重建分组，复用业务边界已保证的实体关系。
- 增删会使受影响的 Floyd 缓存失效，并标记布局需要整理；导航直接消费当前基础段和增量尾。

保存前 `dominoNavModuleDestroyBefore()` 会先整理 fork road/road 数组：活跃实体按 `region_index + type + id` 排序并位于数组
前缀，逻辑删除项位于后缀。排序后单遍扫描活跃前缀，用 `kv_push` 生成分组，再统一填写增量边界；不再为容量或活跃数量预扫实体。
整理完成后直接刷新已有 ID map 的下标；实体和映射的一致性由可信加载及运行期增删入口保证，保存时不再重复扫描校验。因此落盘的每个 segment 必须满足：

- `init_index/init_count` 无缝覆盖该分组在活跃前缀中的连续区间。
- `increment_index` 等于整个活跃前缀长度；当前保存时已将运行期增量整理进基础段。
- `total_count == init_count`，只统计未删除实体。
- segment key 按 `region_index + road_network_type` 严格递增，不允许重复。

存档边界为：

| 状态 | 性质 | 恢复来源 |
|------|------|----------|
| `fork_road` / `road` 实体字段 | 权威世界数据 | 直接从实体分片加载 |
| `DominoNavDataSegment` 的六个协议字段 | 当次存档的布局索引 | 从可信 fork/road 模块 meta 直接加载 |
| Fork/Road ID map | 运行期查询索引 | 扫描实体数组的 ID 重建 |
| `to_fork_road_id` / `to_fork_road_count` | 运行期邻接表 | 由活跃 `road.id` 的起点、终点与可通行权重重建 |
| Floyd 路径矩阵 | 运行期计算缓存 | 首次寻路时由当前路网按需生成 |

当前 Nav 加载流程为：

```text
从 fork_road.meta.json / road.meta.json 读取 data_segment
    -> 加载 fork_road / road 实体
    -> dominoNavModuleInitAfter()
    -> 检查导航实体与分组容量
    -> 保留已加载的 segment 和 storage 建立的 ID map，重建邻接表
    -> 首次寻路时按需生成 Floyd 矩阵
```

segment 字段、唯一分组与连续区间由可信存档保证，不在加载时再次证明或修复。导航仍保留容器、分组顶点及邻接表的容量保护。Floyd 矩阵、顶点指针表和顶点数位于独立的运行期
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

`STORAGE_DEFINE_SHARDED_SIMPLE` 生成无嵌套 data 的实体保存/加载函数。加载入口只校验数组分配大小和哈希容量上限，实体与 ID map 在实际插入时按需分配，失败直接打印到 stderr 并 exit；不为后续步骤预留容量。`STORAGE_DEFINE_SHARDED_WITH_DATA` 的 data push 与 data id map 维护由对应 `deserializeX()` 完成，并在该模块全部分片加载后按可信的 data_id 绑定指针，避免加载中扩容使地址失效。human 使用专用保存／加载函数维护必需的同序 data 与共用人物 ID map，不套用可选 data 的宏。

## 完整性校验

保存时的完整性开关来自 `g_domino_engine_launch_config.storage_integrity_verify`，并写入根 `meta.json.integrity_verify`。加载时使用存档自身声明的策略，因此可用不同的下一次保存策略打开存档。

开启完整性校验时：

- `storageWriteShardFile()` 写分片后计算整文件 CRC32C 与 size，并登记到分片摘要表。
- `metaSave()` 写模块 meta 时把每个分片摘要写入 `shard[]`，同时把模块 meta 自身 CRC32C 与 size 写入根 `meta.json` 的 `sub_module`。
- `loadSubModuleMeta()` 读取模块 meta 时校验模块 meta 文件 CRC32C/size。
- `storageReadShardFile()` 读取分片时根据内存分片摘要表校验分片 CRC32C/size。模块 meta 入口已按可信元数据登记连续 shard_id 的全部摘要，实体加载按已登记的分片范围调用，摘要表在加载期间保持不变，内部读取不重复检查摘要是否存在。

关闭完整性校验时：

- `shard[].crc32c` 与 `shard[].size` 写为 `0`。
- 分片与模块 meta 直接解析，不做 CRC/size 比对。

单个 JSON 文件大小上限为 `DOMINO_STORAGE_SHARD_FILE_MAX_BYTES`，当前为 100 MiB。

## serde 辅助约定

- `serdeWriteU32Array()` 与 `serdeWriteU8Array()` 遇到元素 `0` 时提前停止，适用于有效前缀数组。
- `serdeWriteU8FixedArray()` 与 `serdeWriteI8FixedArray()` 固定长度写出，包含零值。
- `serdeReadU32Array()`、`serdeReadU8Array()`、`serdeReadFloatArray()` 按 JSON 数组顺序覆盖目标有效范围，复用遍历下标并保留容量限制；缺失或非数组输入由遍历接口产生零次迭代，未写入的元素保持原值。实体加载入口负责初始清零，nav 的固定 uint8 数组直接复用 `serdeReadU8Array()`，不额外清零。
- `serdeReadI8FixedArray()` 保留先清零目标数组、再按 JSON 顺序覆盖的独立约定。
- 固定二维／三维坐标按当前格式规定的维度直接读取；分片 `entities` 从可信根对象直接取得。
- `serdeWriteNameDescription()` 保存名称/描述字符串，不保存原始 ID。
- `serdeReadNameDescription()` 依赖实体 data 已由反序列化入口清零；非空名称／描述重新分配 ID 并维护全局映射，空字符串保持零 ID，不在辅助函数内重复清零。新 ID 不是原存档 ID；其哈希分配失败及碰撞仍检查，不能用存档中的实体 ID 唯一性替代。

## 实体覆盖

`serde_host.c`：

- `regions.json` 保存 `DominoRegion` 数组，其中九项数量字段均为 `uint32_t`；加载直接恢复数值并保留固定地域数组的容量检查。
- `movable_object` 保存位置、方向、速度、空间信息。
- `island` 保存 ID、类型、状态、地域索引。

`serde_human.c`：

- `human` 顶层对应 64 字节的 `DominoHuman`，保存 `human_id`、状态、移动对象引用、地域／空间；请求、计时登记及就绪标志只存在运行内存。`status` 保存 `DOMINO_HUMAN_STATUS_NORMAL`（0）或 `DOMINO_HUMAN_STATUS_EXCEPTION`（1）；任务失败或取消后人物进入异常，保存与加载保留该状态，执行器直接跳过，不因重新加载恢复执行。
- 非移动对象人类会保存 `position_x/y/z`。
- 每个人物必须提供独占的嵌套 `data` 对象，对应 4096 字节的 `DominoHumanData`；`data.human_id` 必须与人物的非零 `human_id` 一致。身高、体重、需求、情绪、财富、组织关系、当前位置和导航地址等属性均位于其中，人物 `task_id_list`、`active_task_id_list`、`schedule_revision`、`needs_updated_at_ms` 和 `random_state` 也由此保存。
- 六项需求 `energy/food/water/hygiene/recreation/social` 保存 `uint8_t` 的 0～100 整数分，`DOMINO_HUMAN_NEED_MAX=100`，不保存缩放值或独立小数余量；例如 `energy=20` 表示 20 分精力。正确存档保证六项需求和 `height/weight` 的字段类型与范围；后两者范围仍为 `0..UINT8_MAX`。`needs_updated_at_ms` 保留累计未冻结现实毫秒，后续自然变化仅补结算完整分钟，存档保留未满一分钟的待结算时间，不将结算基准强制推进到保存时刻。
- 人物与 data 数组严格一一对应且同序，共用 `human_id -> array index` 映射，不维护独立 data／AI 索引。加载先登记数据，全部主表稳定后直接绑定 `human->data`；同序身份及绑定关系由可信加载和运行期修改入口维护，保存直接使用同索引记录。人物 data 不允许缺失或为 null。指针、预留 `other_info`、人物核心调度标识及 data 的 `pending_plan_reasons` 不进入存档；加载只重置运行字段，不清空持久事实。
- `data.task_id_list` 只保存有效的持有任务 ID，保留提交顺序；JSON 数组长度恢复为 `uint8_t task_count`，不另存数量，不写未使用尾部。数组长度最多 32，加载保留容量检查，非零 ID 与去重由可信存档及运行期任务增删入口保证；序列化任务 ID 数组前保留有效数量的容量检查。有效前缀之外的尾部不属于持久数据。三个活动槽完整写出，包括零值；加载直接读取三个槽位，不使用遇 0 终止的普通前缀数组辅助函数。

`serde_human_ai.c`：

- 每个人物恰有一条 `{human_id, tasks}` 记录，人物任务嵌套在 `tasks` 数组中，每个任务再嵌套自己的行为块；`tasks` 按 `data.task_id_list` 的有效前缀顺序保存；加载按 ID 导入可信任务，不重新排序或补齐人物任务清单，也不分配人物去重位图。本模块按 128 人分片，不重复保存人物需求或随机状态。
- 任务与行为保存状态、计划／实际时间、有效进度和行为游标。行为的执行进度字段直接属于 `DominoBehavior`，状态、实际时间、有效时长、等待检查时刻和标志均直接保存为行为 JSON 对象的字段。任务为扁平记录，不支持任务暂停／恢复及父子层级，不保存父子引用、停止交接意图或取消范围。任务另保存 `channel_index`（0 为主通道，1、2 为同级次通道），三者均允许任意已定义任务类型，不通过类型推断所属通道。所有业务时间均为 32 位累计未冻结现实毫秒；0 和 `UINT32_MAX` 都是合法数值，由状态和显式标志区分是否启动或结束。
- `DominoTask` 直接保存 `behaviors_ptr` 和 `execution_version`，serde 逐字段写入任务事实，并将指针指向的行为内容写入嵌套数组，不写裸指针或执行版本。加载重新分配连续行为块，填入任务的 `behaviors_ptr` 后交给 `dominoHumanStoreImportTask()`；导入依赖调用方保证任务池已初始化、任务身份正确、独占行为块有效且 ID 唯一，只检查池容量与实际资源操作。成功后由任务池拥有该独占块；导入返回容量错误或资源操作失败时，serde 直接打印到 stderr 后 exit。运行期提交层先校验草案语义、人物归属和目标建筑，再由 `dominoHumanStoreAddValidatedTask()` 复用已取得的人物 data，检查容量、版本上限、ID 分配和入池资源错误。每次导入将该任务的 `uint64_t execution_version` 初始化为 1，新建任务同样从 1 开始，随后重置人物运行态不修改任务版本。后续由执行线程在检查 `UINT64_MAX` 后直接递增本任务版本，达到上限则返回 `ERR_OUT_OF_RANGE` 且版本不变；不同任务无需保持版本不同，也不增加全局版本计数器。执行器在消费异步结果时重新核对请求身份、版本和当前资格。
- 加载完成后只绑定人物/位置指针并重置人物调度字段，不再调用全世界语义审计。人物/data 同序身份、索引、绑定指针、位置引用以及人物异常状态、任务与行为语义、唯一归属、活动槽、行为游标和持久时间关系由可信加载及运行时输入、状态变更边界保证。Human 与 HumanAI 直接按当前记录写出分片，不另设保存前数据审计；数量窄化和固定数组容量仍在写出使用边界检查。
- JSON 字段类型、窄化范围与关系完整性不在读取时复查；行为数、人物任务数组长度、任务池容量、聚合分配大小和行为块分配结果仍检查。`wait_until_ms` 仅在行为处于 `WAITING` 状态时有效，非等待状态忽略该字段，无须清零；`0` 和 `UINT32_MAX` 均是合法等待检查时刻。未启动或未结束状态下不生效的时间字段按原值保留；有效进度与关键引用的约束由可信加载及运行时输入、状态变更边界保证。
- 已定义的任务／行为类型统一保存和加载，任务可包含多个行为，公共状态、时间和游标关系由可信加载及运行时输入、状态变更边界保证。行为不再保存 `parameters` 对象或独立的 `target_active_ms` 字段；睡眠目标有效时长直接由行为的 `planned_end_ms - planned_begin_ms` 推导，执行期间保持该区间长度不变，加载后按保存的区间与 `active_elapsed_ms` 继续执行。当前执行器只执行独立单行为核心睡眠，无需建筑或地点许可；退出时停止全部业务线程后统一结算正常人物，异常人物不再推进需求或任务。加载后正常人物直接续跑并重建计时唤醒，异常人物保持原业务事实。游戏全局冻结／解冻和正常任务的存档续跑不属于任务暂停／恢复能力。通用任务执行与其他地点服务资格仍待扩充。

`serde_social.c`：

- `org`、`building`、`city`、`country` 都有可选嵌套 data，`data_id` 与绑定指针的一致性由可信加载及运行期修改入口维护，保存直接写出已有绑定。
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
6. 如有跨模块 ID 引用，注册不可失败的 `bind_references_fn`，在全部主表稳定后绑定可信 ID；运行时输入及状态变更边界负责维护引用和业务约束，保存复用这些结论。
7. 如需模块 meta 扩展字段，在 `g_storage_module_types[]` 中配置 `write_meta_extra_fn` / `read_meta_extra_fn`。

## 当前边界

- `metaLoad()` 严格要求当前版本格式；本项目不承担历史存档迁移。
- 当前格式每个注册项都有模块 meta；加载不提供缺失模块兼容路径，只有整个根 `meta.json` 不存在时才按新世界启动。
- 名称与描述按字符串重新分配 ID，不保证跨 save/load 后数值 ID 稳定。
- 存档版本由 `DOMINO_STORAGE_SAVE_VERSION` 控制，当前为 `20`：行为进度字段直接保存在行为对象中，睡眠目标时长由计划区间推导，行为移除 `parameters` 及其中的 `target_active_ms`；人物 `status` 明确定义正常与异常，任务失败或取消后的异常状态持久保存。任务／行为状态删除挂起／暂停值，任务移除 `parent_task_id`、`active_child_task_id`、`pending_child_task_id`、`cancel_target_task_id`、`pending_stop_reason` 和 `cancel_scope`；任务没有 `result` 字段，由 `status` 表达当前状态及结束结果。地域统一保存九项 `uint32_t` 数量字段（包含 `movable_object_count` 和 `fork_road_count`）。人物及其必需的 data 统一使用 `human_id`，六项需求采用 0～100 整数分，人物 data 用 `task_id_list` 保存持有任务的提交顺序，数组长度恢复 `task_count`，并保存活动槽、日程版本及需求事实；`human_ai` 依人物 ID 数组顺序保存归组的任务／行为。根 meta 的 `global_increment_id.task` 继续保存全局原子计数器的下一候选 ID。开发样例同步版本 20，不提供旧布局迁移或双版本加载。格式变化必须同步提升版本。
