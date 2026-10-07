#define _POSIX_C_SOURCE 200809L

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "domino_engine.h"
#include "domino_shared_common.h"
#include "domino_shared_nav.h"
#include "entry.h"
#include "host/entry.h"
#include "nav/entry.h"
#include "nav/path.h"
#include "nav/state.h"
#include "nav/storage_view.h"

#define TEST_PATH_MAX 1024U
#define TEST_META_BUFFER_SIZE (2U * 1024U * 1024U)

static bool writeTextFile(const char* path, const char* text) {
    FILE* file_ptr = fopen(path, "wb");
    if (!file_ptr) {
        return false;
    }
    size_t text_len = strlen(text);
    bool success = fwrite(text, 1U, text_len, file_ptr) == text_len;
    success = fclose(file_ptr) == 0 && success;
    return success;
}

static bool readTextFile(const char* path, char* buffer, size_t buffer_size) {
    FILE* file_ptr = fopen(path, "rb");
    if (!file_ptr) {
        return false;
    }
    size_t read_len = fread(buffer, 1U, buffer_size - 1U, file_ptr);
    bool success = !ferror(file_ptr) && feof(file_ptr);
    buffer[read_len] = '\0';
    success = fclose(file_ptr) == 0 && success;
    return success;
}

static domino_road_id_t buildRoadIdForTest(domino_fork_road_id_t start_id, domino_fork_road_id_t target_id) {
    return ((domino_road_id_t)start_id << 32U) | (domino_road_id_t)target_id;
}

static const DominoNavDataSegment* findDataSegmentForTest(const DominoNavDataSegmentVec* segment_list, uint8_t region_index,
                                                          uint8_t network_type) {
    for (size_t segment_index = 0U; segment_index < kv_size(*segment_list); segment_index++) {
        const DominoNavDataSegment* segment = &kv_A(*segment_list, segment_index);
        if (segment->region_index == region_index && segment->road_network_type == network_type) {
            return segment;
        }
    }
    return nullptr;
}

