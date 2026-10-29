#ifndef DOMINO_ENGINE_NAV_PERSISTENCE_H
#define DOMINO_ENGINE_NAV_PERSISTENCE_H

/**
 * @brief 把长期基础段与运行期增量尾压实为下一次保存使用的 canonical 布局。
 *
 * 调用前必须已经通过运行期图校验，并清空所有保存了实体地址的路径缓存。
 */
void navPersistenceCompact(void);

#endif
