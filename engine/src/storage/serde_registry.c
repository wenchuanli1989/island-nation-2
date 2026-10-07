#include "serde_registry.h"

#include "../host/entry.h"
#include "../human/entry.h"
#include "../nav/storage_view.h"
#include "../social/entry.h"
#include "serde.h"

static size_t countHuman(void) {
    return kv_size(domino_all_human_list);
}

static size_t countRegion(void) {
    return g_domino_region_count;
}

static size_t countOrg(void) {
    return kv_size(domino_all_org_list);
}

static size_t countBuilding(void) {
    return kv_size(domino_all_building_list);
}

static size_t countCity(void) {
    return kv_size(domino_all_city_list);
}

static size_t countCountry(void) {
    return kv_size(domino_all_country_list);
}

static size_t countAsset(void) {
    return kv_size(domino_all_asset_list);
}

static size_t countIsland(void) {
    return kv_size(domino_all_island_list);
}

static size_t countMovableObject(void) {
    return kv_size(domino_all_movable_object_list);
}

static size_t countForkRoad(void) {
    return kv_size(domino_all_fork_road_list);
}

static size_t countRoad(void) {
    return kv_size(domino_all_road_list);
}

static size_t countRoadLine(void) {
    return kv_size(domino_all_road_line_list);
}

/* regions 先恢复地域；跨模块引用统一在全部实体加载完成后校验并绑定。 */
const StorageModuleType g_storage_module_types[] = {
    {
        .type_name = "regions",
        .max_per_shard = 1,
        .required = true,
        .count_fn = countRegion,
        .shard_count_fn = storageSingleShardCount,
        .save_fn = serdeSaveRegion,
        .load_fn = serdeLoadRegion,
    },
    {
        .type_name = "asset",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA,
        .required = false,
        .count_fn = countAsset,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveAsset,
        .load_fn = serdeLoadAsset,
        .validate_references_fn = serdeValidateAssetReferences,
        .bind_references_fn = serdeBindAssetReferences,
    },
    {
        .type_name = "building",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA,
        .required = false,
        .count_fn = countBuilding,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveBuilding,
        .load_fn = serdeLoadBuilding,
    },
    {
        .type_name = "city",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA,
        .required = false,
        .count_fn = countCity,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveCity,
        .load_fn = serdeLoadCity,
    },
    {
        .type_name = "country",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA,
        .required = false,
        .count_fn = countCountry,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveCountry,
        .load_fn = serdeLoadCountry,
    },
    {
        .type_name = "fork_road",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA,
        .required = false,
        .count_fn = countForkRoad,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveForkRoad,
        .load_fn = serdeLoadForkRoad,
        .write_meta_extra_fn = serdeNavWriteModuleMetaExtraFields,
        .read_meta_extra_fn = serdeNavReadModuleMetaExtraFields,
    },
    {
        .type_name = "human",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA,
        .required = false,
        .count_fn = countHuman,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveHuman,
        .load_fn = serdeLoadHuman,
        .validate_references_fn = serdeValidateHumanReferences,
        .bind_references_fn = serdeBindHumanReferences,
    },
    {
        .type_name = "island",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA,
        .required = false,
        .count_fn = countIsland,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveIsland,
        .load_fn = serdeLoadIsland,
    },
    {
        .type_name = "movable_object",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA,
        .required = false,
        .count_fn = countMovableObject,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveMovableObject,
        .load_fn = serdeLoadMovableObject,
    },
    {
        .type_name = "org",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_WITH_DATA,
        .required = false,
        .count_fn = countOrg,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveOrg,
        .load_fn = serdeLoadOrg,
    },
    {
        .type_name = "road",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA,
        .required = false,
        .count_fn = countRoad,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveRoad,
        .load_fn = serdeLoadRoad,
        .write_meta_extra_fn = serdeNavWriteModuleMetaExtraFields,
        .read_meta_extra_fn = serdeNavReadModuleMetaExtraFields,
    },
    {
        .type_name = "road_line",
        .max_per_shard = DOMINO_STORAGE_SHARD_ENTITIES_NO_DATA,
        .required = false,
        .count_fn = countRoadLine,
        .shard_count_fn = storageEntityShardCount,
        .save_fn = serdeSaveRoadLine,
        .load_fn = serdeLoadRoadLine,
    },
};

const size_t g_storage_module_type_count = sizeof(g_storage_module_types) / sizeof(g_storage_module_types[0]);
