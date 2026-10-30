#!/usr/bin/env bash
# 远程开发环境：sudo bash setup-dev.sh；Codex Cloud：bash setup-dev.sh --cloud
set -euo pipefail

[[ $# -eq 0 || ( $# -eq 1 && "${1:-}" == --cloud ) ]] || { echo "用法: bash $0 [--cloud]（LLVM_VERSION 默认 22）" >&2; exit 1; }
CLOUD_MODE=false
[[ "${1:-}" != --cloud ]] || CLOUD_MODE=true

# shellcheck source=/dev/null
source /etc/os-release
case "${ID:-}:${VERSION_ID:-}:$CLOUD_MODE" in
    ubuntu:26.04:*|ubuntu:22.04:true|ubuntu:24.04:true) ;;
    *) echo "错误: 默认模式仅支持 Ubuntu 26.04；--cloud 支持 Ubuntu 22.04/24.04/26.04" >&2; exit 1 ;;
esac
if [[ "$CLOUD_MODE" == true && $EUID -ne 0 ]]; then
    exec sudo -nE bash "$0" "$@"
fi
[[ $EUID -eq 0 ]] || { echo "请使用 sudo bash $0 运行" >&2; exit 1; }
LLVM_VERSION="${LLVM_VERSION:-22}"
export DEBIAN_FRONTEND=noninteractive LC_ALL=C
umask 022

apt-get update
apt-get install -y ca-certificates gpg wget

# CMake 和 LLVM 官方软件源。
wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc \
    | gpg --batch --yes --dearmor -o /usr/share/keyrings/kitware-archive-keyring.gpg
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
    | gpg --batch --yes --dearmor -o /usr/share/keyrings/llvm-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ ${VERSION_CODENAME} main" \
    > /etc/apt/sources.list.d/kitware.list
echo "deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] https://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-${LLVM_VERSION} main" \
    > /etc/apt/sources.list.d/llvm.list
chmod 644 /usr/share/keyrings/{kitware,llvm}-archive-keyring.gpg

apt-get update
DEV_PACKAGES=(vim openssh-server sudo pahole)
[[ "$CLOUD_MODE" == false ]] || DEV_PACKAGES=()
apt-get install -y \
    build-essential cmake git curl "${DEV_PACKAGES[@]}" \
    "clang-${LLVM_VERSION}" "clang-tools-${LLVM_VERSION}" "clangd-${LLVM_VERSION}" \
    "clang-format-${LLVM_VERSION}" "clang-tidy-${LLVM_VERSION}" "lldb-${LLVM_VERSION}" \
    "libclang-rt-${LLVM_VERSION}-dev" "llvm-${LLVM_VERSION}-linker-tools"
rm -rf /var/lib/apt/lists/*

for tool in clang clang++ clangd clang-format clang-tidy lldb scan-build; do
    update-alternatives --install "/usr/bin/$tool" "$tool" "/usr/bin/${tool}-${LLVM_VERSION}" "$LLVM_VERSION"
    update-alternatives --set "$tool" "/usr/bin/${tool}-${LLVM_VERSION}"
done

if [[ "$CLOUD_MODE" == true ]]; then
    if [[ -n "${DOMINO_GLIBC_ROOT:-}" ]]; then
        bash "$(dirname "${BASH_SOURCE[0]}")/scripts/setup-glibc.sh"
    fi
    echo "Cloud 开发环境安装完成（LLVM ${LLVM_VERSION}，CMake >= 4.2.1）。"
    exit 0
fi

# 沿用原 Dockerfile 的开发用 Git 和 SSH 配置。
git config --global user.name domino
git config --global user.email domino@domino.com
install -d -m 700 /root/.ssh
if [[ ! -f /root/.ssh/id_ed25519 ]]; then
    ssh-keygen -t ed25519 -C domino@domino.com -f /root/.ssh/id_ed25519 -N ""
fi
if ! ssh-keygen -F github.com -f /root/.ssh/known_hosts >/dev/null 2>&1; then
    ssh-keyscan -T 10 -t rsa,ecdsa,ed25519 github.com >> /root/.ssh/known_hosts
fi
chmod 600 /root/.ssh/known_hosts
echo 'root:123456' | chpasswd
install -d -m 755 /run/sshd /etc/ssh/sshd_config.d
printf 'PermitRootLogin yes\nPasswordAuthentication yes\n' > /etc/ssh/sshd_config.d/00-domino-dev.conf
ssh-keygen -A
/usr/sbin/sshd -t

echo "开发环境安装完成。"
