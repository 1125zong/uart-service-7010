#ifndef APP_PORTS_H
#define APP_PORTS_H

#include "app_com.h"

/**
 * @brief 处理端口管理操作
 * @details 功能ID: 0x08, 包含端口重置、锁定、解锁、查询等功能
 */
void handle_port_management(int client_fd, unsigned char *data, int len);



void handle_port_reset(int client_fd, unsigned char *data, int len);
void handle_port_lock(int client_fd, unsigned char *data, int len);
void handle_port_unlock(int client_fd, unsigned char *data, int len);
void handle_port_lock_query(int client_fd, unsigned char *data, int len);
void handle_port_mirror_config(int client_fd, unsigned char *data, int len);
void handle_port_self_check(int client_fd, unsigned char *data);



#endif /* APP_PORTS_H */