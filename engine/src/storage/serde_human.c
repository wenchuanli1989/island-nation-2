#include <stdio.h>
#include <string.h>

#include "../host/entry.h"
#include "../human/entry.h"
#include "../logger/entry.h"
#include "io.h"
#include "serde.h"
#include "serde_registry.h"

/** @brief 持久化人类扩展数据；任务字段尚未接入当前存档格式。 */
static yyjson_mut_val* serializeHumanData(yyjson_mut_doc* doc, const DominoHumanData* human_data) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);

    yyjson_mut_obj_add_uint(doc, obj, "data_id", human_data->data_id);
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

static int deserializeHumanData(yyjson_val* obj, DominoHumanData* out_human_data) {
    memset(out_human_data, 0, sizeof(DominoHumanData));

    out_human_data->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
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

static yyjson_mut_val* serializeHuman(yyjson_mut_doc* doc, const DominoHuman* human) {
    yyjson_mut_val* obj = yyjson_mut_obj(doc);

    yyjson_mut_obj_add_uint(doc, obj, "id", human->id);
    yyjson_mut_obj_add_uint(doc, obj, "data_id", human->data_id);
    yyjson_mut_obj_add_uint(doc, obj, "status", human->status);

    domino_movable_object_id_t movable_object_id = human->movable_object_id;
    if (movable_object_id != 0) {
        DominoMovableObject* movable_object = dominoGetMovableObjectByID(movable_object_id);
        if (!movable_object) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human id=%u movable_object_id=%u not found", human->id,
                              movable_object_id);
            return nullptr;
        }
        if (movable_object != human->movable_object) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR,
                              "human id=%u movable_object_id=%u pointer mismatch (dominoGetMovableObjectByID vs human->movable_object)", human->id,
                              movable_object_id);
            return nullptr;
        }
    }
    yyjson_mut_obj_add_uint(doc, obj, "movable_object_id", movable_object_id);
    if (movable_object_id == 0) {
        yyjson_mut_obj_add_sint(doc, obj, "position_x", human->position_info.x);
        yyjson_mut_obj_add_sint(doc, obj, "position_y", human->position_info.y);
        yyjson_mut_obj_add_sint(doc, obj, "position_z", human->position_info.z);
    }

    yyjson_mut_obj_add_uint(doc, obj, "family_org_id", human->family_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "company_org_id", human->company_org_id);
    yyjson_mut_obj_add_uint(doc, obj, "country_id", human->country_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_country_id", human->current_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_city_id", human->current_city_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_building_id", human->current_building_id);
    yyjson_mut_obj_add_uint(doc, obj, "current_island_id", human->current_island_id);
    yyjson_mut_obj_add_sint(doc, obj, "wealth", human->wealth);
    yyjson_mut_obj_add_uint(doc, obj, "current_address", human->current_address);
    yyjson_mut_obj_add_uint(doc, obj, "family_address", human->family_address);
    yyjson_mut_obj_add_uint(doc, obj, "company_address", human->company_address);
    yyjson_mut_obj_add_uint(doc, obj, "sleep_address", human->sleep_address);
    yyjson_mut_obj_add_uint(doc, obj, "outer_host_country_id", human->outer_host_country_id);
    yyjson_mut_obj_add_uint(doc, obj, "space_id", human->space_id);
    yyjson_mut_obj_add_uint(doc, obj, "space_area_id", human->space_area_id);

    yyjson_mut_obj_add_uint(doc, obj, "behavior", human->behavior);
    yyjson_mut_obj_add_uint(doc, obj, "health", human->health);
    yyjson_mut_obj_add_uint(doc, obj, "energy", human->energy);
    yyjson_mut_obj_add_uint(doc, obj, "food", human->food);
    yyjson_mut_obj_add_uint(doc, obj, "water", human->water);
    yyjson_mut_obj_add_uint(doc, obj, "emotion", human->emotion);
    yyjson_mut_obj_add_uint(doc, obj, "nutrition", human->nutrition);
    yyjson_mut_obj_add_uint(doc, obj, "hygiene", human->hygiene);
    yyjson_mut_obj_add_uint(doc, obj, "sleep", human->sleep);
    yyjson_mut_obj_add_uint(doc, obj, "exercise", human->exercise);
    yyjson_mut_obj_add_uint(doc, obj, "recreation", human->recreation);
    yyjson_mut_obj_add_uint(doc, obj, "social", human->social);
    yyjson_mut_obj_add_uint(doc, obj, "family_love", human->family_love);
    yyjson_mut_obj_add_uint(doc, obj, "couple_love", human->couple_love);
    yyjson_mut_obj_add_uint(doc, obj, "friendship", human->friendship);

    yyjson_mut_obj_add_uint(doc, obj, "job", human->job);
    yyjson_mut_obj_add_uint(doc, obj, "influence", human->influence);
    yyjson_mut_obj_add_uint(doc, obj, "fame", human->fame);
    yyjson_mut_obj_add_uint(doc, obj, "body_hp_value", human->body_hp_value);
    yyjson_mut_obj_add_uint(doc, obj, "gender", human->gender);
    yyjson_mut_obj_add_uint(doc, obj, "age", human->age);
    yyjson_mut_obj_add_uint(doc, obj, "height", human->height);
    yyjson_mut_obj_add_uint(doc, obj, "weight", human->weight);
    yyjson_mut_obj_add_uint(doc, obj, "intelligence", human->intelligence);
    yyjson_mut_obj_add_uint(doc, obj, "talent", human->talent);
    yyjson_mut_obj_add_uint(doc, obj, "character", human->character);
    yyjson_mut_obj_add_uint(doc, obj, "hobby", human->hobby);
    yyjson_mut_obj_add_uint(doc, obj, "belief", human->belief);
    yyjson_mut_obj_add_uint(doc, obj, "world_view", human->world_view);
    yyjson_mut_obj_add_uint(doc, obj, "life_view", human->life_view);
    yyjson_mut_obj_add_uint(doc, obj, "value_view", human->value_view);
    yyjson_mut_obj_add_uint(doc, obj, "family_info", human->family_info);
    yyjson_mut_obj_add_uint(doc, obj, "org_info", human->org_info);
    yyjson_mut_obj_add_uint(doc, obj, "region_index", human->region_index);
    yyjson_mut_obj_add_uint(doc, obj, "hold_asset_list_count", human->hold_asset_list_count);
    yyjson_mut_obj_add_uint(doc, obj, "own_asset_list_count", human->own_asset_list_count);

    if (!human->data && human->data_id != 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human id=%u data_id=%u but data is null", human->id,
                          human->data_id);
        return nullptr;
    }
    if (human->data && human->data_id == 0) {
        DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human id=%u data is not null but data_id is 0", human->id);
        return nullptr;
    }
    if (human->data) {
        /* data_id 和 data 指针必须同时有效，避免保存出悬空引用或错误嵌套数据。 */
        DominoHumanData* data_from_id = dominoGetHumanDataByID(human->data_id);
        if (!data_from_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human id=%u data_id=%u not found", human->id,
                              human->data_id);
            return nullptr;
        }
        if (data_from_id != human->data) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human id=%u data_id=%u data pointer mismatch", human->id,
                              human->data_id);
            return nullptr;
        }
        yyjson_mut_obj_add_val(doc, obj, "data", serializeHumanData(doc, human->data));
    } else {
        yyjson_mut_obj_add_null(doc, obj, "data");
    }

    return obj;
}

