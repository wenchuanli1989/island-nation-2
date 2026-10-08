#ifndef DOMINO_ENGINE_H
#define DOMINO_ENGINE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "domino_shared_error_codes.h"

typedef enum DominoEngineMsgType {
    DOMINO_ENGINE_MSG_NONE = 0,
} DominoEngineMsgType;

typedef struct DominoEngineMessage {
    uint64_t id;  ///< 入队成功时由所属队列生成并回写的消息 ID。
    uint64_t u64[4];
    uint32_t u32[5];
    uint32_t type;
} DominoEngineMessage;

static_assert(sizeof(DominoEngineMessage) == 64U, "engine message must be 64 bytes");

typedef struct DominoEngineLaunchConfig {
    /** @brief 必填存档目录；初始化时复制路径，缺少 meta.json 时创建空世界。 */
    const char* storage_path;

    /**
     * @brief 下次保存是否记录分片和模块 meta 的 CRC32C/size；关闭时摘要写 0。
     * 加载始终使用存档自身声明的校验策略。
     */
    bool storage_integrity_verify;
} DominoEngineLaunchConfig;

/**
 * @brief 初始化引擎并加载存档；成功调用的线程登记为客户端线程。
 * @return CODE_OK 成功；重复初始化返回 ERR_ALREADY_INITIALIZED；其余失败返回负错误码。
 * @note 失败时逆序释放已初始化模块，恢复为可重试的未初始化状态。
 */
extern DOMINO_CODE dominoEngineInit(DominoEngineLaunchConfig launch_config);

typedef struct DominoEngineClientData {
    uint64_t game_date_time_ns;  ///< Run 写入的累计未冻结现实纳秒基准，不会持续更新；业务计时使用 32 位现实毫秒。
    /* 以下日历字段尚未由引擎计算或写入。 */
    uint32_t game_date_time_sec;
    uint32_t game_time;
    uint32_t game_year;
    uint32_t game_month;

    uint8_t reserved[4072];  ///< 预留字段，填充至 4096 字节。
} DominoEngineClientData;
static_assert(sizeof(DominoEngineClientData) == 4096U, "client data must be 4096 bytes");

/**
 * @brief 从已初始化状态启动主循环，主循环线程创建成功即返回，不等待内部规划线程就绪。
 * @param client_data_out_ptr 可为 nullptr；非空时仅写入 game_date_time_ns。
 * @return CODE_OK 成功；状态、队列初始化或线程创建失败时返回负错误码。
 */
extern DOMINO_CODE dominoEngineRun(DominoEngineClientData* client_data_out_ptr);

/**
 * @brief 停止线程、结算时间、整理导航并保存存档，成功后释放世界并恢复未初始化状态。
 * 保存失败时保留内存世界，可在 STOPPING 状态重试；已完成的停止和时间结算不重复执行，
 * 每次保存仍检查导航是否需要压实。
 * @note 调用方须先停止其他 API 调用并排除并发；进入 STOPPING 后只允许重试 Exit。
 */
extern DOMINO_CODE dominoEngineExit(void);

/**
 * @brief 结算当前会话并冻结时间；工作线程在下次检查冻结标志时暂停，不等待其就绪。
 * @return 非客户端线程、生命周期不允许或已冻结时返回 ERR_GAME_STATE_INVALID。
 */
extern DOMINO_CODE dominoEngineFreezeTime(void);

/**
 * @brief 解冻时间并建立新的单调时钟起点。
 * @return 非客户端线程、生命周期不允许或未冻结时返回 ERR_GAME_STATE_INVALID。
 */
extern DOMINO_CODE dominoEngineThawTime(void);

/**
 * @brief 客户端向引擎主循环发送消息。
 * @param message_ptr 可写消息；成功后 id 为本次入队生成的 ID。
 * @param blocking 达到最大容量时，true 等待空位，false 返回 `ERR_QUEUE_FULL`；两种模式均需获取队列 mutex。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；引擎队列未初始化返回 `ERR_NOT_INITIALIZED`；
 *         生产端已关停返回 `ERR_QUEUE_PRODUCER_STOPPED`；消费端已关停且达到最大容量返回 `ERR_QUEUE_CONSUMER_STOPPED`；
 *         扩容分配失败返回 `ERR_MEMORY_ALLOC`；消息 ID 耗尽返回 `ERR_OUT_OF_RANGE`。
 */
extern DOMINO_CODE dominoEnginePushMsg(DominoEngineMessage* message_ptr, bool blocking);

/**
 * @brief 客户端接收引擎消息。
 * @param blocking 无消息时，true 等待消息，false 返回 `ERR_QUEUE_EMPTY`；两种模式均需获取队列 mutex。
 * @return `CODE_OK` 成功；空指针返回 `ERR_NULL_POINTER`；引擎队列未初始化返回 `ERR_NOT_INITIALIZED`；
 *         消费端已关停返回 `ERR_QUEUE_CONSUMER_STOPPED`；生产端已关停且队列已空返回 `ERR_QUEUE_PRODUCER_STOPPED`。
 */
extern DOMINO_CODE dominoEnginePopMsg(DominoEngineMessage* message_out_ptr, bool blocking);

#ifdef DOMINO_PLAYGROUND_MODE
extern int dominoEnginePlayground(int argc, char* argv[]);
#endif
#endif
