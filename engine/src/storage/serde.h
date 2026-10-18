#ifndef DOMINO_ENGINE_STORAGE_SERDE_H
#define DOMINO_ENGINE_STORAGE_SERDE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"
#include "yyjson/yyjson.h"

/** @brief 写出以 0 作为结束哨兵的 uint32 数组前缀；JSON 构造失败返回 nullptr。 */
static inline yyjson_mut_val* serdeWriteU32Array(yyjson_mut_doc* document, const uint32_t* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        return nullptr;
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (array[element_index] == 0) {
            break;
        }
        if (!yyjson_mut_arr_add_uint(document, json_array, array[element_index])) {
            return nullptr;
        }
    }
    return json_array;
}

/** @brief 写出以 0 作为结束哨兵的 uint8 数组前缀；JSON 构造失败返回 nullptr。 */
static inline yyjson_mut_val* serdeWriteU8Array(yyjson_mut_doc* document, const uint8_t* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        return nullptr;
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (array[element_index] == 0) {
            break;
        }
        if (!yyjson_mut_arr_add_uint(document, json_array, array[element_index])) {
            return nullptr;
        }
    }
    return json_array;
}

/** @brief 固定长度写出 uint8 数组，包含中间和末尾的 0 值；JSON 构造失败返回 nullptr。 */
static inline yyjson_mut_val* serdeWriteU8FixedArray(yyjson_mut_doc* document, const uint8_t* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        return nullptr;
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (!yyjson_mut_arr_add_uint(document, json_array, array[element_index])) {
            return nullptr;
        }
    }
    return json_array;
}

/** @brief 固定长度写出 int8 数组，包含中间和末尾的 0 值；JSON 构造失败返回 nullptr。 */
static inline yyjson_mut_val* serdeWriteI8FixedArray(yyjson_mut_doc* document, const int8_t* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        return nullptr;
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (!yyjson_mut_arr_add_sint(document, json_array, array[element_index])) {
            return nullptr;
        }
    }
    return json_array;
}

/** @brief 固定长度写出 float 数组；JSON 构造失败返回 nullptr。 */
static inline yyjson_mut_val* serdeWriteFloatArray(yyjson_mut_doc* document, const float* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        return nullptr;
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (!yyjson_mut_arr_add_real(document, json_array, (double)array[element_index])) {
            return nullptr;
        }
    }
    return json_array;
}

/** @brief 按 JSON 数组顺序读取 uint32 数组，超出目标容量的元素会被丢弃。 */
static inline void serdeReadU32Array(yyjson_val* json_array, uint32_t* array, uint32_t max_element_count) {
    if (!yyjson_is_arr(json_array)) {
        return;
    }
    size_t element_index;
    size_t element_count;
    yyjson_val* element_value;
    uint32_t array_write_index = 0;
    yyjson_arr_foreach(json_array, element_index, element_count, element_value) {
        if (array_write_index >= max_element_count) {
            break;
        }
        array[array_write_index++] = (uint32_t)yyjson_get_uint(element_value);
    }
}

static inline void serdeReadU8Array(yyjson_val* json_array, uint8_t* array, uint32_t max_element_count) {
    if (!yyjson_is_arr(json_array)) {
        return;
    }
    size_t element_index;
    size_t element_count;
    yyjson_val* element_value;
    uint32_t array_write_index = 0;
    yyjson_arr_foreach(json_array, element_index, element_count, element_value) {
        if (array_write_index >= max_element_count) {
            break;
        }
        array[array_write_index++] = (uint8_t)yyjson_get_uint(element_value);
    }
}

/** @brief 读取固定长度 uint8 数组，读取前先清零目标内存。 */
static inline void serdeReadU8FixedArray(yyjson_val* json_array, uint8_t* array, uint32_t element_count) {
    memset(array, 0, (size_t)element_count);
    serdeReadU8Array(json_array, array, element_count);
}

