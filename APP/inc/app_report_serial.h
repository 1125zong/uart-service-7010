#ifndef __APP_REPORT_SERIAL_H__
#define __APP_REPORT_SERIAL_H__

#include "app_report.h"

/* 串口设置和模式相关函数声明 */
void handle_serial_port_settings(int client_fd, unsigned char *data, int len);
void handle_serial_port_mode(int client_fd, unsigned char *data, int len);


/* 串口模式子命令处理函数声明 */
void handle_set_single_realcom_mode(int client_fd, unsigned char *data, int len);
void handle_set_multiple_realcom_mode(int client_fd, unsigned char *data, int len);
void handle_set_single_tcp_server_mode(int client_fd, unsigned char *data, int len);
void handle_set_multiple_tcp_server_mode(int client_fd, unsigned char *data, int len);

/* 串口设置子命令处理函数声明 */
void handle_read_serial_port_settings(int client_fd, unsigned char *data, int len);
void handle_set_single_serial_port(int client_fd, unsigned char *data, int len);
void handle_set_multiple_serial_ports(int client_fd, unsigned char *data, int len);

#endif /* __APP_REPORT_SERIAL_H__ */
