#include <stdio.h>
#include <stdlib.h>

#include "domino_shared_error_codes.h"

[[nodiscard]] const char* dominoErrorCodeToString(DOMINO_CODE err) {
    switch (err) {
        case CODE_OK:
            return "成功";
        case ERR_INVALID_PARAM:
            return "无效参数";
        case ERR_NULL_POINTER:
            return "空指针";
        case ERR_OUT_OF_RANGE:
            return "超出范围";
        case ERR_NOT_FOUND:
            return "未找到";
        case ERR_ALREADY_EXISTS:
            return "已存在";
        case ERR_NOT_INITIALIZED:
            return "未初始化";
        case ERR_ALREADY_INITIALIZED:
            return "已初始化";
        case ERR_INVALID_DATA:
            return "无效数据";
        case ERR_INVALID_JSON:
            return "无效JSON";
        case ERR_MEMORY_ALLOC:
            return "内存分配失败";
        case ERR_MEMORY_OVERFLOW:
            return "内存溢出";
        case ERR_BUFFER_TOO_SMALL:
            return "缓冲区太小";
        case ERR_NETWORK:
            return "网络错误";
        case ERR_SOCKET_CREATE:
            return "Socket创建失败";
        case ERR_SOCKET_BIND:
            return "Socket绑定失败";
        case ERR_SOCKET_LISTEN:
            return "Socket监听失败";
        case ERR_SOCKET_ACCEPT:
            return "Socket接受连接失败";
        case ERR_SOCKET_CONNECT:
            return "Socket连接失败";
        case ERR_SOCKET_SEND:
            return "Socket发送失败";
        case ERR_SOCKET_RECV:
            return "Socket接收失败";
        case ERR_CONNECTION_CLOSED:
            return "连接已关闭";
        case ERR_CONNECTION_TIMEOUT:
            return "连接超时";
        case ERR_FILE_OPEN:
            return "文件打开失败";
        case ERR_FILE_READ:
            return "文件读取失败";
        case ERR_FILE_WRITE:
            return "文件写入失败";
        case ERR_FILE_NOT_FOUND:
            return "文件未找到";
        case ERR_FILE_FLUSH:
            return "文件刷盘失败";
        case ERR_FILE_RENAME:
            return "文件重命名失败";
        case ERR_PLAYER_NOT_FOUND:
            return "玩家未找到";
        case ERR_PLAYER_ALREADY_EXISTS:
            return "玩家已存在";
        case ERR_INVALID_PLAYER_NAME:
            return "无效的玩家名称";
        case ERR_GAME_STATE_INVALID:
            return "游戏状态无效";
        case ERR_INVALID_MESSAGE:
            return "无效消息";
        case ERR_SYSTEM:
            return "系统错误";
        case ERR_THREAD_CREATE:
            return "线程创建失败";
        case ERR_MUTEX_INIT:
            return "互斥锁初始化失败";
        case ERR_SEMAPHORE_INIT:
            return "信号量初始化失败";
        case ERR_QUEUE_EMPTY:
            return "队列为空";
        case ERR_QUEUE_FULL:
            return "队列已满";
        case ERR_QUEUE_PRODUCER_STOPPED:
            return "队列生产端已停止";
        case ERR_QUEUE_CONSUMER_STOPPED:
            return "队列消费端已停止";
        case ERR_COMMON:
            return "通用错误";
        case ERR_UNKNOWN:
        default:
            return "未知错误";
    }
}

void dominoAssertErrorCode(DOMINO_CODE err, bool is_abort, const char* file, int line) {
    if (err != CODE_OK) {
        const char* err_str = dominoErrorCodeToString(err);
        (void)fprintf(stderr, "[%s:%d] 错误码 %d (%s)\n", file, line, (int)err, err_str);

        if (is_abort) {
            abort();
        }
    }
}
