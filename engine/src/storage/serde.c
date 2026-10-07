#include "serde.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/entry.h"
#include "../host/entry.h"
#include "../logger/entry.h"

void serdeReadStr(yyjson_val* object, const char* key, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return;
    }
    const char* str = yyjson_get_str(yyjson_obj_get(object, key));
    if (str) {
        strncpy(buffer, str, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }
}

void serdeWriteNameDescription(yyjson_mut_doc* document, yyjson_mut_val* object, domino_name_id_t name_id, domino_description_id_t description_id) {
    domino_name_t* name_ptr = dominoGetNameByID(name_id);
    domino_description_t* description_ptr = dominoGetDescriptionByID(description_id);

    yyjson_mut_obj_add_strcpy(document, object, "name", name_ptr ? *name_ptr : "");
    yyjson_mut_obj_add_strcpy(document, object, "description", description_ptr ? *description_ptr : "");
}

DOMINO_CODE serdeReadNameDescription(yyjson_val* object, domino_name_id_t* out_name_id, domino_description_id_t* out_description_id) {
    if (!out_name_id || !out_description_id) {
        return ERR_NULL_POINTER;
    }
    *out_name_id = 0;
    *out_description_id = 0;

    const char* name_str = yyjson_get_str(yyjson_obj_get(object, "name"));
    const char* description_str = yyjson_get_str(yyjson_obj_get(object, "description"));
    bool name_inserted = false;
    khint_t name_map_key = 0;
    size_t name_old_count = 0;

    if (name_str && name_str[0] != '\0') {
        domino_name_id_t new_id;
        DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.name_increment_id, new_id);

        /* 名称字符串表与 id->index 表必须同步追加，避免后续按 ID 反查失败。 */
        name_old_count = kv_size(domino_all_name_list);
        domino_name_t* name_slot = (kv_pushp(domino_name_t, domino_all_name_list));
        strncpy(*name_slot, name_str, sizeof(domino_name_t) - 1);
        (*name_slot)[sizeof(domino_name_t) - 1] = '\0';

        int absent;
        name_map_key = dominoNameIdMap_put(dominoNameIdMap, new_id, &absent);
        if (absent < 0) {
            domino_all_name_list.n = name_old_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to map name id=%u", (uint32_t)new_id);
            return ERR_MEMORY_ALLOC;
        }
        if (absent == 0) {
            domino_all_name_list.n = name_old_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate name id=%u", (uint32_t)new_id);
            return ERR_ALREADY_EXISTS;
        }
        kh_val(dominoNameIdMap, name_map_key) = (uint32_t)name_old_count;

        *out_name_id = new_id;
        name_inserted = true;
    }
    if (description_str && description_str[0] != '\0') {
        domino_description_id_t new_id;
        DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.description_increment_id, new_id);

        size_t current_count = kv_size(domino_all_description_list);
        domino_description_t* description_slot = (kv_pushp(domino_description_t, domino_all_description_list));
        strncpy(*description_slot, description_str, sizeof(domino_description_t) - 1);
        (*description_slot)[sizeof(domino_description_t) - 1] = '\0';

        int absent;
        khint_t map_key = dominoDescriptionIdMap_put(dominoDescriptionIdMap, new_id, &absent);
        if (absent < 0) {
            domino_all_description_list.n = current_count;
            if (name_inserted) {
                dominoNameIdMap_del(dominoNameIdMap, name_map_key);
                domino_all_name_list.n = name_old_count;
                *out_name_id = 0;
            }
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to map description id=%u", (uint32_t)new_id);
            return ERR_MEMORY_ALLOC;
        }
        if (absent == 0) {
            domino_all_description_list.n = current_count;
            if (name_inserted) {
                dominoNameIdMap_del(dominoNameIdMap, name_map_key);
                domino_all_name_list.n = name_old_count;
                *out_name_id = 0;
            }
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate description id=%u", (uint32_t)new_id);
            return ERR_ALREADY_EXISTS;
        }
        kh_val(dominoDescriptionIdMap, map_key) = (uint32_t)current_count;

        *out_description_id = new_id;
    }
    return CODE_OK;
}

yyjson_val* serdeRequireField(yyjson_val* object, const char* key, const char* file_tag, bool (*type_check)(yyjson_val*)) {
    yyjson_val* val = yyjson_obj_get(object, key);
    if (val == nullptr) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: missing %s", file_tag ? file_tag : "json", key);
        return nullptr;
    }
    if (type_check && !type_check(val)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: field %s has wrong type",
                          file_tag ? file_tag : "json", key);
        return nullptr;
    }
    return val;
}

yyjson_val* serdeValidateShardRoot(yyjson_val* root, const char* expected_type, const char* file_tag, uint32_t expected_shard_id) {
    if (!root || !yyjson_is_obj(root)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: root is not object", file_tag ? file_tag : "shard");
        return nullptr;
    }

    yyjson_val* type_val = serdeRequireField(root, "type", file_tag, yyjson_is_str);
    if (!type_val) {
        return nullptr;
    }
    const char* type_str = yyjson_get_str(type_val);
    if (!type_str || (expected_type && strcmp(type_str, expected_type) != 0)) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: type mismatch (expect %s, got %s)",
                          file_tag ? file_tag : "shard", expected_type ? expected_type : "(any)", type_str ? type_str : "(null)");
        return nullptr;
    }

    yyjson_val* shard_id_val = serdeRequireField(root, "shard_id", file_tag, yyjson_is_uint);
    if (!shard_id_val) {
        return nullptr;
    }
    uint32_t shard_id = (uint32_t)yyjson_get_uint(shard_id_val);
    if (expected_shard_id != UINT32_MAX && shard_id != expected_shard_id) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "invalid %s: shard_id mismatch (expect %u, got %u)",
                          file_tag ? file_tag : "shard", expected_shard_id, shard_id);
        return nullptr;
    }
    return serdeRequireField(root, "entities", file_tag, yyjson_is_arr);
}
