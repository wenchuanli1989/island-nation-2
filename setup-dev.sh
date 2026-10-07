#!/bin/bash
# Domino开发环境快速设置脚本

echo "=== Domino游戏服务器开发环境设置 ==="
echo ""

# 检查Docker是否安装
if ! command -v docker &> /dev/null; then
    echo "错误: 未找到Docker，请先安装Docker"
    exit 1
fi

# 检查Docker Compose是否安装
if ! command -v docker-compose &> /dev/null; then
    echo "错误: 未找到Docker Compose，请先安装Docker Compose"
    exit 1
fi

echo "1. 构建并启动Docker容器..."
docker-compose up -d --build

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ 容器启动成功！"
    echo ""
    echo "=== 连接信息 ==="
    echo "主机: localhost"
    echo "端口: 10022"
    echo "用户名: root"
    echo "密码: 123456"
    echo ""
    echo "=== VSCode连接步骤 ==="
    echo "1. 按 Cmd+Shift+P (Mac) 或 Ctrl+Shift+P (Windows/Linux)"
    echo "2. 输入: Remote-SSH: Connect to Host"
    echo "3. 输入: root@localhost -p 10022"
    echo "4. 输入密码: 123456"
    echo "5. 打开目录: /root/domino"
    echo ""
    echo "=== 或者使用SSH命令 ==="
    echo "ssh root@localhost -p 10022"
    echo ""
else
    echo "错误: 容器启动失败"
    exit 1
fi


