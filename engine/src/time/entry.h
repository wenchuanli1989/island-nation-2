#ifndef DOMINO_ENGINE_TIME_H
#define DOMINO_ENGINE_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_types.h"

/* 静态初值和 Destroy 保证空的冻结快照；storage 加载累计基准，Start 解冻时建立单调采样起点。 */

/**
 * @brief 准备冻结等待的互斥锁和条件变量，不重置时间快照、不采样时钟。
 * @pre 由引擎在创建业务线程前调用；首次静态初值或上轮 Destroy 已提供空的冻结状态。
 */
extern void dominoTimeModuleInit(void);

/**
 * @brief 释放冻结等待资源，清零累计基准、采样起点、快照版本和等待计数，恢复冻结轮次 1。
 * @pre 所有业务线程已 join、世界已保存，且无并发 time API 调用。
 * @note 由引擎保证仅在客户端的 DESTROYING 阶段调用，本接口不重复校验，不隐式唤醒或等待线程。
 * @note 仅清空本实例的内存状态；不修改已保存的累计时间，下次 Init 仍从存档恢复。
 */
extern void dominoTimeModuleDestroy(void);

/**
 * @brief 冻结真实累计纳秒基准，推进冻结轮次并清零本轮等待计数；已冻结时均保持不变。
 * @note 客户端串行执行生命周期时间写入。普通冻结只发布时钟状态，不通知或等待业务线程；线程在批次边界检查。
 * 调用方通过 dominoTimeGetRuntimeMs() 校验冻结时刻范围；退出时等待业务线程结束后再按此时刻结算。
 * @note 冻结轮次耗尽（UINT64_MAX）、时钟采样失败、倒退或累计纳秒溢出直接打印到 stderr 并终止进程。
 */
extern void dominoTimeModuleFreeze(void);

/**
 * @brief 从冻结的累计基准重新建立单调起点并解冻，再广播 time 等待者，不计入冻结期间的现实经过量。
 * @pre 调用方须保证冻结基准已通过 32 位业务范围校验，且校验后未被修改。
 * @note 未冻结时调用或时钟采样失败直接打印到 stderr 并终止进程。
 * @note 生命周期调用须串行且消费者已就绪；客户端发布后唤醒主循环、human 和 game_timer，不重置跨存档累计基准。
 */
extern void dominoTimeModuleThaw(void);

extern bool dominoTimeModuleIsFrozen(void);

/**
 * @brief 读取一致的 64 位冻结轮次快照，运行时返回 0，冻结时返回非零轮次。
 * @note 轮次仅标识本实例的一轮冻结，不是业务时间；重复冻结保持轮次，Destroy 后恢复为 1。
 */
extern uint64_t dominoTimeModuleGetFreezeEpoch(void);

/**
 * @brief 在同一轮冻结期间等待，解冻、轮次失效或引擎停止时返回。
 * @param observed_epoch 检查冻结时取得的轮次；0 或过期轮次立即返回。
 * @pre time 已 Init 且尚未 Destroy；调用方不得持有其他线程继续执行所需的业务锁。
 * @note 在等待锁内复查冻结和引擎停止状态；返回后必须回到批次入口重新检查退出和冻结，不能直接执行业务。
 */
extern void dominoTimeModuleWait(uint64_t observed_epoch);

/**
 * @brief 通知 time 等待者重新检查状态，不改变冻结状态、时间基准或冻结轮次。
 * @note Stop 先发布引擎停止状态再调用；冻结且未停止时，被唤醒的同轮等待者继续等待。
 */
extern void dominoTimeModuleWakeAll(void);

/**
 * @brief 读取本轮冻结中已登记、尚未从条件等待返回的线程数，仅用于显示。
 * @note 不包含队列闲等、GameTimer 或 logger 线程；已收到广播但尚未重新持锁的线程仍可能计入。
 * 新一轮冻结清零，旧轮返回不扣新轮计数；计数不参与冻结、解冻、退出或资源释放判断。
 * @pre time 已 Init 且尚未 Destroy。
 */
extern uint32_t dominoTimeModuleGetFreezeWaiterCount(void);

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
