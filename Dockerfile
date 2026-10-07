# Domino游戏服务器开发环境Docker镜像
FROM ubuntu:24.04

ARG LLVM_VERSION=22

# 设置环境变量，避免交互式安装
ENV DEBIAN_FRONTEND=noninteractive TZ=Asia/Shanghai

RUN apt-get update && apt-get install -y ca-certificates gpg wget

# CMake 源
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc  | gpg --dearmor > /usr/share/keyrings/kitware-archive-keyring.gpg 
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ noble main' > /etc/apt/sources.list.d/kitware.list

# llvm 源
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key  | gpg --dearmor > /usr/share/keyrings/llvm-archive-keyring.gpg
RUN echo "deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-${LLVM_VERSION} main" > /etc/apt/sources.list.d/llvm.list

# 安装基础工具和开发依赖
RUN apt-get update && apt-get install -y \
    # 基础工具
    build-essential \
    cmake \
    git \
    vim \
    curl \
    wget \
    # 包含scan-build 
    clang-tools-${LLVM_VERSION} \
    clang-${LLVM_VERSION} \
    clangd-${LLVM_VERSION} \
    clang-format-${LLVM_VERSION} \
    clang-tidy-${LLVM_VERSION} \
    lldb-${LLVM_VERSION} \
    pahole \
    # SSH服务器（用于VSCode远程连接）
    openssh-server \
    sudo \
    # 清理 删除 APT 缓存，避免镜像臃肿
    && rm -rf /var/lib/apt/lists/*

# SSH 会话会重新设置 PATH，不能仅依赖 Docker ENV 中的 LLVM 路径。
# 注册无版本号命令，同时匹配 VSCode 配置中的 /usr/bin/clangd 和 clang-format。
RUN set -eu; \
    for tool in clang clang++ clangd clang-format clang-tidy lldb scan-build; do \
        update-alternatives --install "/usr/bin/${tool}" "${tool}" \
            "/usr/bin/${tool}-${LLVM_VERSION}" "${LLVM_VERSION}"; \
    done

# 使用最小 PATH 验证非交互式 SSH 构建所需的编译器入口。
RUN env -i PATH=/usr/bin:/bin clang --version

# RUN curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.3/install.sh | bash

# # 添加命令到~/.bashrc，用于安装Node.js
# RUN echo 'export NVM_DIR="$HOME/.nvm"' >> ~/.bashrc
# RUN echo '[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"  # Load NVM' >> ~/.bashrc
# RUN echo '[ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"  # Load NVM bash completion' >> ~/.bashrc
# RUN bash -lc "source /root/.nvm/nvm.sh && nvm install 25 && nvm use 25"

RUN git config --global user.name domino
RUN git config --global user.email domino@domino.com

# 创建 SSH 密钥对
RUN mkdir -p /root/.ssh && \
    ssh-keygen -t ed25519 -C "domino@domino.com" -f /root/.ssh/id_ed25519 -N "" && \
    ssh-keyscan -t rsa,ecdsa,ed25519 github.com > /root/.ssh/known_hosts 2>&1

# Configure SSH
RUN echo 'root:123456' | chpasswd

# Prepare privilege separation directory for sshd
RUN mkdir -p /run/sshd

# Allow root login
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config

# 设置工作目录
WORKDIR /root/domino

# 暴露SSH端口
EXPOSE 22
EXPOSE 6666

# 启动SSH服务
CMD ["/usr/sbin/sshd", "-D"]

