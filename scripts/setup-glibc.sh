#!/usr/bin/env bash
# 解包固定版本的独立 glibc，供现有 DOMINO_GLIBC_ROOT 工具链使用。
set -euo pipefail

[[ $# -eq 0 ]] || { echo "用法: DOMINO_GLIBC_ROOT=<安装目录> bash $0" >&2; exit 1; }
[[ "$(uname -s):$(uname -m)" == Linux:x86_64 ]] || { echo "错误: 独立 glibc 工具链仅支持 Linux x86_64" >&2; exit 1; }
GLIBC_ROOT="$(realpath -m "${DOMINO_GLIBC_ROOT:?请设置 DOMINO_GLIBC_ROOT 为独立 glibc 安装目录}")"
[[ "$GLIBC_ROOT" != / ]] || { echo "错误: 独立 glibc 不能安装到系统根目录" >&2; exit 1; }
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_CHECKSUMS="$SCRIPT_DIR/glibc-2.43-amd64.sha256"
DOWNLOAD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/domino-glibc.XXXXXX")"
trap 'rm -rf -- "$DOWNLOAD_DIR"' EXIT

while read -r _checksum package_file; do
    curl -fsSL "https://archive.ubuntu.com/ubuntu/pool/main/g/glibc/$package_file" -o "$DOWNLOAD_DIR/$package_file"
done < "$PACKAGE_CHECKSUMS"
(
    cd "$DOWNLOAD_DIR"
    sha256sum --check "$PACKAGE_CHECKSUMS"
)

mkdir -p "$GLIBC_ROOT"
while read -r _checksum package_file; do
    dpkg-deb --extract "$DOWNLOAD_DIR/$package_file" "$GLIBC_ROOT"
done < "$PACKAGE_CHECKSUMS"

# 复用宿主开发工具链的 Linux ABI 头文件和 GCC 运行库。
# Debian 的部分 ABI 头文件链接到 /usr/lib/linux/uapi；复制实际内容，避免 sysroot 内悬空链接。
# 同时替换上次安装保留的链接，让重复安装也能修复旧目录。
cp -aL --remove-destination /usr/include/linux /usr/include/asm-generic "$GLIBC_ROOT/usr/include/"
cp -aL --remove-destination /usr/include/x86_64-linux-gnu/asm "$GLIBC_ROOT/usr/include/x86_64-linux-gnu/"
cp -L "$(gcc -print-file-name=libgcc_s.so.1)" "$GLIBC_ROOT/usr/lib/x86_64-linux-gnu/libgcc_s.so.1"
ln -sfn usr/lib "$GLIBC_ROOT/lib"
ln -sfn usr/lib64 "$GLIBC_ROOT/lib64"
cp "$PACKAGE_CHECKSUMS" "$GLIBC_ROOT/glibc-packages.sha256"

echo "独立 glibc 2.43-2ubuntu2.4 安装完成：$GLIBC_ROOT"
