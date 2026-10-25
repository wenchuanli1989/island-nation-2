#ifndef DOMINO_ENGINE_NAV_MODULE_H
#define DOMINO_ENGINE_NAV_MODULE_H

/**
 * @brief 在 storage load 前初始化 Nav 的实体、segment、索引与运行期缓存容器。
 * @pre 引擎保证串行按 InitBefore → InitAfter → DestroyBefore → DestroyAfter 各执行一次。
 * @note 内部入口不重复校验生命周期；初始化分配失败直接打印到 stderr 并 exit，DestroyAfter 完成后可再次 InitBefore。
 */
void dominoNavModuleInitBefore(void);

/**
 * @brief 在 storage load 后校验持久化布局并重建邻接。
 * @pre 引擎已完成 InitBefore；有存档时已完成 Storage 加载，无存档时直接校验空状态。
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
