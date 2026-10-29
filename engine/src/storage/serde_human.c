#include <string.h>

#include "../host/entry.h"
#include "../human/entry.h"
#include "../social/entry.h"
#include "domino_shared_common.h"
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
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }
    /* 只保存有效 ID，保持提交顺序；数组长度即 task_count。 */
    yyjson_mut_val* task_ids = yyjson_mut_obj_add_arr(doc, obj, "task_id_list");
    if (!task_ids) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }
    for (size_t index = 0; index < human_data->task_count; index++) {
        if (!yyjson_mut_arr_add_uint(doc, task_ids, human_data->task_id_list[index])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
        }
    }
    yyjson_mut_val* slots = yyjson_mut_obj_add_arr(doc, obj, "active_task_id_list");
    if (!slots) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }
    for (size_t index = 0; index < DOMINO_HUMAN_TASK_CHANNEL_COUNT; index++) {
        if (!yyjson_mut_arr_add_uint(doc, slots, human_data->active_task_id_list[index])) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
        }
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "family_org_id", human_data->family_org_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "company_org_id", human_data->company_org_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "country_id", human_data->country_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "current_country_id", human_data->current_country_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "current_city_id", human_data->current_city_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "current_building_id", human_data->current_building_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "current_island_id", human_data->current_island_id) ||
        !yyjson_mut_obj_add_sint(doc, obj, "wealth", human_data->wealth) ||
        !yyjson_mut_obj_add_uint(doc, obj, "current_address", human_data->current_address) ||
        !yyjson_mut_obj_add_uint(doc, obj, "family_address", human_data->family_address) ||
        !yyjson_mut_obj_add_uint(doc, obj, "company_address", human_data->company_address) ||
        !yyjson_mut_obj_add_uint(doc, obj, "sleep_address", human_data->sleep_address) ||
        !yyjson_mut_obj_add_uint(doc, obj, "outer_host_country_id", human_data->outer_host_country_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "health", human_data->health) || !yyjson_mut_obj_add_uint(doc, obj, "energy", human_data->energy) ||
        !yyjson_mut_obj_add_uint(doc, obj, "food", human_data->food) || !yyjson_mut_obj_add_uint(doc, obj, "water", human_data->water) ||
        !yyjson_mut_obj_add_uint(doc, obj, "emotion", human_data->emotion) ||
        !yyjson_mut_obj_add_uint(doc, obj, "nutrition", human_data->nutrition) ||
        !yyjson_mut_obj_add_uint(doc, obj, "hygiene", human_data->hygiene) || !yyjson_mut_obj_add_uint(doc, obj, "sleep", human_data->sleep) ||
        !yyjson_mut_obj_add_uint(doc, obj, "exercise", human_data->exercise) ||
        !yyjson_mut_obj_add_uint(doc, obj, "recreation", human_data->recreation) ||
        !yyjson_mut_obj_add_uint(doc, obj, "social", human_data->social) ||
        !yyjson_mut_obj_add_uint(doc, obj, "family_love", human_data->family_love) ||
        !yyjson_mut_obj_add_uint(doc, obj, "couple_love", human_data->couple_love) ||
        !yyjson_mut_obj_add_uint(doc, obj, "friendship", human_data->friendship) || !yyjson_mut_obj_add_uint(doc, obj, "job", human_data->job) ||
        !yyjson_mut_obj_add_uint(doc, obj, "influence", human_data->influence) || !yyjson_mut_obj_add_uint(doc, obj, "fame", human_data->fame) ||
        !yyjson_mut_obj_add_uint(doc, obj, "body_hp_value", human_data->body_hp_value) ||
        !yyjson_mut_obj_add_uint(doc, obj, "gender", human_data->gender) || !yyjson_mut_obj_add_uint(doc, obj, "age", human_data->age) ||
        !yyjson_mut_obj_add_uint(doc, obj, "intelligence", human_data->intelligence) ||
        !yyjson_mut_obj_add_uint(doc, obj, "talent", human_data->talent) || !yyjson_mut_obj_add_uint(doc, obj, "character", human_data->character) ||
        !yyjson_mut_obj_add_uint(doc, obj, "hobby", human_data->hobby) || !yyjson_mut_obj_add_uint(doc, obj, "belief", human_data->belief) ||
        !yyjson_mut_obj_add_uint(doc, obj, "world_view", human_data->world_view) ||
        !yyjson_mut_obj_add_uint(doc, obj, "life_view", human_data->life_view) ||
        !yyjson_mut_obj_add_uint(doc, obj, "value_view", human_data->value_view) ||
        !yyjson_mut_obj_add_uint(doc, obj, "family_info", human_data->family_info) ||
        !yyjson_mut_obj_add_uint(doc, obj, "org_info", human_data->org_info) ||
        !yyjson_mut_obj_add_uint(doc, obj, "hold_asset_list_count", human_data->hold_asset_list_count) ||
        !yyjson_mut_obj_add_uint(doc, obj, "own_asset_list_count", human_data->own_asset_list_count)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_val(doc, obj, "own_asset_id_list",
                                serdeWriteU32Array(doc, human_data->own_asset_id_list, DOMINO_MAX_ASSET_COUNT_PER_HUMAN)) ||
        !yyjson_mut_obj_add_val(doc, obj, "hold_asset_id_list",
                                serdeWriteU32Array(doc, human_data->hold_asset_id_list, DOMINO_MAX_ASSET_COUNT_HOLD_PER_HUMAN))) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "partner_id", human_data->partner_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "father_id", human_data->father_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "mother_id", human_data->mother_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_val(doc, obj, "child_id_list", serdeWriteU32Array(doc, human_data->child_id_list, DOMINO_MAX_CHILD_COUNT_PER_FAMILY)) ||
        !yyjson_mut_obj_add_val(doc, obj, "sibling_id_list",
                                serdeWriteU32Array(doc, human_data->sibling_id_list, DOMINO_MAX_CHILD_COUNT_PER_FAMILY - 1)) ||
        !yyjson_mut_obj_add_val(doc, obj, "org_id_list", serdeWriteU32Array(doc, human_data->org_id_list, DOMINO_MAX_ORG_COUNT_PER_HUMAN)) ||
        !yyjson_mut_obj_add_val(doc, obj, "org_boss_id_list",
                                serdeWriteU32Array(doc, human_data->org_boss_id_list, DOMINO_MAX_ORG_BOSS_COUNT_PER_HUMAN)) ||
        !yyjson_mut_obj_add_val(doc, obj, "friend_id_list", serdeWriteU32Array(doc, human_data->friend_id_list, DOMINO_MAX_FRIEND_COUNT_PER_HUMAN)) ||
        !yyjson_mut_obj_add_val(doc, obj, "colleague_id_list",
                                serdeWriteU32Array(doc, human_data->colleague_id_list, DOMINO_MAX_COLLEAGUE_COUNT_PER_HUMAN))) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "death_time", human_data->death_time) ||
        !yyjson_mut_obj_add_uint(doc, obj, "last_active_time", human_data->last_active_time)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "last_active_country_id", human_data->last_active_country_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "last_active_city_id", human_data->last_active_city_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "last_active_building_id", human_data->last_active_building_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "birth_country_id", human_data->birth_country_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "birth_city_id", human_data->birth_city_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "birth_building_id", human_data->birth_building_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "death_country_id", human_data->death_country_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "death_city_id", human_data->death_city_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "death_building_id", human_data->death_building_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_val(doc, obj, "recent_contact_id_list",
                                serdeWriteU32Array(doc, human_data->recent_contact_id_list, DOMINO_MAX_RECENT_CONTACT_HUMAN_COUNT)) ||
        !yyjson_mut_obj_add_val(doc, obj, "recent_visited_address_id_list",
                                serdeWriteU32Array(doc, human_data->recent_visited_address_id_list, DOMINO_MAX_RECENT_VISITED_ADDRESS_COUNT))) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "last_active_region_index", human_data->last_active_region_index) ||
        !yyjson_mut_obj_add_uint(doc, obj, "birth_region_index", human_data->birth_region_index) ||
        !yyjson_mut_obj_add_uint(doc, obj, "death_region_index", human_data->death_region_index) ||
        !yyjson_mut_obj_add_uint(doc, obj, "recent_contact_list_count", human_data->recent_contact_list_count) ||
        !yyjson_mut_obj_add_uint(doc, obj, "recent_contact_id_list_head_index", human_data->recent_contact_id_list_head_index) ||
        !yyjson_mut_obj_add_uint(doc, obj, "recent_visited_address_list_count", human_data->recent_visited_address_list_count) ||
        !yyjson_mut_obj_add_uint(doc, obj, "recent_visited_address_id_list_head_index", human_data->recent_visited_address_id_list_head_index)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    serdeWriteNameDescription(doc, obj, human_data->name_id, human_data->description_id);
    if (!yyjson_mut_obj_add_strcpy(doc, obj, "id_card", human_data->id_card) ||
        !yyjson_mut_obj_add_uint(doc, obj, "generation", human_data->generation)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "last_active_space_id", human_data->last_active_space_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "last_active_area_id", human_data->last_active_area_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "birth_space_id", human_data->birth_space_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "birth_area_id", human_data->birth_area_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "death_space_id", human_data->death_space_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "death_area_id", human_data->death_area_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "skin_color", human_data->skin_color) ||
        !yyjson_mut_obj_add_uint(doc, obj, "hair_color", human_data->hair_color) ||
        !yyjson_mut_obj_add_uint(doc, obj, "eye_color", human_data->eye_color) ||
        !yyjson_mut_obj_add_uint(doc, obj, "face_shape", human_data->face_shape) ||
        !yyjson_mut_obj_add_uint(doc, obj, "body_shape", human_data->body_shape) || !yyjson_mut_obj_add_uint(doc, obj, "voice", human_data->voice) ||
        !yyjson_mut_obj_add_uint(doc, obj, "height", human_data->height) || !yyjson_mut_obj_add_uint(doc, obj, "weight", human_data->weight)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "hat", human_data->hat) || !yyjson_mut_obj_add_uint(doc, obj, "hair", human_data->hair) ||
        !yyjson_mut_obj_add_uint(doc, obj, "glasses", human_data->glasses) || !yyjson_mut_obj_add_uint(doc, obj, "earring", human_data->earring) ||
        !yyjson_mut_obj_add_uint(doc, obj, "necklace", human_data->necklace) || !yyjson_mut_obj_add_uint(doc, obj, "coat", human_data->coat) ||
        !yyjson_mut_obj_add_uint(doc, obj, "trouser", human_data->trouser) || !yyjson_mut_obj_add_uint(doc, obj, "shoe", human_data->shoe) ||
        !yyjson_mut_obj_add_uint(doc, obj, "sock", human_data->sock) || !yyjson_mut_obj_add_uint(doc, obj, "glove", human_data->glove) ||
        !yyjson_mut_obj_add_uint(doc, obj, "ring", human_data->ring) || !yyjson_mut_obj_add_uint(doc, obj, "bracelet", human_data->bracelet) ||
        !yyjson_mut_obj_add_uint(doc, obj, "belt", human_data->belt) || !yyjson_mut_obj_add_uint(doc, obj, "bag", human_data->bag)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    return obj;
}

