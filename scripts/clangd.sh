#!/usr/bin/env bash
# 为本机 clangd 选择对应平台的编译数据库。
set -euo pipefail

WORKSPACE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build-debug"
if [[ "$(uname -s)" == "Darwin" ]]; then
    BUILD_DIR="build-macos-debug"
fi

exec clangd --compile-commands-dir="$WORKSPACE_DIR/$BUILD_DIR" "$@"
