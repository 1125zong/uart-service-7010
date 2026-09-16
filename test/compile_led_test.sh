#!/bin/bash

echo "=== 编译LED测试程序 ==="

# 检查是否在正确的目录
if [ ! -f "test_led.c" ]; then
    echo "错误：请在test目录下运行此脚本"
    exit 1
fi

# 编译测试程序
echo "正在编译..."
gcc -o test_led test_led.c -Wall -Wextra

if [ $? -eq 0 ]; then
    echo "编译成功！"
    echo "运行命令：sudo ./test_led"
    echo ""
    echo "注意：需要root权限访问/dev/mem"
else
    echo "编译失败！"
    exit 1
fi