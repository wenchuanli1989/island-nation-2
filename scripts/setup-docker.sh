#!/usr/bin/env bash
# Docker 开发镜像（Ubuntu 26.04）的安装脚本，由 docker/Dockerfile 在构建时运行。
# 工具链复用 setup-dev.sh，本脚本只补充 SSH 远程开发配置；会开启 root 密码登录，不要在服务器上运行。
set -euo pipefail

[[ $# -eq 0 ]] || { echo "用法: bash $0（LLVM_VERSION 默认 22）" >&2; exit 1; }
bash "$(dirname "${BASH_SOURCE[0]}")/setup-dev.sh"

export DEBIAN_FRONTEND=noninteractive
apt-get install -y --no-install-recommends less openssh-server sudo vim
rm -rf /var/lib/apt/lists/*

git config --global user.name domino
git config --global user.email domino@domino.com

install -d -m 700 /root/.ssh
ssh-keygen -t ed25519 -C domino@domino.com -f /root/.ssh/id_ed25519 -N ""
ssh-keyscan -T 10 -t rsa,ecdsa,ed25519 github.com > /root/.ssh/known_hosts

echo 'root:123456' | chpasswd
install -d -m 755 /run/sshd /etc/ssh/sshd_config.d
printf 'PermitRootLogin yes\nPasswordAuthentication yes\n' > /etc/ssh/sshd_config.d/00-domino-dev.conf
ssh-keygen -A
/usr/sbin/sshd -t
