#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "domino_engine.h"
#include "domino_shared_common.h"

/** @file main.c
 * @brief 默认 Playground：完成一次引擎生命周期后退出，再次运行会加载上次的存档。
 * @note 存档按可执行文件位置写在其构建目录的 runtime/sandbox/（如 build-debug/runtime/sandbox/），与工作目录无关，
 * 从仓库根目录或 VS Code 任务运行时也不会写进源码树。
 */
int main(int argc, char* argv[]) {
    // dominoEnginePlayground(argc, argv);
    if (argc < 1 || argv[0] == NULL) {
        DOMINO_EXIT_WITH_ERROR(ERR_INVALID_PARAM, "缺少可执行文件路径");
    }
    char* executable_path = realpath(argv[0], NULL);
    if (executable_path == NULL) {
        int realpath_errno = errno;
        DOMINO_EXIT_WITH_ERROR(ERR_SYSTEM, "解析可执行文件路径失败 (path=%s, errno=%d)", argv[0], realpath_errno);
    }
    // 可执行文件位于 build-<preset>/bin/，向上两级即构建目录。
    char bin_dir[1024];
    char build_dir[1024];
    char name_buf[256];
    DOMINO_CODE split_code = dominoPathSplitParentBase(executable_path, bin_dir, sizeof(bin_dir), name_buf, sizeof(name_buf));
    if (split_code == CODE_OK) {
        split_code = dominoPathSplitParentBase(bin_dir, build_dir, sizeof(build_dir), name_buf, sizeof(name_buf));
    }
    if (split_code != CODE_OK) {
        DOMINO_EXIT_WITH_ERROR(split_code, "定位构建目录失败: %s", executable_path);
    }
    free(executable_path);

    char runtime_sandbox_path[1024];
    int path_len = snprintf(runtime_sandbox_path, sizeof(runtime_sandbox_path), "%s/runtime/sandbox", build_dir);
    if (path_len < 0) {
        DOMINO_EXIT_WITH_ERROR(ERR_STRING_FORMAT, "沙盒目录路径格式化失败");
    }
    if ((size_t)path_len >= sizeof(runtime_sandbox_path)) {
        DOMINO_EXIT_WITH_ERROR(ERR_BUFFER_TOO_SMALL, "沙盒目录路径过长");
    }
    DOMINO_PRINT_MESSAGE("沙盒目录路径: %s", runtime_sandbox_path);
    dominoEngineInit((DominoEngineLaunchConfig){
        .storage_path = runtime_sandbox_path,
        .storage_integrity_check = 0,
    });

    DominoEngineClientData client_data;

    dominoEngineStart(&client_data);
    dominoEngineStop();
    dominoEngineDestroy();
    return 0;
}
