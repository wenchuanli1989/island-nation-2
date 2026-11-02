#ifndef DOMINO_ENGINE_NAV_MODULE_H
#define DOMINO_ENGINE_NAV_MODULE_H

/**
 * @brief 在 storage load 前初始化 Nav 的实体、segment、索引与运行期缓存容器。
 * @pre 引擎保证串行按 Init → InitAfter → DestroyBefore → Destroy 各执行一次。
 * @note 内部入口不重复校验生命周期；初始化分配失败直接打印到 stderr 并 exit，Destroy 完成后可再次 Init。
 */
void dominoNavModuleInit(void);

/**
 * @brief 在 storage load 后检查 Nav 容量并重建邻接。
 * @pre Nav 已 Init；Storage 已恢复当前格式的正确 canonical 世界及全局下一候选 ID，无存档时为空状态。
 * @note Start 前已具备可查询图；Floyd 缓存按需计算，不承担加载修复。不重新校验存档布局、实体关系或修复 ID 游标。
 */
void dominoNavModuleInitAfter(void);

/**
 * @brief 在 storage save 前把运行期基础段与增量尾压实成 canonical 布局。
 * @pre Nav 已 InitAfter，业务写入及结算均已结束，尚未保存。
 * @note 引擎在 Stop 完成后调用；只整理实体顺序、分段和索引并失效借用缓存，保持道路身份、权重、邻接及路线语义，不推进业务或补做结算。
 */
void dominoNavModuleDestroyBefore(void);

/**
 * @brief 在 storage save 完成后释放 Nav 的全部运行期资源。
 * @pre 引擎已完成 DestroyBefore 和 storage save。
 */
void dominoNavModuleDestroy(void);

#endif
