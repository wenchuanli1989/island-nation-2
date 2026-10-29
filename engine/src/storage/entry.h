#ifndef DOMINO_ENGINE_STORAGE_MODULE_H
#define DOMINO_ENGINE_STORAGE_MODULE_H

#define DOMINO_STORAGE_SAVE_VERSION 20
#define DOMINO_STORAGE_PATH_MAX 1024

/**
 * @brief 初始化 storage 子系统，从运行时配置指定的存档目录恢复内存状态。
 *
 * 由引擎串行保证 Init → Destroy 顺序；storage_path 已在 dominoEngineInit 中校验并复制。
 * 无根 meta 时以新世界启动；已有存档须为当前版本正确运行数据的一次完整持久化。
 * 加载不重验字段、唯一性与引用关系；版本不支持、JSON 解析、完整性、IO、容量或分配失败直接打印 stderr 并 exit。
 */
extern void dominoStorageModuleInit(void);

/**
 * @brief 将当前内存状态事务式保存到存档目录，并销毁 storage 子系统。
 *
 * 由引擎生命周期串行调用；要求当前实例已 Init，业务线程已停止；storage_path 使用初始化时已校验的副本。
 * 完成后可重新 Init，不重复校验引擎已保证的生命周期顺序。
 * 保存会先写入 staging 目录，所有分片和 meta 都成功后再替换正式存档目录。
 * 致命失败直接打印到 stderr 并 exit，保留已完成的磁盘步骤供下次启动恢复。
 * 提交完成后的旧备份清理失败仅报告非致命警告。
 */
extern void dominoStorageModuleDestroy(void);

#endif
