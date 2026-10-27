# Domino 游戏服务器开发环境 Docker 镜像
FROM ubuntu:26.04

ARG LLVM_VERSION=22
ENV TZ=Asia/Shanghai

# 与服务器使用同一份环境安装脚本。
COPY setup-dev.sh /tmp/setup-dev.sh
RUN LLVM_VERSION="${LLVM_VERSION}" bash /tmp/setup-dev.sh \
    && rm /tmp/setup-dev.sh

WORKDIR /root/domino

EXPOSE 22 6666

CMD ["/usr/sbin/sshd", "-D"]
