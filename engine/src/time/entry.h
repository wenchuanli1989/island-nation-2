#ifndef DOMINO_ENGINE_TIME_H
#define DOMINO_ENGINE_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_types.h"

/** @file entry.h
 * @brief 业务时钟：只累计未冻结的现实时间，跨存档延续（宪法第七章）。
 * @note 首次静态初值和 Destroy 都提供空的冻结状态；storage 加载恢复累计基准，Start 解冻时建立单调采样起点。
 * 冻结、解冻、设置基准由生命周期控制方串行调用；读取接口任何线程可用，无需加锁。
 * 时钟采样失败、倒退或累计值溢出均致命退出。
 */

/** @brief 准备冻结等待用的锁和条件变量，不改时间状态、不采样时钟。 */
extern void dominoTimeModuleInit(void);

/** @brief 释放等待资源，清零累计基准、采样起点和等待计数，冻结代次恢复为 1 并保持冻结。
 * @pre 业务线程已 join、世界已保存。存档中的累计时间不受影响。
 */
extern void dominoTimeModuleDestroy(void);

/** @brief 把已走过的时间并入基准并冻结，冻结代次加一、本轮等待计数清零；已冻结时不变。
 * @note 不通知、不等待业务线程。冻结代次耗尽致命退出。
 */
extern void dominoTimeModuleFreeze(void);

/** @brief 以当前单调时刻为新起点解冻，并广播冻结等待者；不计入冻结期间的现实时间。
 * @pre 已冻结，且冻结基准已通过 32 位业务毫秒范围校验。
 */
extern void dominoTimeModuleThaw(void);

extern bool dominoTimeModuleIsFrozen(void);

/** @brief 冻结时返回当前非零冻结代次，未冻结返回 0；代次不是业务时间。 */
extern uint64_t dominoTimeModuleGetFreezeEpoch(void);

/** @brief 在同一轮冻结期间等待；解冻、代次变化或引擎停止时返回，0 或过期代次立即返回。
 * @note 返回后须回到批次入口重新检查退出和冻结。调用时不得持有其他线程继续工作所需的锁。
 */
extern void dominoTimeModuleWait(uint64_t observed_epoch);

/** @brief 唤醒冻结等待者重新检查状态，不改变时间或冻结状态；Stop 发布 STOPPING 后调用。 */
extern void dominoTimeModuleWakeAll(void);

/** @brief 本轮冻结中正在条件等待的线程数，只用于显示，不参与任何控制判断。 */
extern uint32_t dominoTimeModuleGetFreezeWaiterCount(void);

/** @brief 累计未冻结现实纳秒（存档基准 + 本次运行时间）；业务计时须用 dominoTimeGetRuntimeMs。 */
extern void dominoTimeGetRuntimeNs(uint64_t* runtime_ns_ptr);

/** @brief 32 位累计未冻结现实毫秒（纳秒除以 1,000,000，不应用游戏日历倍率）。
 * @note 空指针或超出 UINT32_MAX 毫秒致命退出，不截断、回绕或饱和。
 */
extern void dominoTimeGetRuntimeMs(domino_runtime_ms_t* runtime_ms_ptr);

/** @brief 存档使用的累计纳秒基准。 */
extern uint64_t dominoTimeGetRuntimeBaseNs(void);

/** @brief 恢复存档的累计纳秒基准；业务读取时另行校验 32 位毫秒范围。 */
extern void dominoTimeSetRuntimeBaseNs(uint64_t base_ns);
#endif