static inline void serdeReadI8FixedArray(yyjson_val* json_array, int8_t* array, uint32_t element_count) {
    memset(array, 0, (size_t)element_count);
    if (!yyjson_is_arr(json_array)) {
        return;
    }
    size_t element_index;
    size_t element_count_json;
    yyjson_val* element_value;
    uint32_t array_write_index = 0;
    yyjson_arr_foreach(json_array, element_index, element_count_json, element_value) {
        if (array_write_index >= element_count) {
            break;
        }
        array[array_write_index++] = (int8_t)yyjson_get_sint(element_value);
    }
}

static inline void serdeReadFloatArray(yyjson_val* json_array, float* array, uint32_t max_element_count) {
    if (!yyjson_is_arr(json_array)) {
        return;
    }
    size_t element_index;
    size_t element_count;
    yyjson_val* element_value;
    uint32_t array_write_index = 0;
    yyjson_arr_foreach(json_array, element_index, element_count, element_value) {
        if (array_write_index >= max_element_count) {
            break;
        }
        array[array_write_index++] = (float)yyjson_get_real(element_value);
    }
}

static inline int serdeReadSintVec3FromJson(yyjson_val* object, const char* key, int32_t* out_x, int32_t* out_y, int32_t* out_z) {
    yyjson_val* arr = yyjson_obj_get(object, key);
    if (!yyjson_is_arr(arr) || yyjson_arr_size(arr) != 3) {
        return -1;
    }
    for (size_t i = 0; i < 3; i++) {
        yyjson_val* elem_val = yyjson_arr_get(arr, i);
        if (!yyjson_is_sint(elem_val) && !yyjson_is_uint(elem_val)) {
            return -1;
        }
        int32_t axis_val = (int)yyjson_is_sint(elem_val) ? (int32_t)yyjson_get_sint(elem_val) : (int32_t)yyjson_get_uint(elem_val);
        if (i == 0) {
            *out_x = axis_val;
        } else if (i == 1) {
            *out_y = axis_val;
        } else {
            *out_z = axis_val;
        }
    }
    return 0;
}

static inline int serdeReadSintVec2FromJson(yyjson_val* object, const char* key, int32_t* out_x, int32_t* out_y) {
    yyjson_val* arr = yyjson_obj_get(object, key);
    if (!yyjson_is_arr(arr) || yyjson_arr_size(arr) != 2) {
        return -1;
    }
    for (size_t i = 0; i < 2; i++) {
        yyjson_val* elem_val = yyjson_arr_get(arr, i);
        if (!yyjson_is_sint(elem_val) && !yyjson_is_uint(elem_val)) {
            return -1;
        }
        int32_t axis_val = (int)yyjson_is_sint(elem_val) ? (int32_t)yyjson_get_sint(elem_val) : (int32_t)yyjson_get_uint(elem_val);
        if (i == 0) {
            *out_x = axis_val;
        } else {
            *out_y = axis_val;
        }
    }
    return 0;
}

static inline int serdeReadFloatVec3FromJson(yyjson_val* object, const char* key, float* out_xyz) {
    yyjson_val* arr = yyjson_obj_get(object, key);
    if (!yyjson_is_arr(arr) || yyjson_arr_size(arr) != 3) {
        return -1;
    }
    for (size_t i = 0; i < 3; i++) {
        yyjson_val* elem_val = yyjson_arr_get(arr, i);
        if (!yyjson_is_real(elem_val)) {
            return -1;
        }
        out_xyz[i] = (float)yyjson_get_real(elem_val);
    }
    return 0;
}

/** @brief 写出三维整数坐标；JSON 构造失败返回 nullptr。 */
static inline yyjson_mut_val* serdeWriteSintVec3(yyjson_mut_doc* document, int32_t coord_x, int32_t coord_y, int32_t coord_z) {
    yyjson_mut_val* arr = yyjson_mut_arr(document);
    if (!arr || !yyjson_mut_arr_add_sint(document, arr, coord_x) || !yyjson_mut_arr_add_sint(document, arr, coord_y) ||
        !yyjson_mut_arr_add_sint(document, arr, coord_z)) {
        return nullptr;
    }
    return arr;
}

