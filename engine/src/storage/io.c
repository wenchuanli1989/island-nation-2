#include "io.h"

#include "../entry.h"
#include "../logger/entry.h"
#include "domino_shared_common.h"
#include "entry.h"
#include "sub_module.h"

void storageBuildShardPath(char* buffer, size_t buffer_size, const char* save_path, const char* type_name, uint32_t shard_id) {
    if (strcmp(type_name, "regions") == 0) {
        (void)snprintf(buffer, buffer_size, "%s/regions.json", save_path);
    } else {
        (void)snprintf(buffer, buffer_size, "%s/%s_%u.json", save_path, type_name, shard_id);
    }
}

uint32_t storageCrc32c(const void* data, size_t len) {
    const uint8_t* data_ptr = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data_ptr[i];
        for (uint32_t k = 0; k < 8; k++) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1) ^ (0x82F63B78U & mask);
        }
    }
    return ~crc;
}

yyjson_doc* storageReadJsonFile(const char* path, bool do_verify, uint32_t expect_crc32c, uint32_t expect_size, DOMINO_CODE* result) {
    if (!path) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid path");
        *result = ERR_INVALID_PARAM;
        return nullptr;
    }
    *result = CODE_OK;

    size_t length = 0;
    uint8_t* data = dominoReadFileBytes(path, &length, result);
    if (*result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "read failed: %s", dominoErrorCodeToString(*result));
        return nullptr;
    }
    if (!data || length == 0) {
        free(data);
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "read failed: file is empty: %s", path);
        *result = ERR_INVALID_DATA;
        return nullptr;
    }
    if (do_verify) {
        if (length != (size_t)expect_size) {
            free(data);
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "read failed: file size mismatch: %s", path);
            *result = ERR_INVALID_JSON;
            return nullptr;
        }
        uint32_t crc = storageCrc32c(data, length);
        if (crc != expect_crc32c) {
            free(data);
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "read failed: file crc mismatch: %s", path);
            *result = ERR_INVALID_JSON;
            return nullptr;
        }
    }
    yyjson_read_err err_json = {0};

    yyjson_doc* doc = yyjson_read_opts((char*)data, length, YYJSON_READ_NOFLAG, nullptr, &err_json);
    free(data);
    if (err_json.code != YYJSON_READ_SUCCESS) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "read failed: %s, code=%u pos=%zu path=%s", err_json.msg,
                          err_json.code, err_json.pos, path);
        *result = ERR_INVALID_JSON;
        return nullptr;
    }
    return doc;
}

DOMINO_CODE storageWriteJsonFile(const char* path, yyjson_mut_doc* doc, bool do_verify, uint32_t* out_crc32c, uint32_t* out_size) {
    if (!path || !doc) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid path or doc");
        return ERR_INVALID_PARAM;
    }
    yyjson_write_err err;
    size_t json_len = 0;
    yyjson_write_flag write_flag = YYJSON_WRITE_NOFLAG;
    char* json = yyjson_mut_write_opts(doc, write_flag, nullptr, &json_len, &err);
    if (!json) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "write json failed: %s, code=%u", err.msg, err.code);
        return ERR_COMMON;
    }
    if (json_len > (size_t)DOMINO_STORAGE_SHARD_FILE_MAX_BYTES) {
        free(json);
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "write json failed: json exceeds max bytes (%u): %s",
                          DOMINO_STORAGE_SHARD_FILE_MAX_BYTES, path);
        return ERR_OUT_OF_RANGE;
    }

    /* 摘要基于最终序列化字节，与落盘内容完全一致。 */
    uint32_t crc = !!do_verify ? storageCrc32c(json, json_len) : 0;

    DOMINO_CODE result = dominoAtomicWriteFile(path, json, json_len);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "atomic write json failed: %s", dominoErrorCodeToString(result));
        free(json);
        return result;
    }
    free(json);
    if (do_verify) {
        if (out_crc32c) {
            *out_crc32c = crc;
        }
        if (out_size) {
            *out_size = (uint32_t)json_len;
        }
    }
    return CODE_OK;
}

DOMINO_CODE storageWriteShardFile(const char* type_name, uint32_t shard_id, const char* save_path, yyjson_mut_doc* doc) {
    if (!type_name || !save_path || !doc) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid type_name, save_path or doc");
        return ERR_INVALID_PARAM;
    }
    char path[DOMINO_STORAGE_PATH_MAX];
    storageBuildShardPath(path, sizeof(path), save_path, type_name, shard_id);
    uint32_t crc = 0;
    uint32_t size = 0;
    bool do_verify = storageSubModuleIntegrityVerify();
    DOMINO_CODE result = storageWriteJsonFile(path, doc, do_verify, &crc, &size);
    if (result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "write shard file failed: %s", dominoErrorCodeToString(result));
        return result;
    }
    if (do_verify) {
        StorageSubModuleShard shard_rec = {0};
        strncpy(shard_rec.type_name, type_name, sizeof(shard_rec.type_name) - 1);
        shard_rec.shard_id = shard_id;
        shard_rec.crc32c = crc;
        shard_rec.size = size;
        result = storageSubModuleShardAdd(shard_rec);
        if (result != CODE_OK) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "add shard digest failed: %s",
                              dominoErrorCodeToString(result));
            return result;
        }
    }
    return CODE_OK;
}

yyjson_doc* storageReadShardFile(const char* type_name, uint32_t shard_id, const char* save_path, DOMINO_CODE* result) {
    if (!type_name || !save_path || !result) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid type_name, save_path or result");
        return nullptr;
    }
    *result = CODE_OK;
    char path[DOMINO_STORAGE_PATH_MAX];
    storageBuildShardPath(path, sizeof(path), save_path, type_name, shard_id);

    bool do_verify = storageSubModuleIntegrityVerify();
    uint32_t expect_crc32c = 0;
    uint32_t expect_size = 0;
    if (do_verify) {
        const StorageSubModuleShard* shard = storageSubModuleShardFind(type_name, shard_id);
        if (!shard) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "missing shard digest expectation for %s shard=%u", type_name,
                              shard_id);
            *result = ERR_NOT_FOUND;
            return nullptr;
        }
        expect_crc32c = shard->crc32c;
        expect_size = shard->size;
    }
    yyjson_doc* doc = storageReadJsonFile(path, do_verify, expect_crc32c, expect_size, result);
    if (*result != CODE_OK) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to read shard file: %s",
                          dominoErrorCodeToString(*result));
        return nullptr;
    }
    return doc;
}
