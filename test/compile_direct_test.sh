#!/bin/bash

echo "=== 编译直接LED测试程序 ==="

# 检查文件是否存在
if [ ! -f "test_led_direct.c" ]; then
    echo "错误：未找到 test_led_direct.c"
    exit 1
fi

# 编译
gcc -o test_led_direct test_led_direct.c -Wall -Wextra

if [ $? -eq 0 ]; then
    echo "编译成功！"
    echo "运行命令：sudo ./test_led_direct"
else
    echo "编译失败！"
    exit 1
fi