static int deserializeHuman(yyjson_val* obj, DominoHuman* out_human) {
    memset(out_human, 0, sizeof(DominoHuman));

    out_human->id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "id"));
    out_human->data_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "data_id"));
    out_human->status = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "status"));

    domino_movable_object_id_t movable_object_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "movable_object_id"));
    out_human->movable_object_id = movable_object_id;
    if (movable_object_id == 0) {
        out_human->position_info.x = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_x"));
        out_human->position_info.y = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_y"));
        out_human->position_info.z = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "position_z"));
    } else {
        out_human->movable_object = nullptr;
    }

    out_human->family_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "family_org_id"));
    out_human->company_org_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "company_org_id"));
    out_human->country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "country_id"));
    out_human->current_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_country_id"));
    out_human->current_city_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_city_id"));
    out_human->current_building_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_building_id"));
    out_human->current_island_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_island_id"));
    out_human->wealth = (int32_t)yyjson_get_sint(yyjson_obj_get(obj, "wealth"));
    out_human->current_address = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "current_address"));
    out_human->family_address = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "family_address"));
    out_human->company_address = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "company_address"));
    out_human->sleep_address = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "sleep_address"));
    out_human->outer_host_country_id = (uint32_t)yyjson_get_uint(yyjson_obj_get(obj, "outer_host_country_id"));
    out_human->space_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_id"));
    out_human->space_area_id = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "space_area_id"));

    out_human->behavior = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "behavior"));
    out_human->health = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "health"));
    out_human->energy = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "energy"));
    out_human->food = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "food"));
    out_human->water = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "water"));
    out_human->emotion = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "emotion"));
    out_human->nutrition = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "nutrition"));
    out_human->hygiene = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "hygiene"));
    out_human->sleep = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "sleep"));
    out_human->exercise = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "exercise"));
    out_human->recreation = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "recreation"));
    out_human->social = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "social"));
    out_human->family_love = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "family_love"));
    out_human->couple_love = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "couple_love"));
    out_human->friendship = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "friendship"));

    out_human->job = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "job"));
    out_human->influence = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "influence"));
    out_human->fame = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "fame"));
    out_human->body_hp_value = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "body_hp_value"));
    out_human->gender = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "gender"));
    out_human->age = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "age"));
    out_human->height = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "height"));
    out_human->weight = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "weight"));
    out_human->intelligence = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "intelligence"));
    out_human->talent = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "talent"));
    out_human->character = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "character"));
    out_human->hobby = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "hobby"));
    out_human->belief = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "belief"));
    out_human->world_view = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "world_view"));
    out_human->life_view = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "life_view"));
    out_human->value_view = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "value_view"));
    out_human->family_info = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "family_info"));
    out_human->org_info = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "org_info"));
    out_human->region_index = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "region_index"));
    out_human->hold_asset_list_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "hold_asset_list_count"));
    out_human->own_asset_list_count = (uint8_t)yyjson_get_uint(yyjson_obj_get(obj, "own_asset_list_count"));

    yyjson_val* data_val = yyjson_obj_get(obj, "data");
    if (data_val && !yyjson_is_null(data_val)) {
        /* 嵌套 data 必须和外层 data_id 一致，否则拒绝加载以保护 id->data 映射。 */
        DominoHumanData loaded_human_data = {0};
        if (deserializeHumanData(data_val, &loaded_human_data) != 0) {
            return -1;
        }
        if (out_human->data_id != loaded_human_data.data_id) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human id=%u entity data_id=%u != nested data data_id=%u",
                              out_human->id, out_human->data_id, loaded_human_data.data_id);
            return -1;
        }
        size_t old_data_count = kv_size(domino_all_human_data_list);
        kv_push(DominoHumanData, domino_all_human_data_list, loaded_human_data);
        uint32_t data_arr_index = (uint32_t)old_data_count;

        int absent;
        khint_t data_id_map_slot = dominoHumanDataIdMap_put(dominoHumanDataIdMap, loaded_human_data.data_id, &absent);
        if (absent < 0) {
            domino_all_human_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "failed to map human_data data_id=%u",
                              loaded_human_data.data_id);
            return -1;
        }
        if (absent == 0) {
            domino_all_human_data_list.n = old_data_count;
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "duplicate human_data data_id=%u", loaded_human_data.data_id);
            return -1;
        }
        kh_val(dominoHumanDataIdMap, data_id_map_slot) = data_arr_index;
        out_human->data = &kv_A(domino_all_human_data_list, data_arr_index);
    }
    return 0;
}

