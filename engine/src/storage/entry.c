#include "../entry.h"

#include "entry.h"
#include "meta.h"
#include "sub_module.h"
#include "transaction.h"

void dominoStorageModuleInit(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;
    txnRecoverSave(save_path);
    storageSubModuleInit();
    yyjson_doc* doc = metaLoad();
    if (doc) {
        yyjson_val* sub_module = yyjson_obj_get(yyjson_doc_get_root(doc), "sub_module");
        /* sub_module 借用 doc 内存，模块加载完成后才能释放 doc。 */
        loadAllSubModule(sub_module);
        yyjson_doc_free(doc);
    }
    storageSubModuleDestroy();
}

void dominoStorageModuleDestroy(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;
    storageSubModuleInit();
    storageSubModuleSetIntegrityVerify(g_domino_engine_launch_config.storage_integrity_verify);

    /* 摘要逐层依赖：先写分片，再写模块 meta，最后写根 meta。 */
    char staging_path[DOMINO_STORAGE_PATH_MAX];
    txnPrepareSaveParent(save_path);
    txnBuildStagingPath(save_path, staging_path, sizeof(staging_path));
    saveAllShards(staging_path);
    metaSave(staging_path);
    txnCommitReplace(staging_path, save_path);
    storageSubModuleDestroy();
}
