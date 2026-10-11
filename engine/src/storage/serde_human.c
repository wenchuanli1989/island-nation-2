#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../host/entry.h"
#include "../human/entry.h"
#include "../logger/entry.h"
#include "../social/entry.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/** @brief 保存人物扩展数据与 AI 事实；任务内容由 human_ai 模块保存，调度运行字段不落盘。 */
static yyjson_mut_val* serializeHumanData(yyjson_mut_doc* doc, const DominoHumanData* human_data) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);

    if (!obj || !yyjson_mut_obj_add_uint(doc, obj, "human_id", human_data->human_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "random_state", human_data->random_state) ||
        !yyjson_mut_obj_add_uint(doc, obj, "schedule_revision", human_data->schedule_revision) ||
        !yyjson_mut_obj_add_uint(doc, obj, "needs_updated_at_ms", human_data->needs_updated_at_ms)) {
        return nullptr;
    }
    /* 只保存有效 ID，保持提交顺序；数组长度即 task_count。 */
    yyjson_mut_val* task_ids = yyjson_mut_obj_add_arr(doc, obj, "task_id_list");
    if (!task_ids) {
        return nullptr;
    }
    for (size_t index = 0; index < human_data->task_count; index++) {
        if (!yyjson_mut_arr_add_uint(doc, task_ids, human_data->task_id_list[index])) {
            return nullptr;
        }
    }
    yyjson_mut_val* slots = yyjson_mut_obj_add_arr(doc, obj, "active_task_id_list");
    if (!slots) {
        return nullptr;
    }
    for (size_t index = 0; index < DOMINO_HUMAN_TASK_CHANNEL_COUNT; index++) {
        if (!yyjson_mut_arr_add_uint(doc, slots, human_data->active_task_id_list[index])) {
            return nullptr;
        }
    }

    yyjson_mut_obj_add_uint(doc, obj, "family_org_id", human_data->family_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "company_org_id", human_data->company_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "country_id", human_data->country_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_country_id", human_data->current_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_city_id", human_data->current_city_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_building_id", human_data->current_building_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_island_id", human_data->current_island_id);
    yyjson_mut_obj_add_sint(doc, obj, "wealth", human_data->wealth);
    yyjson_mut_obj_add_uint(doc, obj, "current_address", human_data->current_address);
    yyjson_mut_obj_add_uint(doc, obj, "family_address", human_data->family_address);
    yyjson_mut_obj_add_uint(doc, obj, "company_address", human_data->company_address);
    yyjson_mut_obj_add_uint(doc, obj, "sleep_address", human_data->sleep_address);
    yyjson_mut_obj_add_uint(doc, obj, "outer_host_country_id", human_data->outer_host_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "health", human_data->health);
    yyjson_mut_obj_add_uint(doc, obj, "energy", human_data->energy);
    yyjson_mut_obj_add_uint(doc, obj, "food", human_data->food);
    yyjson_mut_obj_add_uint(doc, obj, "water", human_data->water);
    yyjson_mut_obj_add_uint(doc, obj, "emotion", human_data->emotion);
    yyjson_mut_obj_add_uint(doc, obj, "nutrition", human_data->nutrition);
    yyjson_mut_obj_add_uint(doc, obj, "hygiene", human_data->hygiene);
    yyjson_mut_obj_add_uint(doc, obj, "sleep", human_data->sleep);
    yyjson_mut_obj_add_uint(doc, obj, "exercise", human_data->exercise);
    yyjson_mut_obj_add_uint(doc, obj, "recreation", human_data->recreation);
    yyjson_mut_obj_add_uint(doc, obj, "social", human_data->social);
    yyjson_mut_obj_add_uint(doc, obj, "family_love", human_data->family_love);
    yyjson_mut_obj_add_uint(doc, obj, "couple_love", human_data->couple_love);
    yyjson_mut_obj_add_uint(doc, obj, "friendship", human_data->friendship);
    yyjson_mut_obj_add_uint(doc, obj, "job", human_data->job);
    yyjson_mut_obj_add_uint(doc, obj, "influence", human_data->influence);
    yyjson_mut_obj_add_uint(doc, obj, "fame", human_data->fame);
    yyjson_mut_obj_add_uint(doc, obj, "body_hp_value", human_data->body_hp_value);
    yyjson_mut_obj_add_uint(doc, obj, "gender", human_data->gender);
    yyjson_mut_obj_add_uint(doc, obj, "age", human_data->age);
    yyjson_mut_obj_add_uint(doc, obj, "intelligence", human_data->intelligence);
    yyjson_mut_obj_add_uint(doc, obj, "talent", human_data->talent);
    yyjson_mut_obj_add_uint(doc, obj, "character", human_data->character);
    yyjson_mut_obj_add_uint(doc, obj, "hobby", human_data->hobby);
    yyjson_mut_obj_add_uint(doc, obj, "belief", human_data->belief);
    yyjson_mut_obj_add_uint(doc, obj, "world_view", human_data->world_view);
    yyjson_mut_obj_add_uint(doc, obj, "life_view", human_data->life_view);
    yyjson_mut_obj_add_uint(doc, obj, "value_view", human_data->value_view);
    yyjson_mut_obj_add_uint(doc, obj, "family_info", human_data->family_info);
    yyjson_mut_obj_add_uint(doc, obj, "org_info", human_data->org_info);
    yyjson_mut_obj_add_uint(doc, obj, "hold_asset_list_count", human_data->hold_asset_list_count);
    yyjson_mut_obj_add_uint(doc, obj, "own_asset_list_count", human_data->own_asset_list_count);

    yyjson_mut_obj_add_val(doc, obj, "own_asset_id_list", serdeWriteU32Array(doc, human_data->own_asset_id_list, DOMINO_MAX_ASSET_COUNT_PER_HUMAN));
    yyjson_mut_obj_add_val(doc, obj, "hold_asset_id_list",
                           serdeWriteU32Array(doc, human_data->hold_asset_id_list, DOMINO_MAX_ASSET_COUNT_HOLD_PER_HUMAN));

    yyjson_mut_obj_add_uint(doc, obj, "partner_id", human_data->partner_id);
    yyjson_mut_obj_add_uint(doc, obj, "father_id", human_data->father_id);
    yyjson_mut_obj_add_uint(doc, obj, "mother_id", human_data->mother_id);

    yyjson_mut_obj_add_val(doc, obj, "child_id_list", serdeWriteU32Array(doc, human_data->child_id_list, DOMINO_MAX_CHILD_COUNT_PER_FAMILY));
    yyjson_mut_obj_add_val(doc, obj, "sibling_id_list", serdeWriteU32Array(doc, human_data->sibling_id_list, DOMINO_MAX_CHILD_COUNT_PER_FAMILY - 1));
    yyjson_mut_obj_add_val(doc, obj, "org_id_list", serdeWriteU32Array(doc, human_data->org_id_list, DOMINO_MAX_ORG_COUNT_PER_HUMAN));
    yyjson_mut_obj_add_val(doc, obj, "org_boss_id_list", serdeWriteU32Array(doc, human_data->org_boss_id_list, DOMINO_MAX_ORG_BOSS_COUNT_PER_HUMAN));
    yyjson_mut_obj_add_val(doc, obj, "friend_id_list", serdeWriteU32Array(doc, human_data->friend_id_list, DOMINO_MAX_FRIEND_COUNT_PER_HUMAN));
    yyjson_mut_obj_add_val(doc, obj, "colleague_id_list",
                           serdeWriteU32Array(doc, human_data->colleague_id_list, DOMINO_MAX_COLLEAGUE_COUNT_PER_HUMAN));

    yyjson_mut_obj_add_uint(doc, obj, "death_time", human_data->death_time);
    yyjson_mut_obj_add_uint(doc, obj, "last_active_time", human_data->last_active_time);

    yyjson_mut_obj_add_uint(doc, obj, "last_active_country_id", human_data->last_active_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "last_active_city_id", human_data->last_active_city_id);
    yyjson_mut_obj_add_uint(doc, obj, "last_active_building_id", human_data->last_active_building_id);

    yyjson_mut_obj_add_uint(doc, obj, "birth_country_id", human_data->birth_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "birth_city_id", human_data->birth_city_id);
    yyjson_mut_obj_add_uint(doc, obj, "birth_building_id", human_data->birth_building_id);
    yyjson_mut_obj_add_uint(doc, obj, "death_country_id", human_data->death_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "death_city_id", human_data->death_city_id);
    yyjson_mut_obj_add_uint(doc, obj, "death_building_id", human_data->death_building_id);

    yyjson_mut_obj_add_val(doc, obj, "recent_contact_id_list",
                           serdeWriteU32Array(doc, human_data->recent_contact_id_list, DOMINO_MAX_RECENT_CONTACT_HUMAN_COUNT));
    yyjson_mut_obj_add_val(doc, obj, "recent_visited_address_id_list",
                           serdeWriteU32Array(doc, human_data->recent_visited_address_id_list, DOMINO_MAX_RECENT_VISITED_ADDRESS_COUNT));

    yyjson_mut_obj_add_uint(doc, obj, "last_active_region_index", human_data->last_active_region_index);
    yyjson_mut_obj_add_uint(doc, obj, "birth_region_index", human_data->birth_region_index);
    yyjson_mut_obj_add_uint(doc, obj, "death_region_index", human_data->death_region_index);
    yyjson_mut_obj_add_uint(doc, obj, "recent_contact_list_count", human_data->recent_contact_list_count);
    yyjson_mut_obj_add_uint(doc, obj, "recent_contact_id_list_head_index", human_data->recent_contact_id_list_head_index);
    yyjson_mut_obj_add_uint(doc, obj, "recent_visited_address_list_count", human_data->recent_visited_address_list_count);
    yyjson_mut_obj_add_uint(doc, obj, "recent_visited_address_id_list_head_index", human_data->recent_visited_address_id_list_head_index);

    yyjson_mut_obj_add_strcpy(doc, obj, "id_card", human_data->id_card);
    serdeWriteNameDescription(doc, obj, human_data->name_id, human_data->description_id);
    yyjson_mut_obj_add_uint(doc, obj, "generation", human_data->generation);

    yyjson_mut_obj_add_uint(doc, obj, "last_active_space_id", human_data->last_active_space_id);
    yyjson_mut_obj_add_uint(doc, obj, "last_active_area_id", human_data->last_active_area_id);
    yyjson_mut_obj_add_uint(doc, obj, "birth_space_id", human_data->birth_space_id);
    yyjson_mut_obj_add_uint(doc, obj, "birth_area_id", human_data->birth_area_id);
    yyjson_mut_obj_add_uint(doc, obj, "death_space_id", human_data->death_space_id);
    yyjson_mut_obj_add_uint(doc, obj, "death_area_id", human_data->death_area_id);

    yyjson_mut_obj_add_uint(doc, obj, "skin_color", human_data->skin_color);
    yyjson_mut_obj_add_uint(doc, obj, "hair_color", human_data->hair_color);
    yyjson_mut_obj_add_uint(doc, obj, "eye_color", human_data->eye_color);
    yyjson_mut_obj_add_uint(doc, obj, "face_shape", human_data->face_shape);
    yyjson_mut_obj_add_uint(doc, obj, "body_shape", human_data->body_shape);
    yyjson_mut_obj_add_uint(doc, obj, "voice", human_data->voice);
    yyjson_mut_obj_add_uint(doc, obj, "height", human_data->height);
    yyjson_mut_obj_add_uint(doc, obj, "weight", human_data->weight);

    yyjson_mut_obj_add_uint(doc, obj, "hat", human_data->hat);
    yyjson_mut_obj_add_uint(doc, obj, "hair", human_data->hair);
    yyjson_mut_obj_add_uint(doc, obj, "glasses", human_data->glasses);
    yyjson_mut_obj_add_uint(doc, obj, "earring", human_data->earring);
    yyjson_mut_obj_add_uint(doc, obj, "necklace", human_data->necklace);
    yyjson_mut_obj_add_uint(doc, obj, "coat", human_data->coat);
    yyjson_mut_obj_add_uint(doc, obj, "trouser", human_data->trouser);
    yyjson_mut_obj_add_uint(doc, obj, "shoe", human_data->shoe);
    yyjson_mut_obj_add_uint(doc, obj, "sock", human_data->sock);
    yyjson_mut_obj_add_uint(doc, obj, "glove", human_data->glove);
    yyjson_mut_obj_add_uint(doc, obj, "ring", human_data->ring);
    yyjson_mut_obj_add_uint(doc, obj, "bracelet", human_data->bracelet);
    yyjson_mut_obj_add_uint(doc, obj, "belt", human_data->belt);
    yyjson_mut_obj_add_uint(doc, obj, "bag", human_data->bag);

    return obj;
}

