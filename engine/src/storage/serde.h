#ifndef DOMINO_ENGINE_STORAGE_SERDE_H
#define DOMINO_ENGINE_STORAGE_SERDE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "domino_shared_common.h"
#include "domino_shared_types.h"
#include "yyjson/yyjson.h"

/** @brief 写出以 0 作为结束哨兵的 uint32 数组前缀；JSON 构造失败直接打印并终止。 */
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

/** @brief 写出以 0 作为结束哨兵的 uint8 数组前缀；JSON 构造失败直接打印并终止。 */
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

/** @brief 固定长度写出 uint8 数组，包含中间和末尾的 0 值；JSON 构造失败直接打印并终止。 */
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

/** @brief 固定长度写出 int8 数组，包含中间和末尾的 0 值；JSON 构造失败直接打印并终止。 */
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

/** @brief 固定长度写出 float 数组；JSON 构造失败直接打印并终止。 */
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

/* 内部数组读取契约：目标数组有效；未写入的元素保留原值，实体加载入口负责初始清零。
 * yyjson_arr_foreach 对缺失或非数组输入不迭代；目标容量仍在循环内限制。
 */

/** @brief 按 JSON 数组顺序读取 uint32 数组，超出目标容量的元素会被丢弃。 */
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

/** @brief 读取 int8 数组前清零整个目标，短数组的剩余部分保持为 0。 */
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

static inline void serdeReadSintVec3FromJson(yyjson_val* object, const char* key, int32_t* out_x, int32_t* out_y, int32_t* out_z) {
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

static inline void serdeReadSintVec2FromJson(yyjson_val* object, const char* key, int32_t* out_x, int32_t* out_y) {
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

static inline void serdeReadFloatVec3FromJson(yyjson_val* object, const char* key, float* out_xyz) {
    yyjson_val* arr = yyjson_obj_get(object, key);
    for (size_t i = 0; i < 3; i++) {
        yyjson_val* elem_val = yyjson_arr_get(arr, i);
        out_xyz[i] = (float)yyjson_get_real(elem_val);
    }
}

/** @brief 写出三维整数坐标；JSON 构造失败直接打印并终止。 */
static inline yyjson_mut_val* serdeWriteSintVec3(yyjson_mut_doc* document, int32_t coord_x, int32_t coord_y, int32_t coord_z) {
    yyjson_mut_val* arr = yyjson_mut_arr(document);
    if (!arr || !yyjson_mut_arr_add_sint(document, arr, coord_x) || !yyjson_mut_arr_add_sint(document, arr, coord_y) ||
        !yyjson_mut_arr_add_sint(document, arr, coord_z)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate integer coordinate JSON array");
    }
    return arr;
}

/** @brief 写出二维整数坐标；JSON 构造失败直接打印并终止。 */
static inline yyjson_mut_val* serdeWriteSintVec2(yyjson_mut_doc* document, int32_t coord_x, int32_t coord_y) {
    yyjson_mut_val* arr = yyjson_mut_arr(document);
    if (!arr || !yyjson_mut_arr_add_sint(document, arr, coord_x) || !yyjson_mut_arr_add_sint(document, arr, coord_y)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "failed to allocate integer coordinate JSON array");
    }
    return arr;
}

/**
 * @brief 字符串截断到缓冲区并补零；字段缺失或类型不匹配时保留原内容。
 * @note 调用方提供非空 buffer 和大于 0 的 buffer_size。
 */
void serdeReadStr(yyjson_val* object, const char* key, char* buffer, size_t buffer_size);

/** @brief 名称和描述保存为字符串，运行期 ID 在加载时重新分配；JSON 构造失败直接打印并终止。 */
void serdeWriteNameDescription(yyjson_mut_doc* document, yyjson_mut_val* object, domino_name_id_t name_id, domino_description_id_t description_id);

/** @brief 读取名称和描述并分配运行期 ID；两个输出地址均由调用方提供有效且已清零的实体字段。 */
void serdeReadNameDescription(yyjson_val* object, domino_name_id_t* out_name_id, domino_description_id_t* out_description_id);

/** @brief 写出 Nav 分段；doc/root 已创建，type_name 为注册器中的 fork_road 或 road。 */
void serdeNavWriteModuleMetaExtraFields(yyjson_mut_doc* doc, yyjson_mut_val* root, const char* type_name);

/** @brief 读取 Nav 分段；root 与 type_name 来自当前格式的可信元数据。
 * @pre 引擎已初始化空分段容器，每轮按注册类型各加载一次。
 */
void serdeNavReadModuleMetaExtraFields(yyjson_val* root, const char* type_name);

/**
 * @brief 以下内部序列化回调由 storage 按注册表顺序调用。
 * @pre 世界容器和 ID 映射已由引擎初始化；加载从空容器开始，每类只调用一次，保存发生在世界资源释放前。
 * @note 加载信任当前格式存档的字段、数量、ID 和引用；仍检查本次资源分配、容量和 IO 失败。
 * 保存回调仅由 saveAllShards 调用，直接写出业务边界已保证正确的运行时数据，保留容量及本次资源操作检查。
 */
void serdeSaveHuman(const char* save_path);
void serdeLoadHuman(const char* save_path, uint32_t total_count, uint32_t shard_count);
void serdeBindHumanReferences(void);

void serdeSaveHumanAI(const char* save_path);
void serdeLoadHumanAI(const char* save_path, uint32_t total_count, uint32_t shard_count);
void serdeBindHumanAIReferences(void);

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

#endif
