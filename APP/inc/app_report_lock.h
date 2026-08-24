#ifndef APP_REPORT_LOCK_H
#define APP_REPORT_LOCK_H

#include "app_ports.h"

/**
 * @brief 处理设备锁定命令
 * @details 功能ID: 0x08, 子功能ID: 0x06
 */
void handle_device_lock(int client_fd, unsigned char *data, int len);

/**
 * @brief 处理设备解锁命令
 * @details 功能ID: 0x08, 子功能ID: 0x07
 */
void handle_device_unlock(int client_fd, unsigned char *data, int len);

/**
 * @brief 处理0x08 - 设备管理操作
 */
void handle_device_management(int client_fd, unsigned char *data, int len);

#endif /* APP_REPORT_LOCK_H */