# Storage 子系统说明

本文档说明 `engine/src/storage` 的当前实现。Storage 子系统负责把引擎内存态保存到 `g_domino_engine_launch_config.storage_path` 指向的存档目录，并在引擎启动时从该目录恢复。

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
5. `metaLoad()` 校验根对象、`version`、`info`、`runtime_option`、`runtime_data.game_date_time_ns`、`runtime_data.global_increment_id` 与 `sub_module`，并恢复 `account_id`、`run_mode`、游戏日历时间与全局自增 ID。
6. 加载流程使用根 `meta.json.integrity_verify` 作为该存档的校验策略；运行时配置只决定下一次保存策略。
7. `loadAllSubModule()` 先按 `g_storage_module_types[]` 加载所有模块 meta，建立内存中的模块 meta 表与分片摘要表。
8. 按注册表顺序读取各模块数据；`regions` 是 required 模块，会先读取 `regions.json`，普通实体 `shard_count == 0` 时跳过。
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
    ├── island
    ├── movable_object
    ├── org
    ├── road
    └── road_line
```

`runtime_data.game_date_time_ns` 保存 time 模块内的 `game_date_time_ns`，单位纳秒。`global_increment_id` 当前保存：`human`、`org`、`country`、`city`、`island`、`building`、`fork_road`、`road_line`、`asset`、`movable_object`、`name`、`description`。

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
regions -> asset -> building -> city -> country -> fork_road -> human -> island -> movable_object -> org -> road -> road_line
```

分片大小：

| 类型 | 每片上限 |
|------|----------|
| `human`、`org`、`building`、`city`、`country` | `DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA`，8000 |
| `asset`、`fork_road`、`road`、`road_line`、`island`、`movable_object` | `DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA`，100000 |

`STORAGE_DEFINE_SHARDED_SIMPLE` 生成无嵌套 data 的实体保存/加载函数。`STORAGE_DEFINE_SHARDED_WITH_DATA` 额外预留 data 向量容量，具体 data push 与 data id map 维护由对应 `deserializeX()` 完成。

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

- `human` 保存移动对象引用、地址、资产计数、生理/心理/社会属性等。
- 非移动对象人类会保存 `position_x/y/z`。
- 有 `data_id` 时要求 `human->data` 存在并与全局 data id map 一致。

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
- `regions` 是 required 模块，根 `meta.json.sub_module.regions` 或 `regions.meta.json` 缺失都会导致加载失败；只有整个根 `meta.json` 不存在时才按新存档启动。
- 名称与描述按字符串重新分配 ID，不保证跨 save/load 后数值 ID 稳定。
- 存档版本由 `DOMINO_STORAGE_SAVE_VERSION` 控制，当前为 `5`；格式变化必须同步提升版本。