/** @brief 写出二维整数坐标；JSON 构造失败返回 nullptr。 */
static inline yyjson_mut_val* serdeWriteSintVec2(yyjson_mut_doc* document, int32_t coord_x, int32_t coord_y) {
    yyjson_mut_val* arr = yyjson_mut_arr(document);
    if (!arr || !yyjson_mut_arr_add_sint(document, arr, coord_x) || !yyjson_mut_arr_add_sint(document, arr, coord_y)) {
        return nullptr;
    }
    return arr;
}

/**
 * @brief 字符串截断到缓冲区并补零；字段缺失或类型不匹配时保留原内容。
 * @note 调用方提供非空 buffer 和大于 0 的 buffer_size。
 */
void serdeReadStr(yyjson_val* object, const char* key, char* buffer, size_t buffer_size);

/** @brief 名称和描述保存为字符串，运行期 ID 在加载时重新分配；JSON 构造失败返回 false。 */
bool serdeWriteNameDescription(yyjson_mut_doc* document, yyjson_mut_val* object, domino_name_id_t name_id, domino_description_id_t description_id);

/** @brief 读取名称和描述并分配运行期 ID；两个输出地址均由调用方提供有效实体字段。 */
DOMINO_CODE serdeReadNameDescription(yyjson_val* object, domino_name_id_t* out_name_id, domino_description_id_t* out_description_id);

/** @brief 校验分片类型、分片 ID 和实体数组；expected_type 为注册器中的非空类型名。 */
yyjson_val* serdeValidateShardRoot(yyjson_val* root, const char* expected_type, uint32_t expected_shard_id);

/** @brief 写出 Nav 分段；doc/root 已创建，type_name 为注册器中的 fork_road 或 road。 */
DOMINO_CODE serdeNavWriteModuleMetaExtraFields(yyjson_mut_doc* doc, yyjson_mut_val* root, const char* type_name);

/** @brief 读取 Nav 分段；元数据入口已校验 root、type_name 和 total_count。 */
DOMINO_CODE serdeNavReadModuleMetaExtraFields(yyjson_val* root, const char* type_name, uint32_t total_count);

DOMINO_CODE serdeSaveHuman(const char* save_path);
DOMINO_CODE serdeLoadHuman(const char* save_path, uint32_t total_count, uint32_t shard_count);
/** @brief 加载与保存共用的人物/data 身份、容量和位置引用前置校验。 */
DOMINO_CODE serdeValidateHumanReferences(void);
void serdeBindHumanReferences(void);

DOMINO_CODE serdeSaveHumanAI(const char* save_path);
DOMINO_CODE serdeLoadHumanAI(const char* save_path, uint32_t total_count, uint32_t shard_count);
/** @brief 先校验人物前置，再校验任务恢复数据；供统一加载及 AI 保存入口调用。 */
DOMINO_CODE serdeValidateHumanAIReferences(void);
void serdeBindHumanAIReferences(void);

DOMINO_CODE serdeSaveOrg(const char* save_path);
DOMINO_CODE serdeLoadOrg(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveBuilding(const char* save_path);
DOMINO_CODE serdeLoadBuilding(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveCity(const char* save_path);
DOMINO_CODE serdeLoadCity(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveCountry(const char* save_path);
DOMINO_CODE serdeLoadCountry(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveAsset(const char* save_path);
DOMINO_CODE serdeLoadAsset(const char* save_path, uint32_t total_count, uint32_t shard_count);
DOMINO_CODE serdeValidateAssetReferences(void);
void serdeBindAssetReferences(void);

DOMINO_CODE serdeSaveForkRoad(const char* save_path);
DOMINO_CODE serdeLoadForkRoad(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveRoad(const char* save_path);
DOMINO_CODE serdeLoadRoad(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveRoadLine(const char* save_path);
DOMINO_CODE serdeLoadRoadLine(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveIsland(const char* save_path);
DOMINO_CODE serdeLoadIsland(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveMovableObject(const char* save_path);
DOMINO_CODE serdeLoadMovableObject(const char* save_path, uint32_t total_count, uint32_t shard_count);

DOMINO_CODE serdeSaveRegion(const char* save_path);
DOMINO_CODE serdeLoadRegion(const char* save_path, uint32_t total_count, uint32_t shard_count);

#endif
