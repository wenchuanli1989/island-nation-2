#ifndef DOMINO_ENGINE_NAV_MODULE_H
#define DOMINO_ENGINE_NAV_MODULE_H

/**
 * @brief 在 storage load 前初始化 Nav 的实体、segment、索引与运行期缓存容器。
 * @pre 引擎保证串行按 InitBefore → InitAfter → DestroyBefore → DestroyAfter 各执行一次。
 * @note 内部入口不重复校验生命周期；初始化分配失败直接打印到 stderr 并 exit，DestroyAfter 完成后可再次 InitBefore。
 */
void dominoNavModuleInitBefore(void);

/**
 * @brief 在 storage load 后检查 Nav 容量并重建邻接。
 * @pre 引擎已完成 InitBefore；Storage 已恢复当前格式的正确 canonical 世界及全局下一候选 ID，无存档时为空状态。
 * @note 不重新校验存档布局、实体关系或修复 ID 游标。
 */
void dominoNavModuleInitAfter(void);

/**
 * @brief 在 storage save 前把运行期基础段与增量尾压实成 canonical 布局。
 * @pre 引擎已完成 InitAfter 且停止业务写入。
 */
void dominoNavModuleDestroyBefore(void);

/**
 * @brief 在 storage save 完成后释放 Nav 的全部运行期资源。
 * @pre 引擎已完成 DestroyBefore 和 storage save。
 */
void dominoNavModuleDestroyAfter(void);

#endif
