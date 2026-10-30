#!/usr/bin/env bash
# Codex Cloud Debian 13 (amd64)：独立安装入口，不调用远程开发环境脚本。
set -euo pipefail

[[ $# -eq 0 ]] || { echo "用法: DOMINO_GLIBC_ROOT=/opt/domino/glibc-2.43 bash $0" >&2; exit 1; }
# shellcheck source=/dev/null
source /etc/os-release
[[ "${ID:-}:${VERSION_ID:-}:$(uname -m)" == debian:13:x86_64 ]] || {
    echo "错误: 仅支持 Debian 13 (trixie) Linux x86_64" >&2
    exit 1
}
# 必须在 Cloud 环境设置中配置，让安装阶段和后续任务使用同一个 sysroot。
: "${DOMINO_GLIBC_ROOT:?请在 Cloud 环境变量中设置 DOMINO_GLIBC_ROOT=/opt/domino/glibc-2.43}"
[[ "$DOMINO_GLIBC_ROOT" == /* && "$(realpath -m "$DOMINO_GLIBC_ROOT")" != / ]] || {
    echo "错误: DOMINO_GLIBC_ROOT 必须是非根目录的绝对路径" >&2
    exit 1
}
if [[ $EUID -ne 0 ]]; then
    exec sudo -nE bash "$0" "$@"
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LLVM_VERSION=22
export DEBIAN_FRONTEND=noninteractive LC_ALL=C
umask 022

apt-get update
apt-get install -y --no-install-recommends ca-certificates curl gpg debian-archive-keyring

# Debian 基础仓库的 CMake 3.31 不满足项目要求；仅对 CMake 选用 backports。
cat > /etc/apt/sources.list.d/domino-backports.sources <<'EOF'
Types: deb
URIs: https://deb.debian.org/debian
Suites: trixie-backports
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
EOF
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
    | gpg --batch --yes --dearmor -o /usr/share/keyrings/domino-llvm-archive-keyring.gpg
chmod 644 /usr/share/keyrings/domino-llvm-archive-keyring.gpg
cat > /etc/apt/sources.list.d/domino-llvm.sources <<EOF
Types: deb
URIs: https://apt.llvm.org/trixie/
Suites: llvm-toolchain-trixie-${LLVM_VERSION}
Components: main
Signed-By: /usr/share/keyrings/domino-llvm-archive-keyring.gpg
EOF

apt-get update
apt-get install -y --no-install-recommends \
    build-essential git cmake/trixie-backports \
    "clang-${LLVM_VERSION}" "clang-tools-${LLVM_VERSION}" "clangd-${LLVM_VERSION}" \
    "clang-format-${LLVM_VERSION}" "clang-tidy-${LLVM_VERSION}" "lldb-${LLVM_VERSION}" \
    "libclang-rt-${LLVM_VERSION}-dev" "llvm-${LLVM_VERSION}-linker-tools"

for tool in clang clang++ clangd clang-format clang-tidy lldb scan-build; do
    update-alternatives --install "/usr/bin/$tool" "$tool" "/usr/bin/${tool}-${LLVM_VERSION}" "$LLVM_VERSION"
    update-alternatives --set "$tool" "/usr/bin/${tool}-${LLVM_VERSION}"
done

# 只解包独立运行库，不升级 Debian 系统 glibc、不设置 LD_LIBRARY_PATH。
bash "$SCRIPT_DIR/scripts/setup-glibc.sh"

# 检查实际 PATH，避免镜像中预装的旧工具遮蔽刚安装的版本。
hash -r
cmake_version="$(cmake --version | sed -n '1s/^cmake version //p')"
dpkg --compare-versions "$cmake_version" ge 4.2.1 || {
    echo "错误: PATH 中的 $(command -v cmake) 版本低于 4.2.1" >&2
    exit 1
}
clang --version | head -n 1 | grep -Eq "clang version ${LLVM_VERSION}\." || {
    echo "错误: PATH 中的 $(command -v clang) 不是 Clang ${LLVM_VERSION}" >&2
    exit 1
}
echo "Codex Cloud Debian 13 安装完成（Clang ${LLVM_VERSION}，CMake ${cmake_version}，glibc 2.43）。"
echo "后续任务保留 DOMINO_GLIBC_ROOT=$DOMINO_GLIBC_ROOT，并通过 ./scripts/build.sh 构建。"
