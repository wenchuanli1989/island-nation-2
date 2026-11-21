#ifndef DOMINO_ENGINE_STORAGE_META_H
#define DOMINO_ENGINE_STORAGE_META_H

#include "yyjson/yyjson.h"

/** @file meta.h
 * @brief 根 meta.json 与模块 meta 的读写，以及按注册表顺序保存、加载全部模块。
 * @note 摘要逐层依赖：先写分片，再写模块 meta（含分片摘要），最后写根 meta（含模块 meta 摘要）。
 */

/** @brief 写全部模块 meta 和根 meta.json。
 * @pre 全部分片已写出，清单中已登记分片摘要。
 */
void storageSaveMeta(const char* save_path);

/** @brief 读取根 meta.json：校验版本，恢复完整性开关、账户信息、累计时间基准和全局 ID 计数器。
 * @return 根文档，由调用方释放；根 meta 不存在时返回空指针，表示新世界。其他读取或解析失败致命退出。
 */
yyjson_doc* storageLoadMeta(void);

/** @brief 创建保存目录，按注册表顺序写出全部模块分片。 */
void storageSaveAllShards(const char* save_path);

/** @brief 先读全部模块 meta 登记摘要，再按注册表顺序加载各模块，最后绑定跨模块引用。
 * @param module_digest_obj 根 meta 中 "modules" 对象，借用根文档内存。
 * @pre 世界容器和 ID 映射表已初始化且为空。
 */
void storageLoadAllModules(yyjson_val* module_digest_obj);

#endif
