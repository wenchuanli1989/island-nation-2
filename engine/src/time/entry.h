#ifndef DOMINO_ENGINE_TIME_H
#define DOMINO_ENGINE_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

/** @brief storage 加载前清空上一轮时间状态，并暂时保持冻结。 */
extern void dominoTimeModuleInitBefore(void);

/**
 * @brief storage 加载完成后初始化单调采样起点并保持冻结，等待 Run 就绪后显式解冻；累计基准允许为 0。
 * @return CODE_OK 成功；采样失败返回 ERR_CLOCK_READ，并保持原时间状态。
 */
extern DOMINO_CODE dominoTimeModuleInitAfter(void);

/**
 * @brief 冻结真实累计纳秒基准；已冻结时保持原基准。
 * @note 客户端串行执行生命周期时间写入。普通冻结只发布时钟状态，不通知或等待业务线程；线程在批次边界检查。
 * 调用方通过 dominoTimeGetRuntimeMs() 校验冻结时刻范围；退出时等待业务线程结束后再按此时刻结算。
 * @return CODE_OK 成功；采样失败或倒退返回 ERR_CLOCK_READ，累计纳秒溢出返回 ERR_OUT_OF_RANGE；失败保持原状态。
 */
extern DOMINO_CODE dominoTimeModuleFreeze(void);

/**
 * @brief 从冻结的累计基准重新建立单调起点并解冻，不计入冻结期间的现实经过量。
 * @pre 调用方须保证冻结基准已通过 32 位业务范围校验，且校验后未被修改。
 * @return CODE_OK 成功；未冻结返回 ERR_GAME_STATE_INVALID；采样失败返回 ERR_CLOCK_READ，保持冻结。
 * @note 生命周期调用须串行且消费者已就绪；客户端发布后唤醒主循环、human 和 timer，不重置跨存档累计基准。
 */
extern DOMINO_CODE dominoTimeModuleThaw(void);

extern bool dominoTimeModuleIsFrozen(void);

/**
 * @brief 底层累计未冻结现实纳秒；存档基准加本次运行时间，冻结期间不变。业务计时须使用 32 位毫秒入口。
 * @param runtime_ns_ptr 成功时写入累计纳秒，失败保持原值。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；采样失败或倒退返回 ERR_CLOCK_READ；纳秒累加溢出返回 ERR_OUT_OF_RANGE。
 */
extern DOMINO_CODE dominoTimeModuleGetDateTimeNow(uint64_t* runtime_ns_ptr);

/**
 * @brief 读取 32 位累计未冻结现实毫秒；现实纳秒除以 1,000,000，不应用游戏日历倍率。
 * @param runtime_ms_ptr 成功时写入 uint32_t 业务时刻，冻结期间不增长；失败时保持原值。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；采样失败或倒退返回 ERR_CLOCK_READ；超出 UINT32_MAX 毫秒返回 ERR_OUT_OF_RANGE。
 * @note 不截断、回绕、饱和或归零；64 位仅用于底层纳秒表示，不能据此扩大业务毫秒类型。
 */
extern DOMINO_CODE dominoTimeGetRuntimeMs(domino_runtime_ms_t* runtime_ms_ptr);

/** @brief 存档使用的累计现实纳秒基准；64 位纳秒存储不代表业务毫秒可超出 32 位范围。 */
extern uint64_t dominoGetRuntimeDateTimeBase(void);
/** @brief 恢复存档的累计现实纳秒基准；不会因新会话归零，业务入口另行校验 32 位毫秒范围。 */
extern void dominoSetRuntimeDateTimeBase(uint64_t base_ns);
#endif
