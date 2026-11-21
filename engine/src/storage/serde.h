#ifndef DOMINO_ENGINE_STORAGE_SERDE_H
#define DOMINO_ENGINE_STORAGE_SERDE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "domino_shared_common.h"
#include "domino_shared_types.h"
#include "yyjson/yyjson.h"

/** @file serde.h
 * @brief 实体与 JSON 的序列化工具和各模块的保存／加载回调；JSON 构造或分配失败一律致命退出。
 */

/** @brief 写出以 0 作为结束哨兵的 uint32 数组前缀。 */
static inline yyjson_mut_val* serdeWriteU32Array(yyjson_mut_doc* document, const uint32_t* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate uint32 JSON array (count=%u)", element_count);
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (array[element_index] == 0) {
            break;
        }
        if (!yyjson_mut_arr_add_uint(document, json_array, array[element_index])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to append uint32 JSON array element (index=%u)", element_index);
        }
    }
    return json_array;
}

/** @brief 写出以 0 作为结束哨兵的 uint8 数组前缀。 */
static inline yyjson_mut_val* serdeWriteU8Array(yyjson_mut_doc* document, const uint8_t* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate uint8 JSON array (count=%u)", element_count);
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (array[element_index] == 0) {
            break;
        }
        if (!yyjson_mut_arr_add_uint(document, json_array, array[element_index])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to append uint8 JSON array element (index=%u)", element_index);
        }
    }
    return json_array;
}

/** @brief 固定长度写出 uint8 数组，包含中间和末尾的 0 值。 */
static inline yyjson_mut_val* serdeWriteU8FixedArray(yyjson_mut_doc* document, const uint8_t* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate fixed uint8 JSON array (count=%u)", element_count);
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (!yyjson_mut_arr_add_uint(document, json_array, array[element_index])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to append fixed uint8 JSON array element (index=%u)", element_index);
        }
    }
    return json_array;
}

/** @brief 固定长度写出 int8 数组，包含中间和末尾的 0 值。 */
static inline yyjson_mut_val* serdeWriteI8FixedArray(yyjson_mut_doc* document, const int8_t* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate fixed int8 JSON array (count=%u)", element_count);
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (!yyjson_mut_arr_add_sint(document, json_array, array[element_index])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to append fixed int8 JSON array element (index=%u)", element_index);
        }
    }
    return json_array;
}

/** @brief 固定长度写出 float 数组。 */
static inline yyjson_mut_val* serdeWriteFloatArray(yyjson_mut_doc* document, const float* array, uint32_t element_count) {
    yyjson_mut_val* json_array = yyjson_mut_arr(document);
    if (!json_array) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate float JSON array (count=%u)", element_count);
    }
    for (uint32_t element_index = 0; element_index < element_count; element_index++) {
        if (!yyjson_mut_arr_add_real(document, json_array, (double)array[element_index])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to append float JSON array element (index=%u)", element_index);
        }
    }
    return json_array;
}

/* 数组读取：按 JSON 顺序写入，超出容量的元素丢弃；缺失或非数组时不写，目标保留原值（加载入口已清零）。 */

static inline void serdeReadU32Array(yyjson_val* json_array, uint32_t* array, uint32_t max_element_count) {
    size_t element_index;
    size_t element_count;
    yyjson_val* element_value;
    yyjson_arr_foreach(json_array, element_index, element_count, element_value) {
        if (element_index >= max_element_count) {
            break;
        }
        array[element_index] = (uint32_t)yyjson_get_uint(element_value);
    }
}

static inline void serdeReadU8Array(yyjson_val* json_array, uint8_t* array, uint32_t max_element_count) {
    size_t element_index;
    size_t element_count;
    yyjson_val* element_value;
    yyjson_arr_foreach(json_array, element_index, element_count, element_value) {
        if (element_index >= max_element_count) {
            break;
        }
        array[element_index] = (uint8_t)yyjson_get_uint(element_value);
    }
}

/** @brief 先清零整个目标再读取，短数组的剩余部分为 0。 */
static inline void serdeReadI8FixedArray(yyjson_val* json_array, int8_t* array, uint32_t element_count) {
    memset(array, 0, (size_t)element_count);
    size_t element_index;
    size_t element_count_json;
    yyjson_val* element_value;
    yyjson_arr_foreach(json_array, element_index, element_count_json, element_value) {
        if (element_index >= element_count) {
            break;
        }
        array[element_index] = (int8_t)yyjson_get_sint(element_value);
    }
}

static inline void serdeReadFloatArray(yyjson_val* json_array, float* array, uint32_t max_element_count) {
    size_t element_index;
    size_t element_count;
    yyjson_val* element_value;
    yyjson_arr_foreach(json_array, element_index, element_count, element_value) {
        if (element_index >= max_element_count) {
            break;
        }
        array[element_index] = (float)yyjson_get_real(element_value);
    }
}

