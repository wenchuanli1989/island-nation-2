#include "../entry.h"

#include "../logger/entry.h"
#include "domino_shared_common.h"
#include "domino_shared_error_codes.h"
#include "entry.h"
#include "meta.h"
#include "sub_module.h"
#include "transaction.h"

DOMINO_CODE dominoStorageModuleInit(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;
    if (!save_path || save_path[0] == '\0') {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_WARN, "no save path configured, skip loading");
        return ERR_INVALID_PARAM;
    }
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_INFO, "loading from: %s", save_path);

    DOMINO_CODE result = txnRecoverSave(save_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "save transaction recovery failed (result=%s)",
                          dominoErrorCodeToString(result));
        return result;
    }
    result = storageSubModuleReset();
    if (result != CODE_OK) {
        return result;
    }
    yyjson_doc* doc = metaLoad(&result);
    if (result == ERR_FILE_NOT_FOUND) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_INFO, "no save data found, starting fresh");
        result = CODE_OK;
        goto cleanup;
    }
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "load failed (result=%s)", dominoErrorCodeToString(result));
        goto cleanup;
    }

    yyjson_val* sub_module = yyjson_obj_get(yyjson_doc_get_root(doc), "sub_module");

    /* sub_module 借用 doc 内存，模块加载完成后才能释放 doc。 */
    result = loadAllSubModule(sub_module);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "load all shards failed (result=%s)",
                          dominoErrorCodeToString(result));
    } else {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_INFO, "load complete");
    }

cleanup:
    yyjson_doc_free(doc);
    storageSubModuleDestroy();
    return result;
}

DOMINO_CODE dominoStorageModuleExit(void) {
    const char* save_path = g_domino_engine_launch_config.storage_path;
    if (!save_path || save_path[0] == '\0') {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_WARN, "no save path configured, skip saving");
        return ERR_INVALID_PARAM;
    }
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_INFO, "saving to: %s", save_path);

    DOMINO_CODE result = storageSubModuleReset();
    if (result != CODE_OK) {
        return result;
    }
    storageSubModuleSetIntegrityVerify(g_domino_engine_launch_config.storage_integrity_verify);

    /* 摘要逐层依赖：先写分片，再写模块 meta，最后写根 meta。 */
    char staging_path[DOMINO_STORAGE_PATH_MAX];
    result = txnPrepareSaveParent(save_path);
    if (result != CODE_OK) {
        goto cleanup;
    }
    result = txnBuildStagingPath(save_path, staging_path, sizeof(staging_path));
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "txn staging path build failed");
        result = ERR_INVALID_PARAM;
        goto cleanup;
    }

    result = saveAllShards(staging_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "save entity shards failed (result=%s)",
                          dominoErrorCodeToString(result));
        goto cleanup_staging;
    }
    result = metaSave(staging_path);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "save meta failed (result=%s)", dominoErrorCodeToString(result));
        goto cleanup_staging;
    }

    result = txnCommitReplace(staging_path, save_path);
    if (result != CODE_OK) {
        goto cleanup_staging;
    }

    DOMINO_ENGINE_LOG_MSG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_INFO, "save complete");
    goto cleanup;

cleanup_staging:
    (void)dominoRemovePathRecursive(staging_path);
cleanup:
    storageSubModuleDestroy();
    return result;
}
