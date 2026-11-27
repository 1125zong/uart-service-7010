#ifndef APP_VERSION_INFO_H
#define APP_VERSION_INFO_H

#include <stddef.h>

/*
 * 统一版本号查询接口。
 *
 * 版本号唯一来源仍是 app_version.h 的 FW_VERSION_*。
 * 编译时 app_version_info.c 会把 "APP_VERSION:<FW_VERSION_STRING>"
 * 写进二进制，因此：
 *   - 正在运行的进程：app_version_running()
 *   - 磁盘上的 app 文件：app_version_read_binary(path)
 * 两种情况都能拿到版本号，且天然与编译时版本一致。
 */

/* app 安装路径（p3 数据分区），与 app_update.c 中 APP_INSTALL_PATH_ON_P3 一致 */
#ifndef APP_INSTALL_BIN_PATH
#define APP_INSTALL_BIN_PATH "/mnt/apps/uart_service"
#endif

/* app_version_read_binary() 返回值，用于区分“文件不存在”和“文件里没有标记” */
#define APP_VERSION_OK              0
#define APP_VERSION_ERR_NO_FILE     (-1)   /* 文件不存在或无法打开 */
#define APP_VERSION_ERR_NO_MARKER   (-2)   /* 文件可读，但没有版本标记（旧版本二进制） */

/* 当前进程（正在运行的程序）的版本号，例如 "V1.0.0.1" */
const char *app_version_running(void);

/**
 * @brief 从任意二进制文件中读取它编译时写入的版本号。
 * @param path    二进制文件路径
 * @param out     输出缓冲区（建议 >= 32 字节）
 * @param out_len 输出缓冲区长度
 * @return APP_VERSION_OK 成功；
 *         APP_VERSION_ERR_NO_FILE 文件不存在/打不开；
 *         APP_VERSION_ERR_NO_MARKER 是旧版本二进制，没有版本标记
 */
int app_version_read_binary(const char *path, char *out, size_t out_len);

/**
 * @brief 打印两份版本：正在运行的进程 / 磁盘上安装的文件。
 * @param bin_path app 安装路径，传 NULL 使用 APP_INSTALL_BIN_PATH
 */
void app_version_dump_all(const char *bin_path);

#endif /* APP_VERSION_INFO_H */