static inline void serdeReadSintVec3(yyjson_val* object, const char* key, int32_t* out_x, int32_t* out_y, int32_t* out_z) {
    yyjson_val* arr = yyjson_obj_get(object, key);
    for (size_t i = 0; i < 3; i++) {
        yyjson_val* elem_val = yyjson_arr_get(arr, i);
        int32_t axis_val = (int32_t)yyjson_get_sint(elem_val);
        if (i == 0) {
            *out_x = axis_val;
        } else if (i == 1) {
            *out_y = axis_val;
        } else {
            *out_z = axis_val;
        }
    }
}

static inline void serdeReadSintVec2(yyjson_val* object, const char* key, int32_t* out_x, int32_t* out_y) {
    yyjson_val* arr = yyjson_obj_get(object, key);
    for (size_t i = 0; i < 2; i++) {
        yyjson_val* elem_val = yyjson_arr_get(arr, i);
        int32_t axis_val = (int32_t)yyjson_get_sint(elem_val);
        if (i == 0) {
            *out_x = axis_val;
        } else {
            *out_y = axis_val;
        }
    }
}

static inline void serdeReadFloatVec3(yyjson_val* object, const char* key, float* out_xyz) {
    yyjson_val* arr = yyjson_obj_get(object, key);
    for (size_t i = 0; i < 3; i++) {
        yyjson_val* elem_val = yyjson_arr_get(arr, i);
        out_xyz[i] = (float)yyjson_get_real(elem_val);
    }
}

/** @brief 写出三维整数坐标。 */
static inline yyjson_mut_val* serdeWriteSintVec3(yyjson_mut_doc* document, int32_t coord_x, int32_t coord_y, int32_t coord_z) {
    yyjson_mut_val* arr = yyjson_mut_arr(document);
    if (!arr || !yyjson_mut_arr_add_sint(document, arr, coord_x) || !yyjson_mut_arr_add_sint(document, arr, coord_y) ||
        !yyjson_mut_arr_add_sint(document, arr, coord_z)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate integer coordinate JSON array");
    }
    return arr;
}

/** @brief 写出二维整数坐标。 */
static inline yyjson_mut_val* serdeWriteSintVec2(yyjson_mut_doc* document, int32_t coord_x, int32_t coord_y) {
    yyjson_mut_val* arr = yyjson_mut_arr(document);
    if (!arr || !yyjson_mut_arr_add_sint(document, arr, coord_x) || !yyjson_mut_arr_add_sint(document, arr, coord_y)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate integer coordinate JSON array");
    }
    return arr;
}

/** @brief 读取字符串字段，截断到缓冲区并补 NUL；字段缺失或类型不符时保留原内容。buffer_size 须大于 0。 */
void serdeReadStr(yyjson_val* object, const char* key, char* buffer, size_t buffer_size);

/** @brief 把名称和描述按字符串写出；它们的 ID 只在本次运行有效，加载时重新分配。 */
void serdeWriteNameDescription(yyjson_mut_doc* document, yyjson_mut_val* object, domino_name_id_t name_id, domino_description_id_t description_id);

/** @brief 读取名称和描述，登记到字符串表并输出新分配的 ID；字段为空时不改输出。 */
void serdeReadNameDescription(yyjson_val* object, domino_name_id_t* out_name_id, domino_description_id_t* out_description_id);

/** @brief 模块 meta 扩展字段：写出 fork_road 或 road 的 Nav 分段。 */
void serdeWriteNavSegments(yyjson_mut_doc* doc, yyjson_mut_val* root, const char* type_name);

/** @brief 模块 meta 扩展字段：恢复 fork_road 或 road 的 Nav 分段。 */
void serdeReadNavSegments(yyjson_val* root, const char* type_name);

/* 各模块的保存／加载回调，由注册表按顺序调用；加载从空容器开始、每类一次，信任存档内容（宪法第六章），
 * 保存直接写出运行时数据。容量、分配和 IO 失败仍致命退出。 */
void serdeSaveHuman(const char* save_path);
void serdeLoadHuman(const char* save_path, uint32_t total_count, uint32_t shard_count);
void serdeBindHumanReferences(void);

void serdeSaveHumanTask(const char* save_path);
void serdeLoadHumanTask(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveHumanBehavior(const char* save_path);
void serdeLoadHumanBehavior(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveOrg(const char* save_path);
void serdeLoadOrg(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveBuilding(const char* save_path);
void serdeLoadBuilding(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveCity(const char* save_path);
void serdeLoadCity(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveCountry(const char* save_path);
void serdeLoadCountry(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveAsset(const char* save_path);
void serdeLoadAsset(const char* save_path, uint32_t total_count, uint32_t shard_count);
void serdeBindAssetReferences(void);

void serdeSaveForkRoad(const char* save_path);
void serdeLoadForkRoad(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveRoad(const char* save_path);
void serdeLoadRoad(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveRoadLine(const char* save_path);
void serdeLoadRoadLine(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveIsland(const char* save_path);
void serdeLoadIsland(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveMovableObject(const char* save_path);
void serdeLoadMovableObject(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveRegion(const char* save_path);
void serdeLoadRegion(const char* save_path, uint32_t total_count, uint32_t shard_count);

void serdeSaveHumanMotivation(const char* save_path);
void serdeLoadHumanMotivation(const char* save_path, uint32_t total_count, uint32_t shard_count);

#endif