static void deserializeHumanData(yyjson_val* obj, DominoHumanData* out_human_data) {
    out_human_data->human_id = (typeof(out_human_data->human_id))yyjson_get_uint(yyjson_obj_get(obj, "human_id"));
    out_human_data->random_state = (typeof(out_human_data->random_state))yyjson_get_uint(yyjson_obj_get(obj, "random_state"));
    out_human_data->schedule_revision = (typeof(out_human_data->schedule_revision))yyjson_get_uint(yyjson_obj_get(obj, "schedule_revision"));
    out_human_data->needs_updated_at_ms = (typeof(out_human_data->needs_updated_at_ms))yyjson_get_uint(yyjson_obj_get(obj, "needs_updated_at_ms"));
    yyjson_val* task_ids = yyjson_obj_get(obj, "task_id_list");
    if (yyjson_arr_size(task_ids) > DOMINO_HUMAN_TASK_COUNT_MAX) {
        DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human task count exceeds capacity: human_id=%u count=%zu", out_human_data->human_id,
                               yyjson_arr_size(task_ids));
    }
    out_human_data->task_count = (uint8_t)yyjson_arr_size(task_ids);
    for (size_t index = 0; index < out_human_data->task_count; index++) {
        yyjson_val* task_id = yyjson_arr_get(task_ids, index);
        out_human_data->task_id_list[index] = (domino_task_id_t)yyjson_get_uint(task_id);
    }
    yyjson_val* slots = yyjson_obj_get(obj, "active_task_id_list");
    for (size_t index = 0; index < DOMINO_HUMAN_TASK_CHANNEL_COUNT; index++) {
        yyjson_val* task_id = yyjson_arr_get(slots, index);
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
    out_human_data->energy = (typeof(out_human_data->energy))yyjson_get_uint(yyjson_obj_get(obj, "energy"));
    out_human_data->food = (typeof(out_human_data->food))yyjson_get_uint(yyjson_obj_get(obj, "food"));
    out_human_data->water = (typeof(out_human_data->water))yyjson_get_uint(yyjson_obj_get(obj, "water"));
    out_human_data->emotion = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "emotion"));
    out_human_data->nutrition = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "nutrition"));
    out_human_data->hygiene = (typeof(out_human_data->hygiene))yyjson_get_uint(yyjson_obj_get(obj, "hygiene"));
    out_human_data->sleep = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "sleep"));
    out_human_data->exercise = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "exercise"));
    out_human_data->recreation = (typeof(out_human_data->recreation))yyjson_get_uint(yyjson_obj_get(obj, "recreation"));
    out_human_data->social = (typeof(out_human_data->social))yyjson_get_uint(yyjson_obj_get(obj, "social"));
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
    out_human_data->current_building_id = (typeof(out_human_data->current_building_id))yyjson_get_uint(yyjson_obj_get(obj, "current_building_id"));

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
    serdeReadNameDescription(obj, &out_human_data->name_id, &out_human_data->description_id);
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
    out_human_data->height = (typeof(out_human_data->height))yyjson_get_uint(yyjson_obj_get(obj, "height"));
    out_human_data->weight = (typeof(out_human_data->weight))yyjson_get_uint(yyjson_obj_get(obj, "weight"));

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
}

