# shared 公共头文件说明

`shared/include` 只放跨模块可见的类型、错误码和通用工具声明。实体数组、ID map、模块生命周期函数等运行时状态由 `engine/src/*/entry.h` 暴露给 engine 内部使用，不属于 shared 的职责。

## 文件职责

| 文件 | 说明 |
|------|------|
| `domino_shared_types.h` | 基础 ID、名称/描述、坐标类型，以及核心世界设计约定。 |
| `domino_shared_error_codes.h` | 统一 `DOMINO_CODE` 错误码和错误码字符串转换。 |
| `domino_shared_common.h` | Linux 当前实现下的时间、路径、文件读写、原子写入和递归删除工具。 |
| `domino_shared_host.h` | 地域、岛屿、运动物体、全局自增 ID 结构。 |
| `domino_shared_human.h` | 人类基础实体 `DominoHuman` 和扩展 data `DominoHumanData`。 |
| `domino_shared_social.h` | 组织、建筑、城市、国家、资产及其扩展 data。 |
| `domino_shared_behavior.h` | 行为系统公共数据结构：任务及任务依次触发的行为。 |
| `domino_shared_nav.h` | fork road、road、road line 和导航边权类型。 |
| `domino_shared_core.h` | 核心公共聚合头占位，当前未导出内容。 |

## 公共数据约定

- `0` ID 保留为无效值。engine 通过 `DOMINO_ALLOC_NON_ZERO_ID` 从 `DominoGlobalIncrementID` 的原子计数器分配非 0 ID，并在 storage 根 `meta.json` 中持久化这些计数器。
- 实体数据以扁平数组保存，engine 内部再用 `khashl` 建立 `id -> array index` 映射；shared 只定义实体布局，不保存全局数组或映射。
- 名称和描述使用 `domino_name_id_t`、`domino_description_id_t` 引用运行时字符串表。storage 落盘时保存字符串本身，加载后重新分配运行时 ID，因此名称/描述 ID 不保证跨 save/load 稳定。
- `other_info` 字段是结构体对齐、预留或运行时压缩信息空间。当前 storage serde 通常不读写这些字段，新增持久化字段时需要同步更新对应 `serde_*.c` 和 `engine/src/storage/readme.md`。
- 多数核心实体要求固定尺寸：如 `DominoHuman`、`DominoOrg`、`DominoBuilding`、`DominoCity`、`DominoCountry`、`DominoAsset`、`DominoRoad`、`DominoRoadLine` 为 128 字节；扩展 data 多为 4096 字节。
- 行为系统只有任务和行为两层：`DominoTask` 从创建起依次处于未开始、进行中、结束、归档状态，并同时保存计划条件与执行运行态；一个任务可以按 `sequence_index` 依次触发多个 `DominoBehavior`，每个行为通过 `task_id` 归属于任务，并内联自己的时间、地点和参与者上下文。engine 为任务维护全局数组与 ID map，为行为维护全局数组以及 `(task_id, sequence_index)` 复合索引；两者当前都没有 storage serde。

## 当前实现边界

- 当前跨平台封装仍以 Linux 实现为主，`domino_shared_common.h` 中的路径、rename、fsync、递归删除等函数未完整覆盖 Windows/macOS 差异。
- `domino_shared_nav.h` 中 `dominoAddRoad()` 与 `dominoRemoveRoad()` 已在 engine nav 模块实现；`dominoUpdateRoadWeight()` 与 `dominoBlockRoad()` 当前只是预留声明。
- 物理、个体智能、群体智能和超级智能阶段仍是模拟流水线的设计目标；当前运行逻辑由消息消费主循环和 human 行为规划工作线程承载。
