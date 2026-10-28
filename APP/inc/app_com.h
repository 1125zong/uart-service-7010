#ifndef __APP_COM_H_
#define __APP_COM_H_

/*
 * =====================================================================================
 *
 * Filename:  app_com.h
 *
 * Description:  VxWorks 串口-网络转发应用通用头文件
 * 定义了所有任务共享的常量、数据结构和全局变量声明。
 *
 * =====================================================================================
 */

/* ------------------ VxWorks and Standard C Includes ------------------ */
#include <stdio.h>
#include <string.h>
#include "hal_com.h"
#include "hal_log.h"
#include "hal_timer.h"
#include "hal_ringbuffer.h"
#include "hal_axi16550.h"
#include "app_dev.h"

#include "system_abstraction_layer.h"

#define	SUCCESSED		0x01
#define	FAILED		    0x00


/* 网络端口定义 */
//#define TCP_DATA_PORT_START     950    // 数据通道起始端口 (950-965)
//#define TCP_SET_PORT_START      966    // 串口配置通道起始端口 (966-981)
#define TCP_SETTING_PORT        4000   // 全局设备配置端口

#define BUFFERCOM_SIZE_RX  (65536*2)
#define BUFFERCOM_SIZE_TX  (65536*2)

// 发送配置相关常量
#define MIN_BAUDRATE        50      // 最小波特率
#define MAX_BAUDRATE        1000000 // 最大波特率

#define BITS_PER_CHAR      10      // 1起始位+8数据位+1停止位
#define MIN_PACKET_SIZE    4       // 最小包大小(字节)
#define MAX_PACKET_SIZE    (256)    // 最大包大小(字节)

#define TIMER_TICK_US     100     // 定时器tick为100微秒
#define US_TO_MS          1000    // 微秒转毫秒
#define MAX_MSG_IN_Q      24      // 消息队列最大消息数
#define DATA_QUEUE_CAPACITY (NUM_PORTS * MAX_CLIENTS_PER_CHANNEL + 10) // 16 * 4 + 10 = 74

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


/* ------------------ Enumerations and Type Definitions ------------------ */
// ConnectionManagerTask 接收的控制命令类型
typedef enum {
    CTRL_CMD_RECONFIGURE_CHANNEL, // 命令 ConnectionManagerTask 重新配置一个通道
    CTRL_CMD_CONNECTION_CLOSED,
    // 未来可以扩展其他命令, 如 CTRL_CMD_SHUTDOWN, CTRL_CMD_STATUS_REPORT
} ManagerCtrlCmdType;

// ConnectionManagerTask 接收的控制消息结构
typedef struct {
    ManagerCtrlCmdType cmd_type;
    int                channel_index; // 要重新配置的目标通道号
} ManagerCtrlMsg;


// SerialPortTask 接收的控制命令类型
typedef enum {
    PORT_TASK_CTRL_CMD_CLOSE_ALL_FDS, // 命令 SerialPortTask 关闭其当前持有的所有 sockets
} PortTaskCtrlCmdType;
// SerialPortTask 接收的控制消息结构
typedef struct {
    PortTaskCtrlCmdType cmd_type;
    // 未来可以为其他命令增加参数
} PortTaskCtrlMsg;
/**
 * @brief TCP远程目标端点定义
 * @details 包含目标IP、目标端口和本地源端口。
 */
// TCP Client 模式下，每个目标服务器的配置
typedef struct {
    char target_ip[16];     // 目标服务器IP地址 (e.g., "192.168.1.100")
    int  target_port;       // 目标服务器端口
} TcpClientTarget;
/**
 * @brief 定义网络连接的类型，用于ConnectionManager分发
 */
typedef enum {
    CONN_TYPE_UNKNOWN,
    CONN_TYPE_REALCOM_DATA,
    CONN_TYPE_REALCOM_CMD,
    CONN_TYPE_TCPSERVER,
    CONN_TYPE_TCPCLIENT,
    CONN_TYPE_UDP,
    CONN_TYPE_GLOBAL_CONFIG,    
	
    CONN_TYPE_DATA,    // 数据通道
    
	CONN_TYPE_SET,     // 单个串口配置通道
    CONN_TYPE_SETTING  // 全局设备配置通道
} ConnectionType;

/**
 * @brief 定义单个通道的端口映射关系
 */
typedef struct {
    int channel_index;      // 串口通道号
    int data_port;          // 对应的数据端口
    int command_port;           // 对应的配置端口
    int local_tcp_port;           // 对应的本地TCP端口

    /* ---- 以下字段在运行时填充 ---- */
    int data_listen_fd;     // 数据端口的监听socket fd
    int command_listen_fd;      // 配置端口的监听socket fd
    int local_tcp_listen_fd;      // 本地TCP端口的监听socket fd
} PortMapping;

/**
 * @brief 用于在任务间传递新连接信息的消息结构体
 */
typedef struct {
    ConnectionType type;          // 连接的类型
    int            channel_index; // 通道索引 (0-15)，对于全局配置为-1
    int            client_fd;     // 新建立的socket文件描述符
} NewConnectionMsg;


/* ------------------ Global Variable Declarations (extern) ------------------ */
/*
 * 在.c文件中定义的全局变量，在此处用extern声明，以便其他文件可以访问。
 */
// 消息队列ID
extern MSG_Q_ID g_net_conn_q[NUM_PORTS];
extern MSG_Q_ID g_config_conn_q;

// 互斥锁ID
extern SEM_ID g_config_mutex;
extern SEM_ID g_stats_mutex;

// 任务ID
extern TASK_ID g_conn_manager_tid;
extern TASK_ID g_realtime_scheduler_tid;
extern TASK_ID g_config_task_manager_tid;
extern TASK_ID g_udp_search_tid;
extern TASK_ID g_report_manager_tid;
extern TASK_ID g_key_process_tid;

// 主状态数组：包含所有16个通道的状态
extern SystemConfiguration g_system_config;
	   
//extern PortMapping g_port_mappings[NUM_PORTS];

//extern void ConnectionManagerTask(void);
extern void* ConfigTaskManager(void *arg);
extern void* RealTimeSchedulerTask(void *arg);
extern STATUS start_update_server(void);
extern STATUS stop_update_server(void);
extern STATUS ConnectionManager_TaskStart(void);
extern STATUS ConnectionManager_TaskStop(void);

#endif /* APP_COMMON_H */