/** @brief 序列化已完成引用及绑定指针预检的同索引 Human/Data；调用期间实体主表保持不变。 */
static yyjson_mut_val* serializeHuman(yyjson_mut_doc* doc, const DominoHuman* human, const DominoHumanData* human_data) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);

    if (!obj || !yyjson_mut_obj_add_uint(doc, obj, "human_id", human->human_id) || !yyjson_mut_obj_add_uint(doc, obj, "status", human->status)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    domino_movable_object_id_t movable_object_id = human->movable_object_id;
    if (!yyjson_mut_obj_add_uint(doc, obj, "movable_object_id", movable_object_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }
    if (movable_object_id == 0) {
        if (!yyjson_mut_obj_add_sint(doc, obj, "position_x", human->position_info.x) ||
            !yyjson_mut_obj_add_sint(doc, obj, "position_y", human->position_info.y) ||
            !yyjson_mut_obj_add_sint(doc, obj, "position_z", human->position_info.z)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
        }
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "space_id", human->space_id) ||
        !yyjson_mut_obj_add_uint(doc, obj, "space_area_id", human->space_area_id)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    if (!yyjson_mut_obj_add_uint(doc, obj, "region_index", human->region_index)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    yyjson_mut_val* data = serializeHumanData(doc, human_data);
    if (!yyjson_mut_obj_add_val(doc, obj, "data", data)) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON value");
    }

    return obj;
}

/** @brief 仅解析局部值，成功后由加载循环同步提交 Human 与 Data。 */
static void deserializeHuman(yyjson_val* obj, DominoHuman* out_human, DominoHumanData* out_human_data) {
    out_human->human_id = (typeof(out_human->human_id))yyjson_get_uint(yyjson_obj_get(obj, "human_id"));
    out_human->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));
    out_human->movable_object_id = (typeof(out_human->movable_object_id))yyjson_get_uint(yyjson_obj_get(obj, "movable_object_id"));
    if (out_human->movable_object_id == 0) {
        out_human->position_info.x = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_x"));
        out_human->position_info.y = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_y"));
        out_human->position_info.z = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_z"));
    }
    out_human->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_human->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));
    out_human->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));

    yyjson_val* data = yyjson_obj_get(obj, "data");
    deserializeHumanData(data, out_human_data);
}

