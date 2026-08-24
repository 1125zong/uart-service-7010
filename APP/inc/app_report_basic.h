#ifndef __APP_REPORT_BASIC_H__
#define __APP_REPORT_BASIC_H__

#include "app_report.h"


/* 协议命令处理函数声明 */
void handle_device_overview(int client_fd, unsigned char *data, int len);
void handle_device_name_operation(int client_fd, unsigned char *data, int len);

#endif