/** @brief 新的持久化状态必须显式提供合法的无符号数，禁止缺省为零或窄化截断。 */
static int readHumanUnsigned(yyjson_val* object, const char* key, uint64_t maximum, uint64_t* value_ptr) {
    yyjson_val* value = yyjson_obj_get(object, key);
    if (!yyjson_is_uint(value) || yyjson_get_uint(value) > maximum) {
        return -1;
    }
    *value_ptr = yyjson_get_uint(value);
    return 0;
}

#define HUMAN_READ_UINT(object, record, field, maximum)                       \
    do {                                                                      \
        uint64_t parsed_value;                                                \
        if (readHumanUnsigned(object, #field, maximum, &parsed_value) != 0) { \
            return -1;                                                        \
        }                                                                     \
        (record)->field = (typeof((record)->field))parsed_value;              \
    } while (0)

static int deserializeHumanData(yyjson_val* obj, DominoHumanData* out_human_data) {
    memset(out_human_data, 0, sizeof(DominoHumanData));

    HUMAN_READ_UINT(obj, out_human_data, human_id, UINT32_MAX);
    HUMAN_READ_UINT(obj, out_human_data, random_state, UINT64_MAX);
    HUMAN_READ_UINT(obj, out_human_data, schedule_revision, UINT32_MAX);
    HUMAN_READ_UINT(obj, out_human_data, needs_updated_at_ms, UINT32_MAX);
    yyjson_val* task_ids = yyjson_obj_get(obj, "task_id_list");
    if (!yyjson_is_arr(task_ids) || yyjson_arr_size(task_ids) > DOMINO_HUMAN_TASK_COUNT_MAX) {
        return -1;
    }
    out_human_data->task_count = (uint8_t)yyjson_arr_size(task_ids);
    for (size_t index = 0; index < out_human_data->task_count; index++) {
        yyjson_val* task_id = yyjson_arr_get(task_ids, index);
        if (!yyjson_is_uint(task_id) || yyjson_get_uint(task_id) == 0 || yyjson_get_uint(task_id) > UINT32_MAX) {
            return -1;
        }
        out_human_data->task_id_list[index] = (domino_task_id_t)yyjson_get_uint(task_id);
    }
    yyjson_val* slots = yyjson_obj_get(obj, "active_task_id_list");
    if (!yyjson_is_arr(slots) || yyjson_arr_size(slots) != DOMINO_HUMAN_TASK_CHANNEL_COUNT) {
        return -1;
    }
    for (size_t index = 0; index < DOMINO_HUMAN_TASK_CHANNEL_COUNT; index++) {
        yyjson_val* task_id = yyjson_arr_get(slots, index);
        if (!yyjson_is_uint(task_id) || yyjson_get_uint(task_id) > UINT32_MAX) {
            return -1;
        }
        out_human_data->active_task_id_list[index] = (domino_task_id_t)yyjson_get_uint(task_id);
    }
    out_human_data->family_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "family_org_id"));
    out_human_data->company_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "company_org_id"));
    out_human_data->country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "country_id"));
    out_human_data->current_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_country_id"));
    out_human_data->current_city_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_city_id"));
    out_human_data->current_island_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_island_id"));
    out_human_data->wealth = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "wealth"));
    out_human_data->current_address = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_address"));
    out_human_data->family_address = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "family_address"));
    out_human_data->company_address = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "company_address"));
    out_human_data->sleep_address = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "sleep_address"));
    out_human_data->outer_host_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "outer_host_country_id"));
    out_human_data->health = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "health"));
    HUMAN_READ_UINT(obj, out_human_data, energy, UINT8_MAX);
    HUMAN_READ_UINT(obj, out_human_data, food, UINT8_MAX);
    HUMAN_READ_UINT(obj, out_human_data, water, UINT8_MAX);
    out_human_data->emotion = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "emotion"));
    out_human_data->nutrition = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "nutrition"));
    HUMAN_READ_UINT(obj, out_human_data, hygiene, UINT8_MAX);
    out_human_data->sleep = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "sleep"));
    out_human_data->exercise = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "exercise"));
    HUMAN_READ_UINT(obj, out_human_data, recreation, UINT8_MAX);
    HUMAN_READ_UINT(obj, out_human_data, social, UINT8_MAX);
    out_human_data->family_love = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "family_love"));
    out_human_data->couple_love = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "couple_love"));
    out_human_data->friendship = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "friendship"));
    out_human_data->job = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "job"));
    out_human_data->influence = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "influence"));
    out_human_data->fame = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "fame"));
    out_human_data->body_hp_value = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "body_hp_value"));
    out_human_data->gender = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "gender"));
    out_human_data->age = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "age"));
    out_human_data->intelligence = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "intelligence"));
    out_human_data->talent = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "talent"));
    out_human_data->character = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "character"));
    out_human_data->hobby = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "hobby"));
    out_human_data->belief = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "belief"));
    out_human_data->world_view = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "world_view"));
    out_human_data->life_view = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "life_view"));
    out_human_data->value_view = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "value_view"));
    out_human_data->family_info = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "family_info"));
    out_human_data->org_info = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "org_info"));
    out_human_data->hold_asset_list_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "hold_asset_list_count"));
    out_human_data->own_asset_list_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "own_asset_list_count"));
    yyjson_val* current_building_id_val = yyjson_obj_get(obj, "current_building_id");
    if (!yyjson_is_uint(current_building_id_val) || yyjson_get_uint(current_building_id_val) > UINT32_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human human_id=%u invalid current_building_id",
                          out_human_data->human_id);
        return -1;
    }
    out_human_data->current_building_id = (domino_building_id_t)yyjson_get_uint(current_building_id_val);

    serdeReadU32Array(yyjson_obj_get(obj, "own_asset_id_list"), out_human_data->own_asset_id_list, DOMINO_MAX_ASSET_COUNT_PER_HUMAN);
    serdeReadU32Array(yyjson_obj_get(obj, "hold_asset_id_list"), out_human_data->hold_asset_id_list, DOMINO_MAX_ASSET_COUNT_HOLD_PER_HUMAN);

    out_human_data->partner_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "partner_id"));
    out_human_data->father_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "father_id"));
    out_human_data->mother_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "mother_id"));

    serdeReadU32Array(yyjson_obj_get(obj, "child_id_list"), out_human_data->child_id_list, DOMINO_MAX_CHILD_COUNT_PER_FAMILY);
    serdeReadU32Array(yyjson_obj_get(obj, "sibling_id_list"), out_human_data->sibling_id_list, DOMINO_MAX_CHILD_COUNT_PER_FAMILY - 1);
    serdeReadU32Array(yyjson_obj_get(obj, "org_id_list"), out_human_data->org_id_list, DOMINO_MAX_ORG_COUNT_PER_HUMAN);
    serdeReadU32Array(yyjson_obj_get(obj, "org_boss_id_list"), out_human_data->org_boss_id_list, DOMINO_MAX_ORG_BOSS_COUNT_PER_HUMAN);
    serdeReadU32Array(yyjson_obj_get(obj, "friend_id_list"), out_human_data->friend_id_list, DOMINO_MAX_FRIEND_COUNT_PER_HUMAN);
    serdeReadU32Array(yyjson_obj_get(obj, "colleague_id_list"), out_human_data->colleague_id_list, DOMINO_MAX_COLLEAGUE_COUNT_PER_HUMAN);

    out_human_data->death_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "death_time"));
    out_human_data->last_active_time = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "last_active_time"));

    out_human_data->last_active_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "last_active_country_id"));
    out_human_data->last_active_city_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "last_active_city_id"));
    out_human_data->last_active_building_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "last_active_building_id"));

    out_human_data->birth_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "birth_country_id"));
    out_human_data->birth_city_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "birth_city_id"));
    out_human_data->birth_building_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "birth_building_id"));
    out_human_data->death_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "death_country_id"));
    out_human_data->death_city_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "death_city_id"));
    out_human_data->death_building_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "death_building_id"));

    serdeReadU32Array(yyjson_obj_get(obj, "recent_contact_id_list"), out_human_data->recent_contact_id_list, DOMINO_MAX_RECENT_CONTACT_HUMAN_COUNT);
    serdeReadU32Array(yyjson_obj_get(obj, "recent_visited_address_id_list"), out_human_data->recent_visited_address_id_list,
                      DOMINO_MAX_RECENT_VISITED_ADDRESS_COUNT);

    out_human_data->last_active_region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "last_active_region_index"));
    out_human_data->birth_region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "birth_region_index"));
    out_human_data->death_region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "death_region_index"));
    out_human_data->recent_contact_list_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "recent_contact_list_count"));
    out_human_data->recent_contact_id_list_head_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "recent_contact_id_list_head_index"));
    out_human_data->recent_visited_address_list_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "recent_visited_address_list_count"));
    out_human_data->recent_visited_address_id_list_head_index =
        (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "recent_visited_address_id_list_head_index"));

    serdeReadStr(obj, "id_card", out_human_data->id_card, sizeof(out_human_data->id_card));
    if (serdeReadNameDescription(obj, &out_human_data->name_id, &out_human_data->description_id) != CODE_OK) {
        return -1;
    }
    out_human_data->generation = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "generation"));

    out_human_data->last_active_space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "last_active_space_id"));
    out_human_data->last_active_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "last_active_area_id"));
    out_human_data->birth_space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "birth_space_id"));
    out_human_data->birth_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "birth_area_id"));
    out_human_data->death_space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "death_space_id"));
    out_human_data->death_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "death_area_id"));

    out_human_data->skin_color = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "skin_color"));
    out_human_data->hair_color = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "hair_color"));
    out_human_data->eye_color = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "eye_color"));
    out_human_data->face_shape = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "face_shape"));
    out_human_data->body_shape = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "body_shape"));
    out_human_data->voice = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "voice"));
    yyjson_val* height_val = yyjson_obj_get(obj, "height");
    yyjson_val* weight_val = yyjson_obj_get(obj, "weight");
    if (!yyjson_is_uint(height_val) || yyjson_get_uint(height_val) > UINT8_MAX || !yyjson_is_uint(weight_val) ||
        yyjson_get_uint(weight_val) > UINT8_MAX) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human human_id=%u invalid height or weight",
                          out_human_data->human_id);
        return -1;
    }
    out_human_data->height = (uint8_t)yyjson_get_uint(height_val);
    out_human_data->weight = (uint8_t)yyjson_get_uint(weight_val);

    out_human_data->hat = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "hat"));
    out_human_data->hair = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "hair"));
    out_human_data->glasses = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "glasses"));
    out_human_data->earring = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "earring"));
    out_human_data->necklace = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "necklace"));
    out_human_data->coat = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "coat"));
    out_human_data->trouser = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "trouser"));
    out_human_data->shoe = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "shoe"));
    out_human_data->sock = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "sock"));
    out_human_data->glove = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "glove"));
    out_human_data->ring = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "ring"));
    out_human_data->bracelet = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "bracelet"));
    out_human_data->belt = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "belt"));
    out_human_data->bag = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "bag"));
    return 0;
}