/** @brief 哈希插入成功后按需追加同索引 Human/Data；失败直接终止。 */
static void loadOneHuman(yyjson_val* entity_value) {
    DominoHuman human = {0};
    DominoHumanData data = {0};
    deserializeHuman(entity_value, &human, &data);
    int absent;
    khint_t map_slot = dominoHumanIdMap_put(dominoHumanIdMap, human.human_id, &absent);
    if (absent < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human ID map entry: human_id=%u", human.human_id);
    }
    size_t index = kv_size(domino_all_human_list);
    kv_push(DominoHuman, domino_all_human_list, human);
    kv_push(DominoHumanData, domino_all_human_data_list, data);
    kh_val(dominoHumanIdMap, map_slot) = (uint32_t)index;
}

/** @brief 业务线程已停止；保存前统一检查人物/data 身份、索引、容量、位置引用及绑定指针。 */
void serdeValidateHumanForSave(void) {
    if (domino_all_human_list.n != domino_all_human_data_list.n || domino_all_human_list.n != kh_size(dominoHumanIdMap)) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Human table counts disagree: humans=%zu data=%zu mapped=%u", domino_all_human_list.n,
                               domino_all_human_data_list.n, kh_size(dominoHumanIdMap));
    }
    for (size_t index = 0; index < domino_all_human_list.n; index++) {
        const DominoHuman* human = &domino_all_human_list.a[index];
        const DominoHumanData* data = &domino_all_human_data_list.a[index];
        if (human->human_id == 0 || data->human_id != human->human_id || dominoGetHumanByID(human->human_id) != human || human->data != data) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Invalid human data or binding: index=%zu human_id=%u", index, human->human_id);
        }
        if (data->task_count > DOMINO_HUMAN_TASK_COUNT_MAX) {
            DOMINO_EXIT_WITH_ERROR(ERR_CAPACITY_EXCEEDED, "Human task count exceeds capacity: human_id=%u count=%u", human->human_id,
                                   data->task_count);
        }
        if (human->movable_object_id != 0) {
            const DominoMovableObject* movable_object = dominoGetMovableObjectByID(human->movable_object_id);
            if (!movable_object || human->movable_object != movable_object) {
                DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Invalid human movable-object reference: human_id=%u movable_object_id=%u", human->human_id,
                                       human->movable_object_id);
            }
        }
        if (data->current_building_id != 0 && !dominoGetBuildingByID(data->current_building_id)) {
            DOMINO_EXIT_WITH_ERROR(ERR_INVALID_DATA, "Invalid human building reference: human_id=%u building_id=%u", human->human_id,
                                   data->current_building_id);
        }
    }
}

