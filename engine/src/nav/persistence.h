#ifndef DOMINO_ENGINE_NAV_PERSISTENCE_H
#define DOMINO_ENGINE_NAV_PERSISTENCE_H

/**
 * @brief 把长期基础段与运行期增量尾压实为下一次保存使用的 canonical 布局。
 *
 * @pre 可信加载与运行时增删入口保证图数据正确及实体数量在 Nav 上限内；调用期间无并发修改。
 *      调用前须清空所有保存了实体地址的路径缓存。
 */
void navPersistenceCompact(void);

#endif