/** @brief 序列化已完成引用预检的同索引 Human/Data；调用期间实体主表保持不变。 */
static yyjson_mut_val* serializeHuman(yyjson_mut_doc* doc, const DominoHuman* human, const DominoHumanData* human_data) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);

    yyjson_mut_obj_add_uint(doc, obj, "human_id", human->human_id);
    yyjson_mut_obj_add_uint(doc, obj, "status", human->status);

    domino_movable_object_id_t movable_object_id = human->movable_object_id;
    if (movable_object_id != 0) {
        DominoMovableObject* movable_object = dominoGetMovableObjectByID(movable_object_id);
        if (!movable_object) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human id=%u movable_object_id=%u not found", human->human_id,
                              movable_object_id);
            return nullptr;
        }
        if (movable_object != human->movable_object) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                              "human id=%u movable_object_id=%u pointer mismatch (dominoGetMovableObjectByID vs human->movable_object)",
                              human->human_id, movable_object_id);
            return nullptr;
        }
    }
    yyjson_mut_obj_add_uint(doc, obj, "movable_object_id", movable_object_id);
    if (movable_object_id == 0) {
        yyjson_mut_obj_add_sint(doc, obj, "position_x", human->position_info.x);
        yyjson_mut_obj_add_sint(doc, obj, "position_y", human->position_info.y);
        yyjson_mut_obj_add_sint(doc, obj, "position_z", human->position_info.z);
    }

    yyjson_mut_obj_add_uint(doc, obj, "space_id", human->space_id);
    yyjson_mut_obj_add_uint(doc, obj, "space_area_id", human->space_area_id);

    yyjson_mut_obj_add_uint(doc, obj, "region_index", human->region_index);

    if (human_data != human->data) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human human_id=%u data pointer mismatch", human->human_id);
        return nullptr;
    }
    yyjson_mut_val* data = serializeHumanData(doc, human_data);
    if (!data || !yyjson_mut_obj_add_val(doc, obj, "data", data)) {
        return nullptr;
    }

    return obj;
}

