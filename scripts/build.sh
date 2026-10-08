#!/usr/bin/env bash
# 统一的构建和工具脚本
# 用法: build.sh <command> [options...]

set -euo pipefail

SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
WORKSPACE_DIR="$(realpath "$SCRIPT_DIR/..")"

# 获取构建并行数
get_build_jobs() {
    echo "${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc 2>/dev/null || echo 1)}"
}

# 显示使用说明
show_usage() {
    cat << EOF
用法: $0 <command> [options...]

可用命令:
  debug  [target]        构建项目（调试模式）
  macos-debug           在当前 Intel Mac 上构建默认引擎 Playground（Debug）
  release [target]        构建项目（发布模式）
  release-native [target]        构建项目（发布模式，本机极致优化）
  sanitize [target]              构建项目（Sanitizer模式）
  tsan [target]                  构建项目（ThreadSanitizer模式）
  profile [target]               构建项目（性能分析模式）
  clean                    清理构建目录
  format [target-dir]              运行 clang-format 格式化代码（可选：指定目录）
  clang-tidy [--strict] [target-dir]  运行 clang-tidy 检查代码
  scan-build [target]           使用 Clang Static Analyzer 分析代码
  playground-build <source-dir> [debug|release]  为指定目录构建Playground（需包含 main.c）
  playground-run <c-file> [debug|release] [program-args...]  构建并运行Playground（c-file 需位于 playground/ 下）

clang-tidy 选项:
  --strict                   严格模式：发现错误时退出
  <target>                  指定项目名称（可选），如: shared, engine, client-hub 等
                             如果不指定，将对所有项目执行检查

示例:
  $0 debug
  $0 format
  $0 format shared
  $0 clang-tidy
  $0 clang-tidy --strict
  $0 clang-tidy shared
  $0 clang-tidy --strict engine
  $0 clang-tidy engine-remote/local --strict
  $0 scan-build             使用 Clang Static Analyzer 分析所有项目
  $0 scan-build shared      分析指定项目
  $0 playground-run playground/算法/路径规划/弗洛伊德/main.c
EOF
}

