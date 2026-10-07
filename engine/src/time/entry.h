#ifndef DOMINO_ENGINE_TIME_H
#define DOMINO_ENGINE_TIME_H

#include <stdbool.h>
#include <stdint.h>

/** @brief storage 加载前清空上一轮时间状态，并暂时保持冻结。 */
extern void dominoTimeModuleInitBefore(void);

/** @brief storage 加载完成后建立新 session 并解除冻结；累计现实时间基准允许为 0。 */
extern void dominoTimeModuleInitAfter(void);

/** @brief 将未冻结的会话时间计入基准并冻结，供存档保存。 */
extern void dominoTimeModuleExit(void);

extern bool dominoTimeModuleIsFrozen(void);

/** @brief 累计未冻结的现实纳秒；存档基准加上本次运行时间，冻结期间保持不变。 */
extern uint64_t dominoTimeModuleGetDateTimeNow(void);

/** @brief 累计未冻结的现实毫秒；由累计现实纳秒除以 1,000,000，冻结期间保持不变。 */
extern uint64_t dominoTimeGetRuntimeMs(void);

extern uint64_t dominoGetRuntimeDateTimeBase(void);
extern void dominoSetRuntimeDateTimeBase(uint64_t base_ns);
#endif
