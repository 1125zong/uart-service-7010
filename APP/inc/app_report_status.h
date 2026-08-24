#ifndef __APP_REPORT_STATUS_H__
#define __APP_REPORT_STATUS_H__

#include "app_report.h"

/* 状态监控相关函数声明 */
void handle_device_status_monitoring(int client_fd, unsigned char *data, int len);

#endif /* __APP_REPORT_STATUS_H__ */