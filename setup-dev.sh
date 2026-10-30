#!/usr/bin/env bash
# Ubuntu 26.04 远程开发环境：sudo bash setup-dev.sh
set -euo pipefail

[[ $# -eq 0 ]] || { echo "用法: sudo bash $0（LLVM_VERSION 默认 22）" >&2; exit 1; }

# shellcheck source=/dev/null
source /etc/os-release
[[ "${ID:-}:${VERSION_ID:-}" == ubuntu:26.04 ]] || { echo "错误: 仅支持 Ubuntu 26.04" >&2; exit 1; }
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
apt-get install -y \
    build-essential cmake git curl vim openssh-server sudo pahole \
    "clang-${LLVM_VERSION}" "clang-tools-${LLVM_VERSION}" "clangd-${LLVM_VERSION}" \
    "clang-format-${LLVM_VERSION}" "clang-tidy-${LLVM_VERSION}" "lldb-${LLVM_VERSION}" \
    "libclang-rt-${LLVM_VERSION}-dev" "llvm-${LLVM_VERSION}-linker-tools"
rm -rf /var/lib/apt/lists/*

for tool in clang clang++ clangd clang-format clang-tidy lldb scan-build; do
    update-alternatives --install "/usr/bin/$tool" "$tool" "/usr/bin/${tool}-${LLVM_VERSION}" "$LLVM_VERSION"
    update-alternatives --set "$tool" "/usr/bin/${tool}-${LLVM_VERSION}"
done

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
