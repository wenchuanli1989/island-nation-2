#ifndef DOMINO_SHARED_ERROR_CODES_H
#define DOMINO_SHARED_ERROR_CODES_H

/** @brief 统一错误码：0 成功，负值失败。 */
typedef enum {
    CODE_OK = 0,

    // 通用错误 (-1 到 -99)
    ERR_INVALID_PARAM = -1,
    ERR_NULL_POINTER = -2,
    ERR_OUT_OF_RANGE = -3,
    ERR_NOT_FOUND = -4,
    ERR_ALREADY_EXISTS = -5,
    ERR_NOT_INITIALIZED = -6,
    ERR_ALREADY_INITIALIZED = -7,
    ERR_INVALID_DATA = -8,
    ERR_INVALID_JSON = -9,

    // 内存错误 (-100 到 -199)
    ERR_MEMORY_ALLOC = -100,
    ERR_MEMORY_OVERFLOW = -101,
    ERR_BUFFER_TOO_SMALL = -102,

    // 网络错误 (-200 到 -299)
    ERR_NETWORK = -200,
    ERR_SOCKET_CREATE = -201,
    ERR_SOCKET_BIND = -202,
    ERR_SOCKET_LISTEN = -203,
    ERR_SOCKET_ACCEPT = -204,
    ERR_SOCKET_CONNECT = -205,
    ERR_SOCKET_SEND = -206,
    ERR_SOCKET_RECV = -207,
    ERR_CONNECTION_CLOSED = -208,
    ERR_CONNECTION_TIMEOUT = -209,

    // 文件/IO错误 (-300 到 -399)
    ERR_FILE_OPEN = -300,
    ERR_FILE_READ = -301,
    ERR_FILE_WRITE = -302,
    ERR_FILE_NOT_FOUND = -303,
    ERR_FILE_FLUSH = -304,
    ERR_FILE_RENAME = -305,

    // 游戏逻辑错误 (-400 到 -499)
    ERR_PLAYER_NOT_FOUND = -400,
    ERR_PLAYER_ALREADY_EXISTS = -401,
    ERR_INVALID_PLAYER_NAME = -402,
    ERR_GAME_STATE_INVALID = -403,
    ERR_INVALID_MESSAGE = -404,

    // 系统错误 (-500 到 -599)
    ERR_SYSTEM = -500,
    ERR_THREAD_CREATE = -501,
    ERR_MUTEX_INIT = -502,
    ERR_SEMAPHORE_INIT = -503,

    // 队列错误 (-600 到 -699)
    ERR_QUEUE_EMPTY = -600,
    ERR_QUEUE_FULL = -601,
    ERR_QUEUE_PRODUCER_STOPPED = -602,  ///< 队列生产端已停止。
    ERR_QUEUE_CONSUMER_STOPPED = -603,  ///< 队列消费端已停止。

    ERR_COMMON = -998,
    ERR_UNKNOWN = -999
} DOMINO_CODE;

[[nodiscard]] const char* dominoErrorCodeToString(DOMINO_CODE err);

/** @brief 非 CODE_OK 时打印错误；is_abort 为 true 时终止进程。 */
void dominoAssertErrorCode(DOMINO_CODE err, bool is_abort, const char* file, int line);

#endif  // DOMINO_SHARED_ERROR_CODES_H
