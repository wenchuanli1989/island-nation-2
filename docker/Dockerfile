# Domino 游戏服务器开发环境 Docker 镜像
FROM ubuntu:26.04

ARG LLVM_VERSION=22
ENV TZ=Asia/Shanghai

# 工具链与服务器共用 setup-dev.sh；构建上下文为 scripts/（见 compose.yaml）。
COPY setup-dev.sh setup-docker.sh /tmp/setup/
RUN LLVM_VERSION="${LLVM_VERSION}" bash /tmp/setup/setup-docker.sh \
    && rm -r /tmp/setup

WORKDIR /root/domino

EXPOSE 22 6666

CMD ["/usr/sbin/sshd", "-D"]
