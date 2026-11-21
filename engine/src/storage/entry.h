#ifndef DOMINO_ENGINE_STORAGE_MODULE_H
#define DOMINO_ENGINE_STORAGE_MODULE_H

#define DOMINO_STORAGE_SAVE_VERSION 32
#define DOMINO_STORAGE_PATH_MAX 1024

/** @brief 收敛事务状态后加载存档；根 meta 不存在时以新世界启动。
 * @note 信任存档内容，不重验字段、唯一性和引用关系（宪法第六章）；版本不符及任何读取、解析、校验、容量或分配失败致命退出。
 */
extern void dominoStorageModuleInit(void);

/** @brief 把世界事务式保存到存档目录：写完 staging 后再替换正式目录。
 * @pre 业务线程已停止。
 * @note 保存失败致命退出，已完成的磁盘步骤由下次启动恢复；提交后清理旧备份失败只告警。
 */
extern void dominoStorageModuleDestroy(void);

#endif
