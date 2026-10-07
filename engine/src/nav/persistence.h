#ifndef DOMINO_ENGINE_NAV_PERSISTENCE_H
#define DOMINO_ENGINE_NAV_PERSISTENCE_H

#include "domino_shared_error_codes.h"

/**
 * @brief 校验存档恢复的实体数量、canonical 排列、分段覆盖及每个路网的路口数量上限。
 * @note 调用方已初始化 Nav 并完成 Storage 加载；校验期间不修改实体和分段。
 */
DOMINO_CODE navPersistenceValidateSegments(void);

/**
 * @brief 把长期基础段与运行期增量尾压实为下一次保存使用的 canonical 布局。
 *
 * 调用前必须已经通过运行期图校验，并清空所有保存了实体地址的路径缓存。
 */
void navPersistenceCompact(void);

#endif