# Playground 相关辅助函数
playground_build_for_directory() {
    local candidate
    local build_type="${2:-debug}"
    local cmake_preset
    local build_dir

    case "$build_type" in
        debug)
            cmake_preset="debug"
            build_dir="build-debug"
            ;;
        release)
            cmake_preset="release"
            build_dir="build-release"
            ;;
        *)
            echo "错误: 不支持的构建类型 '$build_type'，仅支持 debug 或 release" >&2
            exit 1
            ;;
    esac

    # 如果输入是文件，取目录；如果是目录，直接使用
    [[ "$1" = /* ]] && candidate="$1" || candidate="$WORKSPACE_DIR/$1"
    local PLAYGROUND_SOURCE_DIR="$(realpath "$candidate")"
    [[ -f "$PLAYGROUND_SOURCE_DIR" ]] && PLAYGROUND_SOURCE_DIR="$(dirname "$PLAYGROUND_SOURCE_DIR")"
    if [[ ! "$PLAYGROUND_SOURCE_DIR" == "$WORKSPACE_DIR"/playground/* ]]; then
        echo "错误: 仅支持构建 workspace/playground 目录下的源码" >&2
        exit 1
    fi
    echo "配置Playground源码目录: $PLAYGROUND_SOURCE_DIR (构建类型: $build_type)"
    cmake_build "$cmake_preset" "$build_dir" "playground" \
        -DPLAYGROUND_SOURCE_DIR="$PLAYGROUND_SOURCE_DIR" \
        -DDOMINO_PLAYGROUND_MODE=ON
}

# 统一的构建函数
# 用法: cmake_build <preset> <build_dir> <target_name> [cmake -D 参数...]
cmake_build() {
    local preset="$1"
    local build_dir="$2"
    local target_name="$3"
    shift 3

    echo "构建目标: $target_name"
    
    cmake --preset "$preset" "$@"
    local jobs="$(get_build_jobs)"

    local build_cmd=(
        cmake --build "$WORKSPACE_DIR/$build_dir"
        -j "$jobs"
    )
    if [[ -n "$target_name" ]]; then
        build_cmd+=(--target "domino-${target_name}")
    fi
    
    "${build_cmd[@]}"
}


# 运行 clang-tidy
run_clang_tidy() {
    # 解析参数：--strict 在前，项目名称在后
    local strict_mode=false
    [[ "${1:-}" == "--strict" ]] && { strict_mode=true; shift; }
    
    # 确定要检查的目录（默认为工作区根目录）
    local target_dir="${1:-$WORKSPACE_DIR}"
    [[ ! -d "$target_dir" ]] && { echo "错误: '$target_dir' 不是一个目录" >&2; exit 1; }
    
    cmake --preset debug
    
    if ! find "$target_dir" \( -name '*.c' -o -name '*.h' \) -not -path '*/build-*/*' \
        -exec clang-tidy -p "$WORKSPACE_DIR/build-debug" {} +; then
        if [[ "$strict_mode" == true ]]; then
            echo "错误: clang-tidy 发现错误" >&2
            exit 1
        fi
    fi
}

# 运行 scan-build
run_scan_build() {
    local project_name="$1"
    cmake --preset debug

    local output_dir="$WORKSPACE_DIR/scan-build-reports"
    mkdir -p "$output_dir"
    local jobs="$(get_build_jobs)"

    local analyzer="$(command -v clang 2>/dev/null || true)"
    [[ -z "$analyzer" ]] && { echo "错误: 未找到 clang，scan-build 无法运行。"; exit 1; }

    local build_cmd=(
        cmake --build "$WORKSPACE_DIR/build-debug"
        -j "$jobs"
    )
    if [[ -n "$project_name" ]]; then
        build_cmd+=(--target "domino-${project_name}")
    fi

    scan-build -o "$output_dir" --use-analyzer "$analyzer" "${build_cmd[@]}" || true

    # 查找并显示报告（无 bug 时 scan-build 会删除报告目录，故可能找不到）
    local latest_report="$(find "$output_dir" -mindepth 1 -maxdepth 1 -type d -name "20*" 2>/dev/null | sort | tail -1)"
    if [[ -n "$latest_report" ]]; then
        echo ""
        echo "分析完成！报告位置: $latest_report/index.html"
    else
        echo ""
        echo "分析完成，未发现 bug（scan-build 未保留报告目录）。"
    fi
}


# 主函数
main() {
    if [[ $# -eq 0 ]]; then
        show_usage
        exit 1
    fi

    COMMAND="$1"
    shift

    case "$COMMAND" in
        macos-debug)
            cmake_build "debug" "build-macos-debug" "playground" \
                -S "$WORKSPACE_DIR" -B "$WORKSPACE_DIR/build-macos-debug" \
                -DPLAYGROUND_SOURCE_DIR="$WORKSPACE_DIR/playground/src" \
                -DDOMINO_PLAYGROUND_MODE=ON -DBUILD_TESTING=OFF
            ;;

        debug)
            echo "构建项目（调试模式）"
            cmake_build "debug" "build-debug" "${1:-}"
            echo "构建完成"
            ;;

        release)
            echo "构建项目（发布模式）"
            cmake_build "release" "build-release" "${1:-}"
            echo "构建完成"
            ;;

        release-native)
            echo "构建项目（发布模式，本机极致优化）"
            cmake_build "release-native" "build-release-native" "${1:-}"
            echo "构建完成"
            ;;

        sanitize)
            echo "构建项目（Sanitizer模式，内存检查）"
            cmake_build "sanitize" "build-sanitize" "${1:-}"
            echo "构建完成"
            ;;

        tsan)
            echo "构建项目（ThreadSanitizer模式）"
            cmake_build "tsan" "build-tsan" "${1:-}"
            echo "构建完成"
            ;;

        profile)
            echo "构建项目（性能分析模式）"
            cmake_build "profile" "build-profile" "${1:-}"
            echo "构建完成"
            ;;


        playground-build)
            echo "构建Playground（需包含 main.c）"
            [[ -z "${1:-}" ]] && { echo "错误: 未指定源码目录或文件" >&2; exit 1; }
            playground_build_for_directory "$1" "${2:-debug}"
            echo "构建完成"
            ;;

        playground-run)
            echo "构建并运行Playground（c-file 需位于 playground/ 下）"
            [[ -z "${1:-}" ]] && { echo "错误: 未指定源码文件" >&2; exit 1; }

            local source_file="$1"
            local build_type="debug"

            # 可选第二个参数指定构建类型（debug 或 release），默认为 debug
            if [[ "${2:-}" == "debug" || "${2:-}" == "release" ]]; then
                build_type="$2"
                shift 2
            else
                shift 1
            fi

            playground_build_for_directory "$source_file" "$build_type"

            local build_dir="build-debug"
            [[ "$build_type" == "release" ]] && build_dir="build-release"

            local program="$WORKSPACE_DIR/$build_dir/bin/domino-playground"
            [[ ! -f "$program" ]] && { echo "错误: 程序不存在: $program" >&2; exit 1; }
            "$program" "$@"

            local exit_code=$?
            if [[ $exit_code -eq 0 ]]; then
                echo "运行完成"
            else
                echo "程序退出，返回码: $exit_code" >&2
                exit $exit_code
            fi
            ;;

        clean)
            echo "清理所有构建目录..."
            rm -rf "$WORKSPACE_DIR"/build-*
            echo "清理完成"
            ;;

        format)
            echo "格式化代码..."
            # 确定要格式化的目录（默认为工作区根目录）
            local target_dir="${1:-$WORKSPACE_DIR}"
            [[ ! -d "$target_dir" ]] && { echo "错误: '$target_dir' 不是一个目录" >&2; exit 1; }

            local clang_format="clang-format"
            if [[ "$(uname -s)" == "Darwin" ]]; then
                clang_format="$(xcrun --find clang-format)"
            fi
            
            # 查找所有 C 和 H 文件，排除构建目录
            find "$target_dir" \( -name '*.c' -o -name '*.h' \) -not -path '*/build-*/*' -exec "$clang_format" --style=file -i {} +
            
            echo "格式化完成"
            ;;

        clang-tidy)
            echo "运行 clang-tidy 检查代码..."
            run_clang_tidy "$@"
            echo "clang-tidy 检查完成"
            ;;

        scan-build)
            echo "使用 Clang Static Analyzer 分析代码..."
            run_scan_build "${1:-}"
            echo "Clang Static Analyzer 分析完成"
            ;;

        *)
            echo "错误: 未知命令 '$COMMAND'"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

main "$@"

