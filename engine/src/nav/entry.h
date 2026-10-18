#ifndef DOMINO_ENGINE_NAV_MODULE_H
#define DOMINO_ENGINE_NAV_MODULE_H

#include "domino_shared_error_codes.h"

/**
 * @brief 在 storage load 前初始化 Nav 的实体、segment、索引与运行期缓存容器。
 * @return `CODE_OK` 成功；失败直接返回具体错误码，已取得的资源保留到进程终结。
 */
DOMINO_CODE dominoNavModuleInitBefore(void);

/** @brief 在 storage load 后校验持久化布局并重建邻接；引擎保证 Nav 初始化与 Storage 加载已成功。 */
DOMINO_CODE dominoNavModuleInitAfter(void);

/** @brief 在 storage save 前把运行期基础段与增量尾压实成 canonical 布局；引擎保证 Nav 已初始化且业务写入已停止。 */
DOMINO_CODE dominoNavModuleExitBefore(void);

/** @brief 在 storage save 完成后释放 Nav 的全部运行期资源。 */
void dominoNavModuleExitAfter(void);

#endif
