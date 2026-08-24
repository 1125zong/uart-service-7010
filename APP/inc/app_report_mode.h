#ifndef __APP_REPORT_MODE_H__
#define __APP_REPORT_MODE_H__

#include "app_report.h"

/* 操作模式相关函数声明 */
void handle_read_operating_mode(int client_fd, unsigned char *data, int len);

#endif /* __APP_REPORT_MODE_H__ */
