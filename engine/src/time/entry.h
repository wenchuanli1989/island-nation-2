#ifndef DOMINO_ENGINE_TIME_H
#define DOMINO_ENGINE_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_types.h"

/* 静态初值和 Destroy 保证空的冻结快照；storage 加载累计基准，Start 解冻时建立单调采样起点，无需 Init 重置。 */

/**
 * @brief 将累计基准、单调采样起点和快照版本归零，恢复冻结状态。
 * @pre 所有业务线程已 join、世界已保存，且无并发 time API 调用。
 * @note 无独立 Init 生命周期；由引擎保证仅在客户端的 DESTROYING 阶段调用，本接口不重复校验。
 * @note 仅清空本实例的内存状态；不修改已保存的累计时间，下次 Init 仍从存档恢复。
 */
extern void dominoTimeModuleDestroy(void);

/**
 * @brief 冻结真实累计纳秒基准；已冻结时保持原基准。
 * @note 客户端串行执行生命周期时间写入。普通冻结只发布时钟状态，不通知或等待业务线程；线程在批次边界检查。
 * 调用方通过 dominoTimeGetRuntimeMs() 校验冻结时刻范围；退出时等待业务线程结束后再按此时刻结算。
 * @note 时钟采样失败、倒退或累计纳秒溢出直接打印到 stderr 并终止进程。
 */
extern void dominoTimeModuleFreeze(void);

/**
 * @brief 从冻结的累计基准重新建立单调起点并解冻，不计入冻结期间的现实经过量。
 * @pre 调用方须保证冻结基准已通过 32 位业务范围校验，且校验后未被修改。
 * @note 未冻结时调用或时钟采样失败直接打印到 stderr 并终止进程。
 * @note 生命周期调用须串行且消费者已就绪；客户端发布后唤醒主循环、human 和 timer，不重置跨存档累计基准。
 */
extern void dominoTimeModuleThaw(void);

extern bool dominoTimeModuleIsFrozen(void);

/**
 * @brief 底层累计未冻结现实纳秒；存档基准加本次运行时间，冻结期间不变。业务计时须使用 32 位毫秒入口。
 * @param runtime_ns_ptr 写入累计纳秒，不得为空。
 * @note 空指针、采样失败、时钟倒退或纳秒累加溢出直接打印到 stderr 并终止进程。
 */
extern void dominoTimeModuleGetDateTimeNow(uint64_t* runtime_ns_ptr);

/**
 * @brief 读取 32 位累计未冻结现实毫秒；现实纳秒除以 1,000,000，不应用游戏日历倍率。
 * @param runtime_ms_ptr 写入 uint32_t 业务时刻，冻结期间不增长，不得为空。
 * @note 空指针、采样失败、时钟倒退或超出 UINT32_MAX 毫秒直接打印到 stderr 并终止进程。
 * @note 不截断、回绕、饱和或归零；64 位仅用于底层纳秒表示，不能据此扩大业务毫秒类型。
 */
extern void dominoTimeGetRuntimeMs(domino_runtime_ms_t* runtime_ms_ptr);

/** @brief 存档使用的累计现实纳秒基准；64 位纳秒存储不代表业务毫秒可超出 32 位范围。 */
extern uint64_t dominoGetRuntimeDateTimeBase(void);
/** @brief 恢复存档的累计现实纳秒基准；不会因新会话归零，业务入口另行校验 32 位毫秒范围。 */
extern void dominoSetRuntimeDateTimeBase(uint64_t base_ns);
#endif
