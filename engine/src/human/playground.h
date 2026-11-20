#ifndef DOMINO_ENGINE_HUMAN_PLAYGROUND_H
#define DOMINO_ENGINE_HUMAN_PLAYGROUND_H

/** @file playground.h
 * @brief 仅 DOMINO_PLAYGROUND_MODE：NPC AI 验证入口。模块内部夹具串行验证结算、预测、决策、行为依赖、应激和候选提交，
 * 以及测试验收专用人物输入的校验与写入；端到端场景在真实线程下验证自主睡眠、客户端收发通道骨架、冻结、条件变化、
 * 受控规划下的应激让位与主辅行为，以及停止、保存和读档后的延续。运行中的观察由测试线程持 human 读锁直接读取，不经客户端通道。
 */

/** @brief 运行全部 NPC AI 验证；sandbox_dir 为样例存档目录。返回失败检查数（0 表示全部通过）。 */
int dominoHumanPlayground(const char* sandbox_dir, const char* work_dir);

#endif
