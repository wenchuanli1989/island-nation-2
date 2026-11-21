#ifndef DOMINO_SHARED_ERROR_CODES_H
#define DOMINO_SHARED_ERROR_CODES_H

/** @brief 统一结果码：0 成功，负值表示错误或接口约定的控制结果。
 * @note 保留最初的失败原因，调用方不把下层错误统一改写为参数或系统错误；EMPTY、查询未命中等是否算故障由接口约定。
 */
typedef enum {
    CODE_OK = 0,

    // 通用错误 (-1 到 -99)
    ERR_INVALID_PARAM = -1,
    ERR_NULL_POINTER = -2,
    ERR_OUT_OF_RANGE = -3,  ///< 数值或业务时间超出表示范围；内存字节数溢出用 ERR_MEMORY_OVERFLOW。
    ERR_NOT_FOUND = -4,
    ERR_ALREADY_EXISTS = -5,
    ERR_NOT_INITIALIZED = -6,
    ERR_ALREADY_INITIALIZED = -7,
    ERR_INVALID_DATA = -8,          ///< 数据结构、字段约束或内部关联不一致。
    ERR_INVALID_JSON = -9,          ///< JSON 语法错误。
    ERR_UNSUPPORTED_VERSION = -10,  ///< 格式有效但版本不受支持。
    ERR_CAPACITY_EXCEEDED = -11,    ///< 实体、容器或业务配额达到上限。
    ERR_ID_EXHAUSTED = -12,
    ERR_CHECKSUM_MISMATCH = -13,
    ERR_STRING_FORMAT = -14,  ///< 字符串格式化失败，不含缓冲区不足。

    // 内存错误 (-100 到 -199)
    ERR_MEMORY_ALLOC = -100,
    ERR_MEMORY_OVERFLOW = -101,  ///< 分配或寻址所需字节数溢出。
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
    ERR_FILE_STAT = -306,
    ERR_DIRECTORY_CREATE = -307,
    ERR_DIRECTORY_READ = -308,
    ERR_FILE_REMOVE = -309,  ///< 删除文件或目录失败。
    ERR_FILE_CLOSE = -310,
    ERR_DIRECTORY_OPEN = -311,
    ERR_DIRECTORY_CLOSE = -312,
    ERR_PATH_TYPE = -313,  ///< 路径存在，但文件/目录类型与操作要求不符。

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
    ERR_THREAD_JOIN = -504,
    ERR_MUTEX_LOCK = -505,
    ERR_MUTEX_UNLOCK = -506,
    ERR_CONDITION_INIT = -507,
    ERR_CONDITION_WAIT = -508,  ///< 条件等待失败；正常超时不属于此错误。
    ERR_CONDITION_SIGNAL = -509,
    ERR_CONDITION_BROADCAST = -510,
    ERR_CLOCK_READ = -511,  ///< 时钟采样失败或单调时钟倒退。
    ERR_THREAD_SLEEP = -512,

    // 队列错误 (-600 到 -699)
    ERR_QUEUE_EMPTY = -600,
    ERR_QUEUE_FULL = -601,
    ERR_QUEUE_FLUSHING = -602,  ///< 队列处于排空状态；消费返回此结果时表示已取空。

    ERR_COMMON = -998,
    ERR_UNKNOWN = -999
} DOMINO_CODE;

[[nodiscard]] const char* dominoErrorCodeToString(DOMINO_CODE err);

#endif  // DOMINO_SHARED_ERROR_CODES_H
