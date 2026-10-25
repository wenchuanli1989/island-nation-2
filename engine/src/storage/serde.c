#include "serde.h"

#include <stdint.h>
#include <string.h>

#include "../common/entry.h"
#include "../host/entry.h"

void serdeReadStr(yyjson_val* object, const char* key, char* buffer, size_t buffer_size) {
    const char* str = yyjson_get_str(yyjson_obj_get(object, key));
    if (str) {
        strncpy(buffer, str, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }
}

void serdeWriteNameDescription(yyjson_mut_doc* document, yyjson_mut_val* object, domino_name_id_t name_id, domino_description_id_t description_id) {
    domino_name_t* name_ptr = dominoGetNameByID(name_id);
    domino_description_t* description_ptr = dominoGetDescriptionByID(description_id);

    if (!yyjson_mut_obj_add_strcpy(document, object, "name", name_ptr ? *name_ptr : "") ||
        !yyjson_mut_obj_add_strcpy(document, object, "description", description_ptr ? *description_ptr : "")) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate entity name and description JSON fields");
    }
}

void serdeReadNameDescription(yyjson_val* object, domino_name_id_t* out_name_id, domino_description_id_t* out_description_id) {
    const char* name_str = yyjson_get_str(yyjson_obj_get(object, "name"));
    const char* description_str = yyjson_get_str(yyjson_obj_get(object, "description"));

    if (name_str && name_str[0] != '\0') {
        domino_name_id_t new_id;
        DOMINO_ALLOC_NON_ZERO_ID(g_domino_global_increment_id.name_increment_id, new_id);

        /* 名称字符串表与 id->index 表必须同步追加，避免后续按 ID 反查失败。 */
        size_t name_old_count = kv_size(domino_all_name_list);
        domino_name_t* name_slot = (kv_pushp(domino_name_t, domino_all_name_list));
        strncpy(*name_slot, name_str, sizeof(domino_name_t) - 1);
        (*name_slot)[sizeof(domino_name_t) - 1] = '\0';

        int absent;
        khint_t name_map_key = dominoNameIdMap_put(dominoNameIdMap, new_id, &absent);
        if (absent < 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to insert restored name ID (id=%u)", new_id);
        }
        if (absent == 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_ALREADY_EXISTS, "restored name ID collides with existing name (id=%u)", new_id);
        }
        kh_val(dominoNameIdMap, name_map_key) = (uint32_t)name_old_count;

        *out_name_id = new_id;
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
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to insert restored description ID (id=%u)", new_id);
        }
        if (absent == 0) {
            DOMINO_EXIT_WITH_ERROR(ERR_ALREADY_EXISTS, "restored description ID collides with existing description (id=%u)", new_id);
        }
        kh_val(dominoDescriptionIdMap, map_key) = (uint32_t)current_count;

        *out_description_id = new_id;
    }
}

yyjson_val* serdeValidateShardRoot(yyjson_val* root, const char* expected_type, uint32_t expected_shard_id) {
    /* obj_get 和 equals_str 同时拒绝非对象根、缺失字段及错误字段类型。 */
    yyjson_val* type_val = yyjson_obj_get(root, "type");
    if (!yyjson_equals_str(type_val, expected_type)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "invalid shard root or type (expected_type=%s, shard=%u)", expected_type, expected_shard_id);
    }

    yyjson_val* shard_id_val = yyjson_obj_get(root, "shard_id");
    if (!yyjson_is_uint(shard_id_val) || yyjson_get_uint(shard_id_val) != expected_shard_id) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "invalid shard ID in JSON (type=%s, expected_shard=%u)", expected_type, expected_shard_id);
    }
    yyjson_val* entities = yyjson_obj_get(root, "entities");
    if (!yyjson_is_arr(entities)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "shard entities field is not an array (type=%s, shard=%u)", expected_type, expected_shard_id);
    }
    return entities;
}
