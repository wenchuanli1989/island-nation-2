#include <libgen.h>
#include <stdio.h>
#include <string.h>

#include "domino_engine.h"
#include "domino_shared_common.h"

/** @file main.c
 * @brief NPC AI 验证入口：模块内部夹具串行验证结算、预测、决策与候选提交，再以真实线程运行端到端场景。
 * @note 样例存档取自 playground/sandbox/domino，端到端存档写在可执行文件所在构建目录的 runtime/ 下（如 build-debug/runtime/）；
 * 失败检查数非零时返回 1。
 */
int main(int argc, char* argv[]) {
    (void)argc;
    char source_path[1024];
    snprintf(source_path, sizeof(source_path), "%s", __FILE__);
    char sandbox_dir[1024];
    snprintf(sandbox_dir, sizeof(sandbox_dir), "%s/../sandbox/domino", dirname(source_path));
    char executable_path[1024];
    snprintf(executable_path, sizeof(executable_path), "%s", argv[0]);
    char work_dir[1100];
    snprintf(work_dir, sizeof(work_dir), "%s/../runtime", dirname(executable_path));
    if (dominoMkdirRecursive(work_dir) != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(ERR_DIRECTORY_CREATE, "cannot create %s", work_dir);
    }
    char* playground_argv[] = {argv[0], "npc_ai", sandbox_dir, work_dir, NULL};
    const int result = dominoEnginePlayground(4, playground_argv);
    DOMINO_PRINT_MESSAGE("npc_ai playground %s", result == 0 ? "PASSED" : "FAILED");
    return result;
}
