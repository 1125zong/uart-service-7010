#ifndef APP_REPORT_H
#define APP_REPORT_H

#include "app_com.h"
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* 上报客户端会话结构体 */
#define REPORT_BUFFER_SIZE 1024

typedef struct 
{
    int fd;                                     // 客户端socket fd
    int active;                                 // 是否活跃
    unsigned char rx_buffer[REPORT_BUFFER_SIZE]; // 接收缓冲区
    int rx_bytes;                               // 接收缓冲区中的字节数
    time_t last_activity_time;                  // 最后活动时间
    struct sockaddr_in client_addr;             // 客户端地址信息
} ReportClientSession;

/* 上报服务端口定义 */
#define TCP_REPORT_PORT 4000
#define MAX_REPORT_CLIENTS 5

/* 全局变量声明 */
extern ReportClientSession g_report_clients[MAX_REPORT_CLIENTS];

/* 串口参数常量定义 */
#define PARITY_NONE     0    /* 无校验 */
#define PARITY_ODD      1    /* 奇校验 */
#define PARITY_EVEN     2    /* 偶校验 */

#define DATA_BITS_5     5    /* 5位数据位 */
#define DATA_BITS_6     6    /* 6位数据位 */
#define DATA_BITS_7     7    /* 7位数据位 */
#define DATA_BITS_8     8    /* 8位数据位 */

#define STOP_BITS_1     1    /* 1位停止位 */
#define STOP_BITS_2     2    /* 2位停止位 */

/* 上报任务函数声明 */
extern void *ReportManagerTask(void *arg);
extern void send_report_response(int client_fd, const unsigned char main_id,  unsigned char sub_id, unsigned char status);

#endif /* APP_REPORT_H */
