#include "io.h"

#include "domino_shared_common.h"
#include "entry.h"
#include "sub_module.h"

void storageBuildShardPath(char* buffer, size_t buffer_size, const char* save_path, const char* type_name, uint32_t shard_id) {
    int written;
    if (strcmp(type_name, "regions") == 0) {
        written = snprintf(buffer, buffer_size, "%s/regions.json", save_path);
    } else {
        written = snprintf(buffer, buffer_size, "%s/%s_%u.json", save_path, type_name, shard_id);
    }
    if (written < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "failed to format shard path (type=%s, shard=%u)", type_name, shard_id);
    }
    if ((size_t)written >= buffer_size) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "shard path exceeds output buffer (type=%s, shard=%u, capacity=%zu)", type_name, shard_id,
                               buffer_size);
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

yyjson_doc* storageReadJsonFile(const char* path, bool do_verify, uint32_t expect_crc32c, uint32_t expect_size) {
    size_t length = 0;
    uint8_t* data = nullptr;
    DOMINO_CODE result = dominoReadFileBytes(path, &data, &length);
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to read JSON file (path=%s)", path);
    }
    if (length == 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_JSON, "JSON file is empty (path=%s)", path);
    }
    if (do_verify) {
        if (length != (size_t)expect_size) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "JSON file size does not match metadata (path=%s, expected=%u, actual=%zu)", path, expect_size,
                                   length);
        }
        uint32_t crc = storageCrc32c(data, length);
        if (crc != expect_crc32c) {
            DOMINO_EXIT_WITH_ERROR(ERR_CHECKSUM_MISMATCH, "JSON file CRC32C does not match metadata (path=%s, expected=%u, actual=%u)", path,
                                   expect_crc32c, crc);
        }
    }
    yyjson_read_err err_json = {0};
    yyjson_doc* doc = yyjson_read_opts((char*)data, length, YYJSON_READ_NOFLAG, nullptr, &err_json);
    if (!doc) {
        DOMINO_CODE code = err_json.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ? ERR_MEMORY_ALLOC : ERR_INVALID_JSON;
        DOMINO_EXIT_WITH_ERROR(code, "failed to parse JSON file (path=%s, offset=%zu, reason=%s)", path, err_json.pos, err_json.msg);
    }
    free(data);
    return doc;
}

void storageWriteJsonFile(const char* path, yyjson_mut_doc* doc, bool do_verify, uint32_t* out_crc32c, uint32_t* out_size) {
    yyjson_write_err err;
    size_t json_len = 0;
    char* json = yyjson_mut_write_opts(doc, YYJSON_WRITE_NOFLAG, nullptr, &json_len, &err);
    if (!json) {
        DOMINO_CODE code = err.code == YYJSON_WRITE_ERROR_MEMORY_ALLOCATION ? ERR_MEMORY_ALLOC : ERR_INVALID_DATA;
        DOMINO_EXIT_WITH_ERROR(code, "failed to encode JSON file (path=%s, reason=%s)", path, err.msg);
    }
    if (json_len > (size_t)DOMINO_STORAGE_SHARD_FILE_MAX_BYTES) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "JSON file exceeds shard byte limit (path=%s, size=%zu, limit=%u)", path, json_len,
                               (unsigned)DOMINO_STORAGE_SHARD_FILE_MAX_BYTES);
    }
    /* 摘要基于最终序列化字节，与落盘内容完全一致。 */
    uint32_t crc = do_verify ? storageCrc32c(json, json_len) : 0;
    DOMINO_CODE result = dominoAtomicWriteFile(path, json, json_len);
    if (result != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(result, "failed to write JSON file (path=%s)", path);
    }
    free(json);
    if (do_verify) {
        *out_crc32c = crc;
        *out_size = (uint32_t)json_len;
    }
}

void storageWriteShardFile(const char* type_name, uint32_t shard_id, const char* save_path, yyjson_mut_doc* doc) {
    char path[DOMINO_STORAGE_PATH_MAX];
    storageBuildShardPath(path, sizeof(path), save_path, type_name, shard_id);
    uint32_t crc = 0;
    uint32_t size = 0;
    bool do_verify = storageSubModuleIntegrityVerify();
    storageWriteJsonFile(path, doc, do_verify, &crc, &size);
    if (do_verify) {
        StorageSubModuleShard shard_rec = {0};
        strncpy(shard_rec.type_name, type_name, sizeof(shard_rec.type_name) - 1);
        shard_rec.shard_id = shard_id;
        shard_rec.crc32c = crc;
        shard_rec.size = size;
        storageSubModuleShardAdd(shard_rec);
    }
}

yyjson_doc* storageReadShardFile(const char* type_name, uint32_t shard_id, const char* save_path) {
    char path[DOMINO_STORAGE_PATH_MAX];
    storageBuildShardPath(path, sizeof(path), save_path, type_name, shard_id);
    bool do_verify = storageSubModuleIntegrityVerify();
    uint32_t expect_crc32c = 0;
    uint32_t expect_size = 0;
    if (do_verify) {
        /* 元数据入口已按该模块的连续 shard_id 登记全部摘要，加载期间摘要表保持不变。 */
        const StorageSubModuleShard* shard = storageSubModuleShardFind(type_name, shard_id);
        expect_crc32c = shard->crc32c;
        expect_size = shard->size;
    }
    return storageReadJsonFile(path, do_verify, expect_crc32c, expect_size);
}
