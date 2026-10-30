#!/usr/bin/env bash
# Codex Cloud Debian 13 (amd64)：下载并解包到用户目录，不执行系统安装。
set -euo pipefail

[[ $# -eq 0 ]] || { echo "用法: [DOMINO_CLOUD_ROOT=/workspace/.local/domino] bash $0" >&2; exit 1; }
# shellcheck source=/dev/null
source /etc/os-release
[[ "${ID:-}:${VERSION_ID:-}:$(uname -m)" == debian:13:x86_64 ]] || {
    echo "错误: 仅支持 Debian 13 (trixie) Linux x86_64" >&2
    exit 1
}
for tool in /usr/bin/apt-get curl gpg dpkg-deb gcc g++ make git perl /usr/bin/python3; do
    command -v "$tool" >/dev/null || { echo "错误: 基础镜像缺少 $tool" >&2; exit 1; }
done
[[ -r /usr/share/keyrings/debian-archive-keyring.gpg ]] || { echo "错误: 缺少 Debian 官方签名密钥" >&2; exit 1; }

# 单独管理 Cloud 的安装根目录；env.sh 会覆盖旧环境中的 /opt glibc 设置。
CLOUD_ROOT="${DOMINO_CLOUD_ROOT:-/workspace/.local/domino}"
[[ "$CLOUD_ROOT" == /* ]] || { echo "错误: DOMINO_CLOUD_ROOT 必须是绝对路径" >&2; exit 1; }
CLOUD_ROOT="$(realpath -m "$CLOUD_ROOT")"
[[ "$CLOUD_ROOT" != / && "$CLOUD_ROOT" != *[[:space:]\"\\]* ]] || {
    echo "错误: DOMINO_CLOUD_ROOT 必须是非根绝对路径，且不含空白、双引号或反斜杠" >&2
    exit 1
}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LLVM_VERSION=22
APT_ROOT="$CLOUD_ROOT/apt"
PACKAGE_ROOT="$CLOUD_ROOT/packages"
export LC_ALL=C
umask 022
mkdir -p "$APT_ROOT/etc/apt.conf.d" "$APT_ROOT/etc/sources.list.d" "$APT_ROOT/etc/preferences.d" "$APT_ROOT/state/lists/partial" \
    "$APT_ROOT/cache" "$APT_ROOT/log" "$PACKAGE_ROOT" "$CLOUD_ROOT/bin"
WORK_DIR="$(mktemp -d "$CLOUD_ROOT/.download.XXXXXX")"
trap 'rm -rf -- "$WORK_DIR"' EXIT
mkdir -m 700 "$WORK_DIR/gnupg"
mkdir -p "$WORK_DIR/archives/partial"

curl -fsSL --retry 2 https://apt.llvm.org/llvm-snapshot.gpg.key -o "$WORK_DIR/llvm.asc"
llvm_fingerprint="$(gpg --batch --homedir "$WORK_DIR/gnupg" --show-keys --with-colons "$WORK_DIR/llvm.asc" \
    | awk -F: '$1 == "fpr" && !seen++ { print $10 }')"
[[ "$llvm_fingerprint" == 6084F3CF814B57C1CF12EFD515CF4D18AF4F7421 ]] || {
    echo "错误: LLVM 软件源签名密钥指纹不匹配" >&2
    exit 1
}
gpg --batch --yes --homedir "$WORK_DIR/gnupg" --dearmor -o "$APT_ROOT/llvm.gpg" "$WORK_DIR/llvm.asc"

# 禁用系统 apt.conf.d（包括安装后清理钩子），所有索引、缓存和日志均写入用户目录。
# 用系统 dpkg 状态的本地副本解析缺失依赖，锁文件也留在用户目录。
# 不会运行 dpkg 安装或维护脚本。
cp /var/lib/dpkg/status "$APT_ROOT/state/status"
cat > "$APT_ROOT/apt.conf" <<EOF
Dir::Etc "$APT_ROOT/etc";
Dir::Etc::main "apt.conf";
Dir::Etc::parts "apt.conf.d";
Dir::Etc::sourcelist "sources.list";
Dir::Etc::sourceparts "sources.list.d";
Dir::State "$APT_ROOT/state";
Dir::State::status "$APT_ROOT/state/status";
Dir::Cache "$APT_ROOT/cache";
Dir::Cache::archives "$WORK_DIR/archives";
Dir::Log "$APT_ROOT/log";
APT::Architecture "amd64";
Acquire::Languages "none";
EOF
touch "$APT_ROOT/etc/apt.conf"
cat > "$APT_ROOT/etc/sources.list" <<EOF
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie-updates main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie-backports main
deb [signed-by=$APT_ROOT/llvm.gpg] https://apt.llvm.org/trixie/ llvm-toolchain-trixie-$LLVM_VERSION main
EOF

# APT 使用小写代理变量；保留平台代理和 CA 验证，不走系统 apt 包装器。
export http_proxy="${http_proxy:-${HTTP_PROXY:-}}" https_proxy="${https_proxy:-${HTTPS_PROXY:-}}"
run_apt() {
    APT_CONFIG="$APT_ROOT/apt.conf" /usr/bin/apt-get -o "APT::Sandbox::User=$(id -un)" \
        -o Acquire::Retries=2 -o APT::Update::Error-Mode=any "$@"
}
run_apt update
run_apt --download-only --reinstall --no-install-recommends -y install \
    cmake/trixie-backports cmake-data/trixie-backports \
    "clang-$LLVM_VERSION" "clang-tools-$LLVM_VERSION" "clangd-$LLVM_VERSION" \
    "clang-format-$LLVM_VERSION" "clang-tidy-$LLVM_VERSION" "lldb-$LLVM_VERSION" \
    "libclang-rt-$LLVM_VERSION-dev" "llvm-$LLVM_VERSION-linker-tools" "llvm-$LLVM_VERSION"

# APT 已按签名索引核验软件包摘要；只解包，不触碰系统软件数据库。
packages=("$WORK_DIR"/archives/*.deb)
(cd "$WORK_DIR/archives" && sha256sum -- *.deb) > "$CLOUD_ROOT/packages.sha256"
: > "$CLOUD_ROOT/packages.versions"
for package in "${packages[@]}"; do
    dpkg-deb --field "$package" Package Version >> "$CLOUD_ROOT/packages.versions"
    dpkg-deb --extract "$package" "$PACKAGE_ROOT"
done

# 工具依赖库只作用于该命令及子进程，独立 glibc 不加入 LD_LIBRARY_PATH。
cat > "$CLOUD_ROOT/bin/.domino-tool" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tool="${0##*/}"
export LD_LIBRARY_PATH="$root/packages/usr/lib/x86_64-linux-gnu:$root/packages/usr/lib/llvm-22/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
case "$tool" in
    cmake|ctest|cpack) exec "$root/packages/usr/bin/$tool" "$@" ;;
    lldb) export PYTHONPATH="$root/packages/usr/lib/llvm-22/lib/python3.13/site-packages${PYTHONPATH:+:$PYTHONPATH}" ;;
esac
exec "$root/packages/usr/bin/$tool-22" "$@"
EOF
chmod 755 "$CLOUD_ROOT/bin/.domino-tool"
for tool in cmake ctest cpack clang clang++ clangd clang-format clang-tidy lldb scan-build llvm-symbolizer llvm-ar llvm-ranlib; do
    ln -sfn .domino-tool "$CLOUD_ROOT/bin/$tool"
done

# glibc 与工具链使用同一个可写根目录，不继承旧版 Cloud 的 /opt 设置。
export DOMINO_GLIBC_ROOT="$CLOUD_ROOT/glibc-2.43"
bash "$SCRIPT_DIR/scripts/setup-glibc.sh"
{
    printf 'export DOMINO_CLOUD_ROOT=%q\n' "$CLOUD_ROOT"
    printf 'export DOMINO_GLIBC_ROOT=%q\n' "$DOMINO_GLIBC_ROOT"
    cat <<'EOF'
case "$PATH" in
    "$DOMINO_CLOUD_ROOT/bin"|"$DOMINO_CLOUD_ROOT/bin:"*) ;;
    *) export PATH="$DOMINO_CLOUD_ROOT/bin:$PATH" ;;
esac
EOF
} > "$CLOUD_ROOT/env.sh"
# shellcheck source=/dev/null
source "$CLOUD_ROOT/env.sh"
hash -r
cmake_version="$(cmake --version | sed -n '1s/^cmake version //p')"
dpkg --compare-versions "$cmake_version" ge 4.2.1 || { echo "错误: CMake 版本低于 4.2.1" >&2; exit 1; }
clang --version | grep -Eq "clang version $LLVM_VERSION\." || { echo "错误: 未启用 Clang $LLVM_VERSION" >&2; exit 1; }
echo "Codex Cloud 用户目录安装完成（Clang $LLVM_VERSION，CMake $cmake_version，glibc 2.43）。"
printf '后续 shell 先执行: source %q\n' "$CLOUD_ROOT/env.sh"