/** @brief 仅解析局部值，成功后由加载循环同步提交 Human 与 Data。 */
static int deserializeHuman(yyjson_val* obj, DominoHuman* out_human, DominoHumanData* out_human_data) {
    memset(out_human, 0, sizeof(DominoHuman));
    if (!yyjson_is_obj(obj)) {
        return -1;
    }
    HUMAN_READ_UINT(obj, out_human, human_id, UINT32_MAX);
    if (out_human->human_id == 0) {
        return -1;
    }
    out_human->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    HUMAN_READ_UINT(obj, out_human, movable_object_id, UINT32_MAX);
    if (out_human->movable_object_id == 0) {
        out_human->position_info.x = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_x"));
        out_human->position_info.y = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_y"));
        out_human->position_info.z = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_z"));
    }
    out_human->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_human->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));
    out_human->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));

    yyjson_val* data = yyjson_obj_get(obj, "data");
    if (!yyjson_is_obj(data) || deserializeHumanData(data, out_human_data) != 0 || out_human->human_id != out_human_data->human_id) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human human_id=%u requires matching data object",
                          out_human->human_id);
        return -1;
    }
    return 0;
}

#undef HUMAN_READ_UINT

/** @brief 两个数组保持相同索引，共用一张 human_id 映射；这里只预留容量，不增加实体数量。 */
static DOMINO_CODE reserveHumanLoad(uint32_t total_count) {
    size_t reserve = storageComputeReserveCapacity(total_count, HUMAN_VEC_RESERVE_CAPACITY_MIN);
    if (reserve < total_count || reserve > SIZE_MAX / sizeof(DominoHumanData) || reserve > SIZE_MAX / sizeof(DominoHuman)) {
        return ERR_OUT_OF_RANGE;
    }
    STORAGE_RESERVE_ID_MAP("human", dominoHumanIdMap, dominoHumanIdMap_resize, reserve);
    if (kv_max(domino_all_human_list) < reserve) {
        kv_resize(DominoHuman, domino_all_human_list, reserve);
    }
    if (kv_max(domino_all_human_data_list) < reserve) {
        kv_resize(DominoHumanData, domino_all_human_data_list, reserve);
    }
    return CODE_OK;
}

