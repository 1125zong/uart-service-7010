#ifndef APP_VERSION_H
#define APP_VERSION_H

/*
 * 固件版本：唯一版本源，升级版本只改这四行。
 * 启动日志、协议上报、屏幕显示等全部由这四个宏派生，不要另处再写版本号。
 *
 * 注意：
 *   1) 必须写成纯数字，不能带括号，否则字符串化会得到 "(1)" 而不是 "1"；
 *   2) FW_VERSION_BUILD 要填进 unsigned char，只能取 0~255。
 */
#define FW_VERSION_MAJOR             1
#define FW_VERSION_MINOR             0
#define FW_VERSION_PATCH             0
#define FW_VERSION_BUILD             3

/* 版本字符串，由上面四个宏自动拼接，不要手改 */
#define FW_VER_STR_(x)               #x
#define FW_VER_STR(x)                FW_VER_STR_(x)
#define FW_VERSION_STRING            "V" FW_VER_STR(FW_VERSION_MAJOR) "." \
                                         FW_VER_STR(FW_VERSION_MINOR) "." \
                                         FW_VER_STR(FW_VERSION_PATCH) "." \
                                         FW_VER_STR(FW_VERSION_BUILD)

/*
 * 版本标记：会被编译进二进制（见 app_version_info.c）。
 * 升级程序可以直接扫描任意二进制文件（如 /mnt/apps/uart_service）
 * 找到这个固定前缀，读出该文件自身携带的版本号。
 * 前缀必须保持唯一，且在读取端（app_version_info.h）保持一致。
 */
#define APP_VERSION_MARKER_PREFIX    "APP_VERSION:"
#define APP_VERSION_MARKER           APP_VERSION_MARKER_PREFIX FW_VERSION_STRING

#endif /* APP_VERSION_H */
