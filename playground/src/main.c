#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

#include "domino_engine.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    // dominoEnginePlayground(argc, argv);
    char cwd[1024] = {0};
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("当前目录路径: %s\n", cwd);
    } else {
        (void)fprintf(stderr, "获取当前目录失败\n");
        return 1;
    }
    char runtime_sandbox_path[1024] = {0};
    (void)snprintf(runtime_sandbox_path, sizeof(runtime_sandbox_path), "%s/runtime/sandbox", cwd);
    printf("沙盒目录路径: %s\n", runtime_sandbox_path);
    DOMINO_CODE init_rc = dominoEngineInit((DominoEngineLaunchConfig){
        .storage_path = runtime_sandbox_path,
        .storage_integrity_verify = 0,
    });
    if (init_rc != CODE_OK) {
        (void)fprintf(stderr, "dominoEngineInit failed: %d\n", init_rc);
        return 1;
    }

    DominoEngineClientData client_data;

    dominoEngineRun(&client_data);
    printf("runtime data: game_date_time_ns=%" PRIu64 "\n", client_data.game_date_time_ns);
    DOMINO_CODE exit_rc = dominoEngineExit();
    if (exit_rc != CODE_OK) {
        (void)fprintf(stderr, "dominoEngineExit failed: %d\n", exit_rc);
        return 1;
    }
    return 0;
}
