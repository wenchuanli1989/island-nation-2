#ifndef DOMINO_ENGINE_NAV_LAYOUT_H
#define DOMINO_ENGINE_NAV_LAYOUT_H

/** @brief 保存前把运行期增量尾并入基础段：逻辑删除的实体排到末尾，活跃实体按 (region_index, road_network_type, id) 排序，
 * 重建分段并刷新映射表下标。
 * @pre 已清空所有保存了实体地址的路径缓存；调用期间无并发修改。
 */
void navLayoutCompact(void);

#endif
