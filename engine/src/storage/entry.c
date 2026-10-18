#include "../entry.h"

#include "domino_shared_error_codes.h"
#include "entry.h"
#include "meta.h"
#include "sub_module.h"
#include "transaction.h"

DOMINO_CODE dominoStorageModuleInit(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;

    DOMINO_CODE result = txnRecoverSave(save_path);
    if (result != CODE_OK) {
        return result;
    }
    result = storageSubModuleReset();
    if (result != CODE_OK) {
        return result;
    }
    yyjson_doc* doc = metaLoad(&result);
    if (result == ERR_FILE_NOT_FOUND) {
        storageSubModuleDestroy();
        return CODE_OK;
    }
    if (result != CODE_OK) {
        return result;
    }

    yyjson_val* sub_module = yyjson_obj_get(yyjson_doc_get_root(doc), "sub_module");

    /* sub_module 借用 doc 内存，模块加载完成后才能释放 doc。 */
    result = loadAllSubModule(sub_module);
    if (result != CODE_OK) {
        return result;
    }

    yyjson_doc_free(doc);
    storageSubModuleDestroy();
    return CODE_OK;
}

DOMINO_CODE dominoStorageModuleExit(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;

    DOMINO_CODE result = storageSubModuleReset();
    if (result != CODE_OK) {
        return result;
    }
    storageSubModuleSetIntegrityVerify(g_domino_engine_launch_config.storage_integrity_verify);

    /* 摘要逐层依赖：先写分片，再写模块 meta，最后写根 meta。 */
    char staging_path[DOMINO_STORAGE_PATH_MAX];
    result = txnPrepareSaveParent(save_path);
    if (result != CODE_OK) {
        return result;
    }
    result = txnBuildStagingPath(save_path, staging_path, sizeof(staging_path));
    if (result != CODE_OK) {
        return result;
    }

    result = saveAllShards(staging_path);
    if (result != CODE_OK) {
        return result;
    }
    result = metaSave(staging_path);
    if (result != CODE_OK) {
        return result;
    }

    result = txnCommitReplace(staging_path, save_path);
    if (result != CODE_OK) {
        return result;
    }

    storageSubModuleDestroy();
    return CODE_OK;
}