/** @brief 哈希插入为最后一个可返回错误的步骤；成功后同步提交同索引 Human/Data。 */
static DOMINO_CODE loadOneHuman(yyjson_val* entity_value) {
    DominoHuman human = {0};
    DominoHumanData data = {0};
    if (deserializeHuman(entity_value, &human, &data) != 0) {
        return ERR_INVALID_DATA;
    }
    int absent;
    khint_t map_slot = dominoHumanIdMap_put(dominoHumanIdMap, human.human_id, &absent);
    if (absent < 0) {
        return ERR_MEMORY_ALLOC;
    }
    if (absent == 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate human human_id=%u", human.human_id);
        return ERR_ALREADY_EXISTS;
    }
    size_t index = kv_size(domino_all_human_list);
    kv_push(DominoHuman, domino_all_human_list, human);
    kv_push(DominoHumanData, domino_all_human_data_list, data);
    kh_val(dominoHumanIdMap, map_slot) = (uint32_t)index;
    return CODE_OK;
}

DOMINO_CODE serdeSaveHuman(const char* save_path) {
    DOMINO_CODE result = serdeValidateHumanReferences();
    if (result != CODE_OK) {
        return result;
    }
    if (domino_all_human_list.n > UINT32_MAX) {
        return ERR_OUT_OF_RANGE;
    }
    uint32_t total = (uint32_t)domino_all_human_list.n;
    uint32_t shard_count = storageEntityShardCount(total, DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA);
    for (uint32_t shard = 0; shard < shard_count; shard++) {
        uint32_t start = shard * DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA;
        uint32_t count = total - start;
        if (count > DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA) {
            count = DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA;
        }
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_val* entities = yyjson_mut_arr(doc);
        result = ERR_MEMORY_ALLOC;
        if (!doc || !root || !entities || !yyjson_mut_obj_add_str(doc, root, "type", "human") ||
            !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard) || !yyjson_mut_obj_add_val(doc, root, "entities", entities)) {
            goto shard_done;
        }
        yyjson_mut_doc_set_root(doc, root);
        for (uint32_t index = start; index < start + count; index++) {
            yyjson_mut_val* object = serializeHuman(doc, &domino_all_human_list.a[index], &domino_all_human_data_list.a[index]);
            if (!object) {
                result = ERR_INVALID_DATA;
                goto shard_done;
            }
            if (!yyjson_mut_arr_append(entities, object)) {
                result = ERR_MEMORY_ALLOC;
                goto shard_done;
            }
        }
        result = storageWriteShardFile("human", shard, save_path, doc);
    shard_done:
        yyjson_mut_doc_free(doc);
        if (result != CODE_OK) {
            return result;
        }
    }
    return CODE_OK;
}

