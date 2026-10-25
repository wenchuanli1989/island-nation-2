#ifndef DOMINO_CLIENT_HUB_H
#define DOMINO_CLIENT_HUB_H

/** @brief 准备本模块资源，不创建线程；生命周期由调用方串行管理。
 * @note Init 仅接受未初始化，Start 仅接受已初始化，Stop 接受已初始化或已启动，Destroy 仅接受已停止。
 * 重复、未初始化或错误顺序调用直接打印 stderr 并退出；仅 Init 的实例仍须 Stop → Destroy。
 */
void dominoClientHubInit(void);

/** @brief 使用 Init 已准备的资源启动线程；每次 Init 后至多调用一次。 */
void dominoClientHubStart(void);

/** @brief 排空日志并 join 已启动的线程，保留资源；支持仅 Init、未 Start。 */
void dominoClientHubStop(void);

/** @brief Stop 完成后释放资源并复位，不停止或 join 线程；再次使用须重新 Init。 */
void dominoClientHubDestroy(void);

#endif
