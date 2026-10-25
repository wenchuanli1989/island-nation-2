#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#include "domino_engine.h"
#include "domino_shared_common.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    // dominoEnginePlayground(argc, argv);
    char cwd[1024] = {0};
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        DOMINO_PRINT_MESSAGE("当前目录路径: %s", cwd);
    } else {
        DOMINO_EXIT_WITH_ERROR(ERR_SYSTEM, "获取当前目录失败");
    }
    char runtime_sandbox_path[1024] = {0};
    int path_len = snprintf(runtime_sandbox_path, sizeof(runtime_sandbox_path), "%s/runtime/sandbox", cwd);
    if (path_len < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "沙盒目录路径格式化失败");
    }
    if ((size_t)path_len >= sizeof(runtime_sandbox_path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "沙盒目录路径过长");
    }
    DOMINO_PRINT_MESSAGE("沙盒目录路径: %s", runtime_sandbox_path);
    dominoEngineInit((DominoEngineLaunchConfig){
        .storage_path = runtime_sandbox_path,
        .storage_integrity_verify = 0,
    });

    DominoEngineClientData client_data;

    dominoEngineStart(&client_data);
    dominoEngineStop();
    dominoEngineDestroy();
    return 0;
}