int main(void) {
    int exit_code = EXIT_FAILURE;
    bool engine_active = false;
    char temp_root[] = "/tmp/domino-storage-lifecycle.XXXXXX";
    if (!mkdtemp(temp_root)) {
        (void)fprintf(stderr, "mkdtemp failed\n");
        return EXIT_FAILURE;
    }

    char invalid_world_path[TEST_PATH_MAX];
    char invalid_meta_path[TEST_PATH_MAX];
    char world_path[TEST_PATH_MAX];
    char mutable_world_path[TEST_PATH_MAX];
    char root_meta_path[TEST_PATH_MAX];
    char fork_road_meta_path[TEST_PATH_MAX];
    char road_meta_path[TEST_PATH_MAX];
    char fork_road_path[TEST_PATH_MAX];
    char world_backup_path[TEST_PATH_MAX];
    char external_dir_path[TEST_PATH_MAX];
    char external_marker_path[TEST_PATH_MAX];
    char recursive_delete_path[TEST_PATH_MAX];
    char recursive_link_path[TEST_PATH_MAX];
    char retry_save_path[TEST_PATH_MAX];
    char isolation_world_a_path[TEST_PATH_MAX];
    char isolation_world_b_path[TEST_PATH_MAX];
    (void)snprintf(invalid_world_path, sizeof(invalid_world_path), "%s/invalid-world", temp_root);
    (void)snprintf(invalid_meta_path, sizeof(invalid_meta_path), "%s/meta.json", invalid_world_path);
    (void)snprintf(world_path, sizeof(world_path), "%s/new/parent/tree/world", temp_root);
    (void)snprintf(mutable_world_path, sizeof(mutable_world_path), "%s", world_path);
    (void)snprintf(root_meta_path, sizeof(root_meta_path), "%s/meta.json", world_path);
    (void)snprintf(fork_road_meta_path, sizeof(fork_road_meta_path), "%s/fork_road.meta.json", world_path);
    (void)snprintf(road_meta_path, sizeof(road_meta_path), "%s/road.meta.json", world_path);
    (void)snprintf(fork_road_path, sizeof(fork_road_path), "%s/fork_road_0.json", world_path);
    (void)snprintf(world_backup_path, sizeof(world_backup_path), "%s.prev", world_path);
    (void)snprintf(external_dir_path, sizeof(external_dir_path), "%s/external-data", temp_root);
    (void)snprintf(external_marker_path, sizeof(external_marker_path), "%s/keep.txt", external_dir_path);
    (void)snprintf(recursive_delete_path, sizeof(recursive_delete_path), "%s/delete-root", temp_root);
    (void)snprintf(recursive_link_path, sizeof(recursive_link_path), "%s/external-link", recursive_delete_path);
    (void)snprintf(retry_save_path, sizeof(retry_save_path), "%s/retry-save-world", temp_root);
    (void)snprintf(isolation_world_a_path, sizeof(isolation_world_a_path), "%s/isolation-world-a", temp_root);
    (void)snprintf(isolation_world_b_path, sizeof(isolation_world_b_path), "%s/isolation-world-b", temp_root);

#define TEST_CHECK(condition, message)                        \
    do {                                                      \
        if (!(condition)) {                                   \
            (void)fprintf(stderr, "FAILED: %s\n", (message)); \
            goto cleanup;                                     \
        }                                                     \
    } while (0)

    TEST_CHECK(dominoMkdirRecursive(invalid_world_path) == CODE_OK, "create invalid world directory");
    TEST_CHECK(writeTextFile(invalid_meta_path, "{}"), "write invalid meta.json");

    DominoEngineLaunchConfig invalid_config = {.storage_path = invalid_world_path, .storage_integrity_verify = false};
    TEST_CHECK(dominoEngineInit(invalid_config) != CODE_OK, "invalid save must fail initialization");

    DominoEngineLaunchConfig create_config = {.storage_path = mutable_world_path, .storage_integrity_verify = false};
    TEST_CHECK(dominoEngineInit(create_config) == CODE_OK, "retry initialization after failure");
    engine_active = true;

    DominoForkRoad start = {.position_x = 10, .position_y = 20};
    DominoForkRoad target = {.position_x = 30, .position_y = 40};
    TEST_CHECK(dominoAddRoad(&start, &target, 7U, 0U, ROAD_NETWORK_TYPE_ROAD) == CODE_OK, "add canonical road data");
    TEST_CHECK(start.id != 0U && target.id != 0U, "allocate fork road ids");
    domino_road_id_t road_id = buildRoadIdForTest(start.id, target.id);
    for (uint32_t road_index = 0U; road_index < 600U; road_index++) {
        DominoForkRoad extra_start = {.position_x = (int32_t)road_index};
        DominoForkRoad extra_target = {.position_y = (int32_t)road_index};
        TEST_CHECK(dominoAddRoad(&extra_start, &extra_target, 3U, 0U, ROAD_NETWORK_TYPE_ROAD) == CODE_OK,
                   "batch add roads across fork-road vector growth");
    }
    DominoForkRoad* current_start = dominoGetForkRoadByID(start.id);
    TEST_CHECK(current_start != nullptr && current_start->to_fork_road_count == 1U && current_start->to_fork_road_id[0] == target.id,
               "id map and adjacency remain valid across vector growth");
    const DominoNavDataSegment* runtime_fork_segment =
        findDataSegmentForTest(&domino_all_fork_road_nav_data_segment_list, 0U, ROAD_NETWORK_TYPE_ROAD);
    TEST_CHECK(runtime_fork_segment != nullptr && runtime_fork_segment->init_index == 0U && runtime_fork_segment->init_count == 0U &&
                   runtime_fork_segment->increment_index == 0U && runtime_fork_segment->total_count == 1202U,
               "new world keeps fork roads in an increment tail until save");
    const DominoNavDataSegment* runtime_road_segment =
        findDataSegmentForTest(&domino_all_road_nav_data_segment_list, 0U, ROAD_NETWORK_TYPE_ROAD);
    TEST_CHECK(runtime_road_segment != nullptr && runtime_road_segment->init_index == 0U && runtime_road_segment->init_count == 0U &&
                   runtime_road_segment->increment_index == 0U && runtime_road_segment->total_count == 601U,
               "new world keeps roads in an increment tail until save");

    memset(mutable_world_path, 'x', strlen(mutable_world_path));
    mutable_world_path[0] = '\0';
    TEST_CHECK(dominoEngineExit() == CODE_OK, "save new world into recursively created parent directories");
    engine_active = false;

    static char meta_buffer[TEST_META_BUFFER_SIZE];
    TEST_CHECK(readTextFile(root_meta_path, meta_buffer, sizeof(meta_buffer)), "read generated root meta.json");
    TEST_CHECK(strstr(meta_buffer, "\"version\":5") != nullptr, "write storage format version 5");
    TEST_CHECK(strstr(meta_buffer, "meta_file") == nullptr && strstr(meta_buffer, "shard_prefix") == nullptr,
               "manifest must not persist derived file names");
    TEST_CHECK(readTextFile(fork_road_meta_path, meta_buffer, sizeof(meta_buffer)), "read generated fork road meta");
    TEST_CHECK(strstr(meta_buffer, "data_segment") != nullptr && strstr(meta_buffer, "init_index") != nullptr &&
                   strstr(meta_buffer, "init_count") != nullptr && strstr(meta_buffer, "increment_index") != nullptr &&
                   strstr(meta_buffer, "total_count") != nullptr && strstr(meta_buffer, "region_index") != nullptr &&
                   strstr(meta_buffer, "road_network_type") != nullptr,
               "fork road meta must persist all data segment fields");
    char* segment_region_index = strstr(meta_buffer, "\"region_index\":0");
    TEST_CHECK(segment_region_index != nullptr, "find persisted fork road segment region index");
    char* segment_region_index_digit = segment_region_index + strlen("\"region_index\":");
    TEST_CHECK(*segment_region_index_digit == '0', "locate persisted fork road segment region digit");
    *segment_region_index_digit = '1';
    TEST_CHECK(writeTextFile(fork_road_meta_path, meta_buffer), "write inconsistent fork road data segment");
    DominoEngineLaunchConfig reload_config = {.storage_path = world_path, .storage_integrity_verify = true};
    TEST_CHECK(dominoEngineInit(reload_config) != CODE_OK, "reject data segment inconsistent with loaded entities");
    *segment_region_index_digit = '0';
    TEST_CHECK(writeTextFile(fork_road_meta_path, meta_buffer), "restore valid fork road data segment");
    TEST_CHECK(readTextFile(road_meta_path, meta_buffer, sizeof(meta_buffer)), "read generated road meta");
    TEST_CHECK(strstr(meta_buffer, "data_segment") != nullptr && strstr(meta_buffer, "init_index") != nullptr &&
                   strstr(meta_buffer, "init_count") != nullptr && strstr(meta_buffer, "increment_index") != nullptr &&
                   strstr(meta_buffer, "total_count") != nullptr && strstr(meta_buffer, "region_index") != nullptr &&
                   strstr(meta_buffer, "road_network_type") != nullptr,
               "road meta must persist all data segment fields");
    TEST_CHECK(readTextFile(fork_road_path, meta_buffer, sizeof(meta_buffer)), "read generated fork road shard");
    TEST_CHECK(strstr(meta_buffer, "to_fork_road_id") == nullptr && strstr(meta_buffer, "to_fork_road_count") == nullptr,
               "fork road shard must not persist derived adjacency");

    TEST_CHECK(dominoEngineInit(reload_config) == CODE_OK, "reload self-generated world with a different next-save policy");
    engine_active = true;
    DominoForkRoad* loaded_start = dominoGetForkRoadByID(start.id);
    TEST_CHECK(loaded_start != nullptr, "rebuild fork road id map after load");
    TEST_CHECK(dominoGetRoadByID(road_id) != nullptr, "rebuild road id map after load");
    TEST_CHECK(kv_size(domino_all_fork_road_nav_data_segment_list) == 1U, "restore fork road data segment from module meta");
    const DominoNavDataSegment* fork_segment = &kv_A(domino_all_fork_road_nav_data_segment_list, 0U);
    TEST_CHECK(fork_segment->init_index == 0U && fork_segment->init_count == 1202U && fork_segment->increment_index == 1202U &&
                   fork_segment->total_count == 1202U && fork_segment->region_index == 0U &&
                   fork_segment->road_network_type == ROAD_NETWORK_TYPE_ROAD,
               "restored fork road data segment must match persisted canonical layout");
    TEST_CHECK(kv_size(domino_all_road_nav_data_segment_list) == 1U, "restore road data segment from module meta");
    const DominoNavDataSegment* road_segment = &kv_A(domino_all_road_nav_data_segment_list, 0U);
    TEST_CHECK(road_segment->init_index == 0U && road_segment->init_count == 601U && road_segment->increment_index == 601U &&
                   road_segment->total_count == 601U && road_segment->region_index == 0U &&
                   road_segment->road_network_type == ROAD_NETWORK_TYPE_ROAD,
               "restored road data segment must match persisted canonical layout");
    TEST_CHECK(loaded_start->to_fork_road_count == 1U && loaded_start->to_fork_road_id[0] == target.id,
               "rebuild adjacency from canonical road data after load");

    size_t loaded_start_index = (size_t)(loaded_start - domino_all_fork_road_list.a);
    DominoForkRoad tail_a1_start = {.position_x = 101, .position_y = 102};
    DominoForkRoad tail_a1_target = {.position_x = 103, .position_y = 104};
    DominoForkRoad tail_b1_start = {.position_x = 201, .position_y = 202};
    DominoForkRoad tail_b1_target = {.position_x = 203, .position_y = 204};
    DominoForkRoad tail_a2_start = {.position_x = 301, .position_y = 302};
    DominoForkRoad tail_a2_target = {.position_x = 303, .position_y = 304};
    DominoForkRoad tail_b2_start = {.position_x = 401, .position_y = 402};
    DominoForkRoad tail_b2_target = {.position_x = 403, .position_y = 404};
    TEST_CHECK(dominoAddRoad(&tail_a1_start, &tail_a1_target, 5U, 0U, ROAD_NETWORK_TYPE_ROAD) == CODE_OK,
               "append first delta road to persisted group");
    TEST_CHECK(dominoAddRoad(&tail_b1_start, &tail_b1_target, 5U, 1U, ROAD_NETWORK_TYPE_RAILWAY) == CODE_OK,
               "append first runtime-only group");
    TEST_CHECK(dominoAddRoad(&tail_a2_start, &tail_a2_target, 5U, 0U, ROAD_NETWORK_TYPE_ROAD) == CODE_OK,
               "interleave persisted-group delta after runtime-only group");
    TEST_CHECK(dominoAddRoad(&tail_b2_start, &tail_b2_target, 5U, 1U, ROAD_NETWORK_TYPE_RAILWAY) == CODE_OK,
               "reuse runtime-only group across an interleaved tail");

    runtime_fork_segment = findDataSegmentForTest(&domino_all_fork_road_nav_data_segment_list, 0U, ROAD_NETWORK_TYPE_ROAD);
    runtime_road_segment = findDataSegmentForTest(&domino_all_road_nav_data_segment_list, 0U, ROAD_NETWORK_TYPE_ROAD);
    TEST_CHECK(runtime_fork_segment != nullptr && runtime_fork_segment->init_index == 0U && runtime_fork_segment->init_count == 1202U &&
                   runtime_fork_segment->increment_index == 1202U && runtime_fork_segment->total_count == 1206U,
               "persisted fork base remains fixed while its delta count grows");
    TEST_CHECK(runtime_road_segment != nullptr && runtime_road_segment->init_index == 0U && runtime_road_segment->init_count == 601U &&
                   runtime_road_segment->increment_index == 601U && runtime_road_segment->total_count == 603U,
               "persisted road base remains fixed while its delta count grows");
    const DominoNavDataSegment* railway_fork_segment =
        findDataSegmentForTest(&domino_all_fork_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    const DominoNavDataSegment* railway_road_segment =
        findDataSegmentForTest(&domino_all_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    TEST_CHECK(railway_fork_segment != nullptr && railway_fork_segment->init_index == 0U && railway_fork_segment->init_count == 0U &&
                   railway_fork_segment->increment_index == 1204U && railway_fork_segment->total_count == 4U,
               "runtime-only fork group records its first tail index");
    TEST_CHECK(railway_road_segment != nullptr && railway_road_segment->init_index == 0U && railway_road_segment->init_count == 0U &&
                   railway_road_segment->increment_index == 602U && railway_road_segment->total_count == 2U,
               "runtime-only road group records its own first tail index");
    TEST_CHECK(dominoGetForkRoadByID(start.id) == &kv_A(domino_all_fork_road_list, loaded_start_index),
               "interleaved additions do not reorder the persisted base");
    TEST_CHECK(kv_A(domino_all_fork_road_list, 1202U).id == tail_a1_start.id &&
                   kv_A(domino_all_fork_road_list, 1204U).id == tail_b1_start.id &&
                   kv_A(domino_all_fork_road_list, 1206U).id == tail_a2_start.id &&
                   kv_A(domino_all_fork_road_list, 1208U).id == tail_b2_start.id,
               "fork entities preserve interleaved append order");

    TEST_CHECK(dominoNavPathPlanning(1U, ROAD_NETWORK_TYPE_RAILWAY, nullptr) == CODE_OK,
               "plan a runtime-only group directly from the filtered delta tail");
    railway_fork_segment = findDataSegmentForTest(&domino_all_fork_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    railway_road_segment = findDataSegmentForTest(&domino_all_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    const DominoNavPathCache* railway_path_cache_ptr = navStateGetPathCache(1U, ROAD_NETWORK_TYPE_RAILWAY);
    TEST_CHECK(railway_fork_segment != nullptr && railway_fork_segment->init_count == 0U && railway_fork_segment->increment_index == 1204U &&
                   railway_path_cache_ptr != nullptr && railway_path_cache_ptr->vertex_count == 4U,
               "path planning builds a filtered four-vertex cache from the interleaved tail without compaction");
    DominoRoadLine tail_b1_line = {
        .current_fork_road_id = tail_b1_start.id,
        .target_fork_road_id = tail_b1_target.id,
        .type = ROAD_NETWORK_TYPE_RAILWAY,
        .region_index = 1U,
    };
    DominoRoadLine tail_b2_line = {
        .current_fork_road_id = tail_b2_start.id,
        .target_fork_road_id = tail_b2_target.id,
        .type = ROAD_NETWORK_TYPE_RAILWAY,
        .region_index = 1U,
    };
    TEST_CHECK(dominoNavigation(&tail_b1_line) == CODE_OK && tail_b1_line.remaining_distance == 5U &&
                   dominoNavigation(&tail_b2_line) == CODE_OK && tail_b2_line.remaining_distance == 5U,
               "runtime-only cache routes both disjoint roads selected from the interleaved tail");
    TEST_CHECK(dominoGetForkRoadByID(start.id) == &kv_A(domino_all_fork_road_list, loaded_start_index),
               "path planning does not reorder the long-lived base and tail");
    TEST_CHECK(railway_road_segment != nullptr && railway_road_segment->init_count == 0U && railway_road_segment->increment_index == 602U &&
                   kv_A(domino_all_fork_road_list, 1202U).id == tail_a1_start.id &&
                   kv_A(domino_all_fork_road_list, 1204U).id == tail_b1_start.id &&
                   kv_A(domino_all_fork_road_list, 1206U).id == tail_a2_start.id &&
                   kv_A(domino_all_fork_road_list, 1208U).id == tail_b2_start.id &&
                   kv_A(domino_all_road_list, 601U).id == buildRoadIdForTest(tail_a1_start.id, tail_a1_target.id) &&
                   kv_A(domino_all_road_list, 602U).id == buildRoadIdForTest(tail_b1_start.id, tail_b1_target.id) &&
                   kv_A(domino_all_road_list, 603U).id == buildRoadIdForTest(tail_a2_start.id, tail_a2_target.id) &&
                   kv_A(domino_all_road_list, 604U).id == buildRoadIdForTest(tail_b2_start.id, tail_b2_target.id),
               "path planning preserves the interleaved physical tail and both segment boundaries");

    domino_road_id_t tail_b1_road_id = buildRoadIdForTest(tail_b1_start.id, tail_b1_target.id);
    domino_road_id_t tail_b2_road_id = buildRoadIdForTest(tail_b2_start.id, tail_b2_target.id);
    TEST_CHECK(dominoRemoveRoad(tail_b1_road_id) == CODE_OK && dominoRemoveRoad(tail_b2_road_id) == CODE_OK,
               "remove every active road from a runtime-only group");
    railway_fork_segment = findDataSegmentForTest(&domino_all_fork_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    railway_road_segment = findDataSegmentForTest(&domino_all_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    railway_path_cache_ptr = navStateGetPathCache(1U, ROAD_NETWORK_TYPE_RAILWAY);
    TEST_CHECK(railway_fork_segment != nullptr && railway_fork_segment->increment_index == 1204U && railway_fork_segment->total_count == 0U &&
                   railway_path_cache_ptr == nullptr,
               "zero-count runtime fork group retains its original tail boundary and invalidates caches");
    TEST_CHECK(railway_road_segment != nullptr && railway_road_segment->increment_index == 602U && railway_road_segment->total_count == 0U,
               "zero-count runtime road group remains available until save compaction");

    DominoForkRoad tail_b3_start = {.position_x = 501, .position_y = 502};
    DominoForkRoad tail_b3_target = {.position_x = 503, .position_y = 504};
    TEST_CHECK(dominoAddRoad(&tail_b3_start, &tail_b3_target, 5U, 1U, ROAD_NETWORK_TYPE_RAILWAY) == CODE_OK,
               "reuse a zero-count runtime group");
    railway_fork_segment = findDataSegmentForTest(&domino_all_fork_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    railway_road_segment = findDataSegmentForTest(&domino_all_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    TEST_CHECK(railway_fork_segment != nullptr && railway_fork_segment->init_count == 0U && railway_fork_segment->increment_index == 1204U &&
                   railway_fork_segment->total_count == 2U,
               "reused fork group keeps its first-ever runtime tail boundary");
    TEST_CHECK(railway_road_segment != nullptr && railway_road_segment->init_count == 0U && railway_road_segment->increment_index == 602U &&
                   railway_road_segment->total_count == 1U,
               "reused road group keeps its first-ever runtime tail boundary");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "resave loaded world with integrity enabled");
    engine_active = false;

    reload_config.storage_integrity_verify = false;
    TEST_CHECK(dominoEngineInit(reload_config) == CODE_OK, "load integrity-protected world independent of next-save policy");
    engine_active = true;
    TEST_CHECK(dominoGetRoadByID(road_id) != nullptr, "world remains loadable after second save");
    const DominoNavDataSegment* compacted_road_fork_segment =
        findDataSegmentForTest(&domino_all_fork_road_nav_data_segment_list, 0U, ROAD_NETWORK_TYPE_ROAD);
    const DominoNavDataSegment* compacted_railway_fork_segment =
        findDataSegmentForTest(&domino_all_fork_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    const DominoNavDataSegment* compacted_road_segment =
        findDataSegmentForTest(&domino_all_road_nav_data_segment_list, 0U, ROAD_NETWORK_TYPE_ROAD);
    const DominoNavDataSegment* compacted_railway_road_segment =
        findDataSegmentForTest(&domino_all_road_nav_data_segment_list, 1U, ROAD_NETWORK_TYPE_RAILWAY);
    TEST_CHECK(compacted_road_fork_segment != nullptr && compacted_road_fork_segment->init_index == 0U &&
                   compacted_road_fork_segment->init_count == 1206U && compacted_road_fork_segment->increment_index == 1208U &&
                   compacted_road_fork_segment->total_count == 1206U,
               "save compacts the persisted fork group into a new canonical base");
    TEST_CHECK(compacted_railway_fork_segment != nullptr && compacted_railway_fork_segment->init_index == 1206U &&
                   compacted_railway_fork_segment->init_count == 2U && compacted_railway_fork_segment->increment_index == 1208U &&
                   compacted_railway_fork_segment->total_count == 2U,
               "save compacts the reused runtime fork group into a canonical base");
    TEST_CHECK(compacted_road_segment != nullptr && compacted_road_segment->init_index == 0U && compacted_road_segment->init_count == 603U &&
                   compacted_road_segment->increment_index == 604U && compacted_road_segment->total_count == 603U,
               "save compacts the persisted road group into a new canonical base");
    TEST_CHECK(compacted_railway_road_segment != nullptr && compacted_railway_road_segment->init_index == 603U &&
                   compacted_railway_road_segment->init_count == 1U && compacted_railway_road_segment->increment_index == 604U &&
                   compacted_railway_road_segment->total_count == 1U,
               "save compacts the reused runtime road group into a canonical base");
    TEST_CHECK(kv_A(domino_all_fork_road_list, 1208U).status != 0U && kv_A(domino_all_road_list, 604U).status != 0U,
               "save moves runtime tombstones behind the active canonical prefix");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "final save");
    engine_active = false;

    TEST_CHECK(rename(world_path, world_backup_path) == 0, "simulate crash after active-to-backup rename");
    TEST_CHECK(dominoEngineInit(reload_config) == CODE_OK, "restore backup when active save is missing");
    engine_active = true;
    TEST_CHECK(dominoGetRoadByID(road_id) != nullptr, "restored backup remains loadable");
    TEST_CHECK(!dominoPathExists(world_backup_path, false), "backup path is consumed by recovery");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "save after backup recovery");
    engine_active = false;

    TEST_CHECK(dominoMkdirRecursive(world_backup_path) == CODE_OK, "create stale backup after committed active save");
    TEST_CHECK(dominoEngineInit(reload_config) == CODE_OK, "prefer committed active save over stale backup");
    engine_active = true;
    TEST_CHECK(dominoGetRoadByID(road_id) != nullptr, "committed active save remains authoritative");
    TEST_CHECK(!dominoPathExists(world_backup_path, false), "stale backup is removed after active save is confirmed");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "save after stale-backup cleanup");
    engine_active = false;

    TEST_CHECK(dominoMkdirRecursive(external_dir_path) == CODE_OK, "create external directory used by symlink safety case");
    TEST_CHECK(writeTextFile(external_marker_path, "keep"), "write external marker");
    TEST_CHECK(symlink(external_dir_path, world_backup_path) == 0, "create backup symlink conflict");
    TEST_CHECK(dominoEngineInit(reload_config) != CODE_OK, "refuse backup symlink instead of following it");
    TEST_CHECK(readTextFile(external_marker_path, meta_buffer, sizeof(meta_buffer)), "external marker survives rejected recovery");
    TEST_CHECK(strcmp(meta_buffer, "keep") == 0, "backup symlink target remains untouched");
    TEST_CHECK(unlink(world_backup_path) == 0, "remove rejected backup symlink");

    TEST_CHECK(dominoMkdirRecursive(recursive_delete_path) == CODE_OK, "create recursive-delete root");
    TEST_CHECK(symlink(external_dir_path, recursive_link_path) == 0, "create nested symlink for recursive-delete safety");
    TEST_CHECK(dominoRemovePathRecursive(recursive_delete_path) == 0, "recursive delete unlinks nested symlink only");
    TEST_CHECK(readTextFile(external_marker_path, meta_buffer, sizeof(meta_buffer)), "nested symlink target survives recursive delete");
    TEST_CHECK(strcmp(meta_buffer, "keep") == 0, "recursive delete does not enter symlink target");

    DominoEngineLaunchConfig retry_save_config = {.storage_path = retry_save_path, .storage_integrity_verify = false};
    TEST_CHECK(dominoEngineInit(retry_save_config) == CODE_OK, "initialize world used to exercise save retry");
    engine_active = true;
    DominoForkRoad retry_start = {.position_x = 50, .position_y = 60};
    DominoForkRoad retry_target = {.position_x = 70, .position_y = 80};
    TEST_CHECK(dominoAddRoad(&retry_start, &retry_target, 9U, 0U, ROAD_NETWORK_TYPE_ROAD) == CODE_OK,
               "add world data retained across save failure");
    domino_road_id_t retry_road_id = buildRoadIdForTest(retry_start.id, retry_target.id);
    TEST_CHECK(writeTextFile(retry_save_path, "blocking regular file"), "create external save-path conflict");
    TEST_CHECK(dominoEngineExit() != CODE_OK, "save-path conflict must fail without destroying the in-memory world");
    TEST_CHECK(dominoGetRoadByID(retry_road_id) != nullptr, "world data remains in memory after save failure");
    TEST_CHECK(dominoEngineThawTime() == ERR_GAME_STATE_INVALID, "stopping engine cannot be thawed between save retries");
    TEST_CHECK(remove(retry_save_path) == 0, "remove external save-path conflict");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "retry save after resolving external IO conflict");
    engine_active = false;

    TEST_CHECK(dominoEngineInit(retry_save_config) == CODE_OK, "reload world written by retried save");
    engine_active = true;
    TEST_CHECK(dominoGetRoadByID(retry_road_id) != nullptr, "retried save contains retained world data");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "exit reloaded retry-save world");
    engine_active = false;

    DominoEngineLaunchConfig isolation_world_a_config = {.storage_path = isolation_world_a_path, .storage_integrity_verify = false};
    TEST_CHECK(dominoEngineInit(isolation_world_a_config) == CODE_OK, "initialize first world used to verify process-local isolation");
    engine_active = true;
    g_domino_region_count = 1U;
    atomic_store(&g_domino_region_list[0].human_count, 37);
    g_domino_region_list[0].type = 5U;
    g_domino_region_list[0].status = 2U;
    atomic_store(&g_domino_global_increment_id.human_increment_id, 701U);
    (void)snprintf(g_domino_storage_account_id, sizeof(g_domino_storage_account_id), "%s", "account-a");
    (void)snprintf(g_domino_storage_run_mode, sizeof(g_domino_storage_run_mode), "%s", "server");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "save first isolated world");
    engine_active = false;

    DominoEngineLaunchConfig isolation_world_b_config = {.storage_path = isolation_world_b_path, .storage_integrity_verify = false};
    TEST_CHECK(dominoEngineInit(isolation_world_b_config) == CODE_OK, "initialize a different empty world in the same process");
    engine_active = true;
    TEST_CHECK(g_domino_region_count == 0U, "empty world must not inherit region count from previous world");
    TEST_CHECK(atomic_load(&g_domino_region_list[0].human_count) == 0 && g_domino_region_list[0].type == 0U &&
                   g_domino_region_list[0].status == 0U,
               "empty world must not inherit fixed region data from previous world");
    TEST_CHECK(atomic_load(&g_domino_global_increment_id.human_increment_id) == 0U,
               "empty world must not inherit global increment ids from previous world");
    TEST_CHECK(g_domino_storage_account_id[0] == '\0' && strcmp(g_domino_storage_run_mode, "local") == 0,
               "empty world must use fresh root-meta defaults");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "save second isolated world");
    engine_active = false;

    TEST_CHECK(dominoEngineInit(isolation_world_a_config) == CODE_OK, "reload first world after switching storage paths");
    engine_active = true;
    TEST_CHECK(g_domino_region_count == 1U && atomic_load(&g_domino_region_list[0].human_count) == 37 &&
                   g_domino_region_list[0].type == 5U && g_domino_region_list[0].status == 2U,
               "stored region state must override the empty initialization baseline");
    TEST_CHECK(atomic_load(&g_domino_global_increment_id.human_increment_id) == 701U,
               "stored global increment ids must override the empty initialization baseline");
    TEST_CHECK(strcmp(g_domino_storage_account_id, "account-a") == 0 && strcmp(g_domino_storage_run_mode, "server") == 0,
               "stored root-meta state must be restored after switching worlds");
    TEST_CHECK(dominoEngineExit() == CODE_OK, "exit reloaded first isolated world");
    engine_active = false;

    exit_code = EXIT_SUCCESS;

cleanup:
    if (engine_active) {
        (void)dominoEngineExit();
    }
    (void)dominoRemovePathRecursive(temp_root);
    return exit_code;
}
