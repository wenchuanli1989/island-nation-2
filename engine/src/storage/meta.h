#ifndef DOMINO_ENGINE_STORAGE_META_H
#define DOMINO_ENGINE_STORAGE_META_H

#include "yyjson/yyjson.h"

/**
 * @brief 写根 meta.json 和所有模块 meta 文件。
 *
 * 调用前必须已经完成分片写入，因为模块 meta 会消费分片摘要缓存。
 */
void metaSave(const char* save_path);

/**
 * @brief 读取根 meta.json，恢复运行时选项和全局自增 ID。
 *
 * 返回的 yyjson_doc 由调用者释放；nullptr 表示不存在存档，应启动新世界。
 * 存档存在时，IO、格式、完整性或分配失败直接打印到 stderr 并 exit。
 */
yyjson_doc* metaLoad(void);

/** @brief 写注册表中的所有模块数据分片。 */
void saveAllShards(const char* save_path);

/** @brief 读取所有模块 meta，再按注册表顺序加载模块数据。
 * @pre 引擎已初始化全部世界容器和 ID 映射；目标为空，本次 Init 尚未加载数据。
 */
void loadAllSubModule(yyjson_val* sub_module);

#endif
