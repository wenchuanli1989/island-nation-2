#!/usr/bin/env bash
# Claude Code 云端环境（Ubuntu 24.04 x86_64）的安装脚本，由环境的 Setup script 以 root 运行，须连同仓库使用。
# 安装复用 setup-dev.sh，本脚本只处理云端网络和镜像的差异。
set -euo pipefail

[[ $# -eq 0 ]] || { echo "用法: bash $0" >&2; exit 1; }
[[ $EUID -eq 0 ]] || { echo "错误: 请以 root 运行" >&2; exit 1; }
export LLVM_VERSION=22

# 两个软件源都不在云端默认白名单内，先给出明确提示。
for url in https://apt.llvm.org/llvm-snapshot.gpg.key https://apt.kitware.com/keys/kitware-archive-latest.asc; do
    curl -fsSL --max-time 30 "$url" -o /dev/null || {
        echo "错误: 无法访问 $url；请在云端环境的 Allowed domains 中加入 apt.llvm.org 和 apt.kitware.com" >&2
        exit 1
    }
done

# 镜像可能带有白名单外的第三方源，会使 apt-get update 失败；此时只保留 Ubuntu 官方源。
if ! apt-get update; then
    for source in /etc/apt/sources.list.d/*.{list,sources}; do
        [[ ! -e "$source" || "$source" == */ubuntu.sources ]] || mv -- "$source" "$source.disabled"
    done
fi

bash "$(dirname "${BASH_SOURCE[0]}")/setup-dev.sh"

# 镜像自带的旧版 LLVM 会留下 GNU ar 优先加载的插件，它读不了本版 Clang 的 ThinLTO 目标文件，
# 使 release 静态库缺少符号索引；只保留本版 LLVM 的插件链接。
for plugin in /usr/lib/bfd-plugins/LLVMgold*.so; do
    [[ "$(realpath "$plugin")" == "/usr/lib/llvm-$LLVM_VERSION/"* ]] || rm -f -- "$plugin"
done

# 镜像自带其他版本的 CMake 和 Clang，核验 PATH 中生效的是新装版本。
hash -r
cmake_version="$(cmake --version | sed -n '1s/^cmake version //p')"
dpkg --compare-versions "$cmake_version" ge 4.2.1 || { echo "错误: $(command -v cmake) 版本低于 4.2.1（$cmake_version）" >&2; exit 1; }
[[ "$(clang --version)" == *"clang version $LLVM_VERSION."* ]] || { echo "错误: $(command -v clang) 不是 Clang $LLVM_VERSION" >&2; exit 1; }