DOMINO_CODE serdeLoadHuman(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    if (domino_all_human_list.n != 0 || domino_all_human_data_list.n != 0 || kh_size(dominoHumanIdMap) != 0 ||
        shard_count != storageEntityShardCount(total_count, DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA)) {
        return ERR_INVALID_DATA;
    }
    DOMINO_CODE result = reserveHumanLoad(total_count);
    if (result != CODE_OK) {
        return result;
    }
    for (uint32_t shard = 0; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human", shard, save_path, &result);
        if (result != CODE_OK) {
            return result;
        }
        if (!doc) {
            return ERR_INVALID_JSON;
        }
        yyjson_val* entities = serdeValidateShardRoot(yyjson_doc_get_root(doc), "human", "human", shard);
        if (!entities) {
            yyjson_doc_free(doc);
            return ERR_INVALID_JSON;
        }
        size_t index;
        size_t count;
        yyjson_val* entity;
        yyjson_arr_foreach(entities, index, count, entity) {
            if (domino_all_human_list.n >= total_count) {
                result = ERR_INVALID_DATA;
                break;
            }
            result = loadOneHuman(entity);
            if (result != CODE_OK) {
                break;
            }
        }
        yyjson_doc_free(doc);
        if (result != CODE_OK) {
            return result;
        }
    }
    if (domino_all_human_list.n != total_count) {
        return ERR_INVALID_DATA;
    }
    DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_INFO, "loaded %zu humans", domino_all_human_list.n);
    return CODE_OK;
}

