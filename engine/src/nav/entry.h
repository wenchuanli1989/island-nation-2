#ifndef DOMINO_ENGINE_NAV_MODULE_H
#define DOMINO_ENGINE_NAV_MODULE_H

#include "domino_shared_error_codes.h"

/**
 * @brief 在 storage load 前初始化 Nav 的实体、segment、索引与运行期缓存容器。
 * @return `CODE_OK` 成功；失败返回具体错误码，且不会留下部分初始化状态。
 */
DOMINO_CODE dominoNavModuleInitBefore(void);

/** @brief 在 storage load 后校验持久化布局，并重建 ID map 与邻接关系。 */
DOMINO_CODE dominoNavModuleInitAfter(void);

/** @brief 在 storage save 前把运行期基础段与增量尾压实成 canonical 持久化布局。 */
DOMINO_CODE dominoNavModuleExitBefore(void);

/** @brief 在 storage save 完成后释放 Nav 的全部运行期资源。 */
void dominoNavModuleExitAfter(void);

#endif