STORAGE_DEFINE_SHARDED_WITH_DATA(Human, "human", DominoHuman, domino_all_human_list, dominoHumanIdMap, dominoHumanIdMap_put, dominoHumanIdMap_resize,
                                 serializeHuman, deserializeHuman, HUMAN_VEC_RESERVE_CAPACITY_MIN, DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA,
                                 DominoHumanData, domino_all_human_data_list, dominoHumanDataIdMap, dominoHumanDataIdMap_resize)

DOMINO_CODE serdeValidateHumanReferences(void) {
    for (size_t human_index = 0; human_index < kv_size(domino_all_human_list); human_index++) {
        const DominoHuman* human = &kv_A(domino_all_human_list, human_index);
        if (human->movable_object_id == 0) {
            continue;
        }
        if (!dominoGetMovableObjectByID(human->movable_object_id)) {
            DOMINO_ENGINE_LOG(DOMINO_ENGINE_LOG_MODULE_STORAGE, DOMINO_LOG_LEVEL_ERROR, "human id=%u movable_object_id=%u not found", human->id,
                              human->movable_object_id);
            return ERR_INVALID_DATA;
        }
    }
    return CODE_OK;
}

void serdeBindHumanReferences(void) {
    for (size_t human_index = 0; human_index < kv_size(domino_all_human_list); human_index++) {
        DominoHuman* human = &kv_A(domino_all_human_list, human_index);
        if (human->movable_object_id != 0) {
            human->movable_object = dominoGetMovableObjectByID(human->movable_object_id);
        }
    }
}
