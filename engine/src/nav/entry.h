#ifndef DOMINO_ENGINE_NAV_MODULE_H
#define DOMINO_ENGINE_NAV_MODULE_H

/** @file entry.h
 * @brief Nav 模块生命周期：Init → InitAfter → DestroyBefore → Destroy，由引擎串行各调用一次。
 */

/** @brief storage 加载前创建 ID 映射表。 */
void dominoNavModuleInit(void);

/** @brief storage 加载后检查容量、重建邻接并把布局标记为 canonical；路径缓存按需构建。 */
void dominoNavModuleInitAfter(void);

/** @brief storage 保存前把增量尾压实为 canonical 布局；只整理顺序、分段和下标，不改变道路语义。 */
void dominoNavModuleDestroyBefore(void);

/** @brief 保存完成后释放全部 Nav 资源。 */
void dominoNavModuleDestroy(void);

#endif
