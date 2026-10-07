#ifndef DOMINO_ENGINE_STORAGE_META_H
#define DOMINO_ENGINE_STORAGE_META_H

#include "domino_shared_error_codes.h"
#include "yyjson/yyjson.h"

/**
 * @brief 写根 meta.json 和所有模块 meta 文件。
 *
 * 调用前必须已经完成分片写入，因为模块 meta 会消费分片摘要缓存。
 */
DOMINO_CODE metaSave(const char* save_path);

/**
 * @brief 读取根 meta.json，恢复运行时选项和全局自增 ID。
 *
 * 返回的 yyjson_doc 由调用者释放；result 为 ERR_FILE_NOT_FOUND 时表示新存档。
 */
yyjson_doc* metaLoad(DOMINO_CODE* result);

/** @brief 写注册表中的所有模块数据分片。 */
DOMINO_CODE saveAllShards(const char* save_path);

/** @brief 读取所有模块 meta，再按注册表顺序加载模块数据。 */
DOMINO_CODE loadAllSubModule(yyjson_val* sub_module);

#endif
