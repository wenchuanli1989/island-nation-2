#!/usr/bin/env bash
# 标准 Ubuntu 26.04／24.04 的项目安装脚本：sudo bash scripts/setup-dev.sh
# 其他环境由各自的脚本调用本脚本，只处理自身差异（setup-docker.sh、setup-claude-cloud.sh）。
set -euo pipefail

[[ $# -eq 0 ]] || { echo "用法: sudo bash $0（LLVM_VERSION 默认 22）" >&2; exit 1; }
# shellcheck source=/dev/null
source /etc/os-release
[[ "${ID:-}:${VERSION_ID:-}" =~ ^ubuntu:2[46]\.04$ ]] || { echo "错误: 仅支持 Ubuntu 26.04 和 24.04" >&2; exit 1; }
[[ $EUID -eq 0 ]] || { echo "请使用 sudo bash $0 运行" >&2; exit 1; }
LLVM_VERSION="${LLVM_VERSION:-22}"
export DEBIAN_FRONTEND=noninteractive LC_ALL=C
umask 022

# 参数：名称、签名密钥地址、软件源地址与发行代号。
add_source() {
    curl -fsSL "$2" -o "/etc/apt/keyrings/$1.asc"
    echo "deb [signed-by=/etc/apt/keyrings/$1.asc] $3 main" > "/etc/apt/sources.list.d/$1.list"
}

apt-get update
apt-get install -y --no-install-recommends ca-certificates curl
add_source kitware https://apt.kitware.com/keys/kitware-archive-latest.asc "https://apt.kitware.com/ubuntu/ $VERSION_CODENAME"
add_source llvm https://apt.llvm.org/llvm-snapshot.gpg.key \
    "https://apt.llvm.org/$VERSION_CODENAME/ llvm-toolchain-$VERSION_CODENAME-$LLVM_VERSION"

apt-get update
apt-get install -y --no-install-recommends build-essential cmake git pahole \
    "clang-$LLVM_VERSION" "clang-tools-$LLVM_VERSION" "clangd-$LLVM_VERSION" "clang-format-$LLVM_VERSION" \
    "clang-tidy-$LLVM_VERSION" "lldb-$LLVM_VERSION" "llvm-$LLVM_VERSION" "llvm-$LLVM_VERSION-linker-tools" \
    "libclang-rt-$LLVM_VERSION-dev"

# llvm-ar／llvm-ranlib 须与 clang 同版本：ThinLTO 的 .o 是 bitcode，旧版读不了新版产物。
for tool in clang clang++ clangd clang-format clang-tidy lldb scan-build llvm-symbolizer llvm-ar llvm-ranlib; do
    update-alternatives --install "/usr/bin/$tool" "$tool" "/usr/bin/$tool-$LLVM_VERSION" "$LLVM_VERSION"
    update-alternatives --set "$tool" "/usr/bin/$tool-$LLVM_VERSION"
done

echo "开发环境安装完成。"

# Ubuntu 24.04 的系统 glibc 低于 2.43，另装独立 glibc。
if [[ "$VERSION_ID" == 24.04 ]]; then
    export DOMINO_GLIBC_ROOT="${DOMINO_GLIBC_ROOT:-/opt/glibc-2.43}"
    bash "$(dirname "${BASH_SOURCE[0]}")/setup-glibc.sh"
    echo "构建前须设置环境变量 DOMINO_GLIBC_ROOT=$DOMINO_GLIBC_ROOT"
fi
