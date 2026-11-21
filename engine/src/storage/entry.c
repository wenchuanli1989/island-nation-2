#include "../entry.h"

#include "entry.h"
#include "manifest.h"
#include "meta.h"
#include "transaction.h"

void dominoStorageModuleInit(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;
    storageTxnRecover(save_path);
    storageManifestInit();
    yyjson_doc* doc = storageLoadMeta();
    if (doc) {
        // module_digest_obj 借用 doc 内存，全部模块加载完才能释放 doc。
        yyjson_val* module_digest_obj = yyjson_obj_get(yyjson_doc_get_root(doc), "modules");
        storageLoadAllModules(module_digest_obj);
        yyjson_doc_free(doc);
    }
    storageManifestDestroy();
}

void dominoStorageModuleDestroy(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;
    storageManifestInit();
    storageManifestSetIntegrityCheck(g_domino_engine_launch_config.storage_integrity_check);

    // 摘要逐层依赖：先写分片，再写模块 meta，最后写根 meta。
    char staging_path[DOMINO_STORAGE_PATH_MAX];
    storageTxnPrepareParent(save_path);
    storageTxnBuildStagingPath(save_path, staging_path, sizeof(staging_path));
    storageSaveAllShards(staging_path);
    storageSaveMeta(staging_path);
    storageTxnCommit(staging_path, save_path);
    storageManifestDestroy();
}