void serdeSaveHuman(const char* save_path) {
    /* 预检已确认数量等于使用 32 位计数的 ID map。 */
    uint32_t total = (uint32_t)domino_all_human_list.n;
    uint32_t shard_count = storageEntityShardCount(total, DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA);
    for (uint32_t shard = 0; shard < shard_count; shard++) {
        uint32_t start = shard * DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA;
        uint32_t count = total - start;
        if (count > DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA) {
            count = DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA;
        }
        yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
        if (!doc) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON document: shard=%u", shard);
        }
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        if (!root) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human JSON root: shard=%u", shard);
        }
        yyjson_mut_val* entities = yyjson_mut_arr(doc);
        if (!entities || !yyjson_mut_obj_add_str(doc, root, "type", "human") || !yyjson_mut_obj_add_uint(doc, root, "shard_id", shard) ||
            !yyjson_mut_obj_add_val(doc, root, "entities", entities)) {
            DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot allocate human shard fields: shard=%u", shard);
        }
        yyjson_mut_doc_set_root(doc, root);
        for (uint32_t index = start; index < start + count; index++) {
            yyjson_mut_val* object = serializeHuman(doc, &domino_all_human_list.a[index], &domino_all_human_data_list.a[index]);
            if (!yyjson_mut_arr_append(entities, object)) {
                DOMINO_EXIT_WITH_ERROR(ERR_MEMORY_ALLOC, "Cannot append human JSON record: shard=%u", shard);
            }
        }
        storageWriteShardFile("human", shard, save_path, doc);
        yyjson_mut_doc_free(doc);
    }
}

void serdeLoadHuman(const char* save_path, uint32_t total_count, uint32_t shard_count) {
    storageValidateLoadCount(total_count, sizeof(DominoHuman));
    storageValidateLoadCount(total_count, sizeof(DominoHumanData));
    for (uint32_t shard = 0; shard < shard_count; shard++) {
        yyjson_doc* doc = storageReadShardFile("human", shard, save_path);
        yyjson_val* entities = yyjson_obj_get(yyjson_doc_get_root(doc), "entities");
        size_t index;
        size_t count;
        yyjson_val* entity;
        yyjson_arr_foreach(entities, index, count, entity) {
            loadOneHuman(entity);
        }
        yyjson_doc_free(doc);
    }
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
