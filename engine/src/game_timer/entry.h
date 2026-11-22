#ifndef DOMINO_ENGINE_GAME_TIMER_H
#define DOMINO_ENGINE_GAME_TIMER_H

#include "../queue/message.h"
#include "domino_shared_error_codes.h"

/** @file entry.h
 * @brief GameTimer：按累计未冻结业务毫秒到期，把一次性通知投递到业务方的收件队列（宪法第二章）。
 * @note 请求经“命令队列 → 全局最小堆 → 业务收件队列”按值流转；不执行业务、不持久化，改期须取消后重新登记。
 * 计时线程在批次边界检查退出和冻结；一批最多处理 DOMINO_GAME_TIMER_BATCH_MAX 个堆项和同样数量的命令。
 * 调用顺序由引擎保证，模块不维护生命周期状态；同步、分配和投递失败均致命退出。
 */

/** @brief 初始化命令队列和同步对象，不创建线程。 */
void dominoGameTimerModuleInit(void);

/** @brief 创建计时线程。 */
void dominoGameTimerModuleStart(void);

/** @brief join 计时线程，保留队列、堆和同步对象。
 * @pre 引擎已进入 STOPPING 并唤醒计时线程，其他 GameTimer 接口调用已结束；目标收件队列在返回前须保持可入队。
 */
void dominoGameTimerModuleStop(void);

/** @brief 丢弃未投递的请求，释放资源并清零模块状态，不 join 线程；已投递的通知归业务方处理。 */
void dominoGameTimerModuleDestroy(void);

/** @brief 解冻或退出状态发布后唤醒计时线程。 */
void dominoGameTimerNotifyTimeChanged(void);

/** @brief 异步登记一个绝对截止点；入队后通知计时线程，不等待入堆。
 * @param event_queue_ptr 业务方拥有、元素为 DominoHumanInboxMessage 的收件队列，计时线程停止前须保持有效。
 * @param due_ms 截止点；已过期也照常登记，尽快投递。
 * @param event_type、target_id 业务自定义的事件类型和目标 ID，原样随通知送回。
 * @param game_timer_id_out_ptr 输出本次 Init 内唯一的非零登记 ID，可凭它取消；失败时为 0。
 * @return CODE_OK；空指针 ERR_NULL_POINTER；队列元素大小不符 ERR_INVALID_PARAM。
 * @note 到期通知为收件消息，game_timer 按值保存原登记记录。
 * 登记时会读取一次业务时间做范围校验。冻结期间和退出末批仍可登记。
 */
DOMINO_CODE dominoGameTimerScheduleAt(DominoThreadQueue* event_queue_ptr, domino_runtime_ms_t due_ms, uint8_t event_type, uint64_t target_id,
                                      domino_game_timer_id_t* game_timer_id_out_ptr);

/** @brief 依次在命令队列、全局堆和 event_queue_ptr 中按登记 ID 查找并标记取消，不删除任何元素（宪法 2.3）。
 * @param event_queue_ptr 登记时使用的收件队列，只限定已投递通知的查找范围。
 * @return 找到（含已取消）返回 CODE_OK；ID 为 0 或元素大小不符 ERR_INVALID_PARAM；空指针 ERR_NULL_POINTER；
 *         不存在、已移除或已出队返回 ERR_NOT_FOUND。结果由调用方记录。
 * @note 已出队的通知无法撤回：消费方须先跳过已取消的通知，再核对当前有效的 game_timer_id。
 */
DOMINO_CODE dominoGameTimerCancel(domino_game_timer_id_t game_timer_id, DominoThreadQueue* event_queue_ptr);

#endif