DOMINO_CODE serdeValidateHumanReferences(void) {
    if (!dominoHumanIdMap) {
        return ERR_NOT_INITIALIZED;
    }
    if (domino_all_human_list.n != domino_all_human_data_list.n || domino_all_human_list.n != kh_size(dominoHumanIdMap)) {
        return ERR_INVALID_DATA;
    }
    for (size_t index = 0; index < domino_all_human_list.n; index++) {
        const DominoHuman* human = &domino_all_human_list.a[index];
        const DominoHumanData* data = &domino_all_human_data_list.a[index];
        if (human->human_id == 0 || data->human_id != human->human_id || data->task_count > DOMINO_HUMAN_TASK_COUNT_MAX ||
            dominoGetHumanByID(human->human_id) != human) {
            return ERR_INVALID_DATA;
        }
        if (human->movable_object_id != 0 && !dominoGetMovableObjectByID(human->movable_object_id)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human human_id=%u movable_object_id=%u not found",
                              human->human_id, human->movable_object_id);
            return ERR_INVALID_DATA;
        }
        if (data->current_building_id != 0 && !dominoGetBuildingByID(data->current_building_id)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human human_id=%u current_building_id=%u not found",
                              human->human_id, data->current_building_id);
            return ERR_INVALID_DATA;
        }
    }
    return CODE_OK;
}

void serdeBindHumanReferences(void) {
    for (size_t index = 0; index < domino_all_human_list.n; index++) {
        DominoHuman* human = &domino_all_human_list.a[index];
        human->data = &domino_all_human_data_list.a[index];
        if (human->movable_object_id != 0) {
            human->movable_object = dominoGetMovableObjectByID(human->movable_object_id);
        }
    }
}
