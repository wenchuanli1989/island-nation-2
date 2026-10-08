#ifndef DOMINO_ENGINE_TIME_H
#define DOMINO_ENGINE_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"
#include "domino_shared_types.h"

/** @brief storage 加载前清空上一轮时间状态，并暂时保持冻结。 */
extern void dominoTimeModuleInitBefore(void);

/** @brief storage 加载完成后建立新 session 并解除冻结；累计现实时间基准允许为 0。 */
extern void dominoTimeModuleInitAfter(void);

/** @brief 将未冻结的会话时间计入基准并冻结，供存档保存。 */
extern void dominoTimeModuleExit(void);

extern bool dominoTimeModuleIsFrozen(void);

/** @brief 底层累计未冻结现实纳秒；存档基准加本次运行时间，冻结期间不变。业务计时须使用 32 位毫秒入口。 */
extern uint64_t dominoTimeModuleGetDateTimeNow(void);

/**
 * @brief 读取 32 位累计未冻结现实毫秒；现实纳秒除以 1,000,000，不应用游戏日历倍率。
 * @param runtime_ms_ptr 成功时写入 uint32_t 业务时刻，冻结期间不增长；失败时保持原值。
 * @return CODE_OK 成功；空指针返回 ERR_NULL_POINTER；超出 UINT32_MAX 毫秒返回 ERR_OUT_OF_RANGE。
 * @note 不截断、回绕、饱和或归零；64 位仅用于底层纳秒表示，不能据此扩大业务毫秒类型。
 */
extern DOMINO_CODE dominoTimeGetRuntimeMs(domino_runtime_ms_t* runtime_ms_ptr);

/** @brief 存档使用的累计现实纳秒基准；64 位纳秒存储不代表业务毫秒可超出 32 位范围。 */
extern uint64_t dominoGetRuntimeDateTimeBase(void);
/** @brief 恢复存档的累计现实纳秒基准；不会因新会话归零，业务入口另行校验 32 位毫秒范围。 */
extern void dominoSetRuntimeDateTimeBase(uint64_t base_ns);
#endif
