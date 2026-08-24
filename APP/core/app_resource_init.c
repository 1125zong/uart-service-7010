/*
 * =====================================================================================
 *
 * Filename:  app_resource_init.c
 *
 * Description:  应用程序资源初始化模块
 * 负责初始化全局资源（消息队列、互斥锁、状态变量）
 *
 * =====================================================================================
 */

#include "app_com.h"
#include "app_init_modules.h"

/* ------------------ Global Variable Definitions ------------------ */
MSG_Q_ID g_config_conn_q;
MSG_Q_ID g_net_conn_q[NUM_PORTS];
MSG_Q_ID g_serial_port_ctrl_q[NUM_PORTS];

SEM_ID g_config_mutex;

// 任务ID变量定义
TASK_ID g_conn_manager_tid;
TASK_ID g_realtime_scheduler_tid;
TASK_ID g_config_task_manager_tid;
TASK_ID g_udp_search_tid;
TASK_ID g_report_manager_tid;
TASK_ID g_key_process_tid;  // 按键处理任务ID

/* ------------------ Function Implementations ------------------ */

/**
 * @brief 初始化全局资源（消息队列和互斥锁）
 * @return 成功返回OK，失败返回ERROR
 */
STATUS init_global_resources(void) {
    int i;
    
    // 创建网络连接消息队列
    for (i = 0; i < NUM_PORTS; i++)
    {
        g_net_conn_q[i] = msgQCreate(MAX_MSG_IN_Q, sizeof(NewConnectionMsg), MSG_Q_FIFO);
        if (g_net_conn_q[i] == NULL) {
            LOG_ERROR("Failed to create message queue for channel %d\n", i);
            return ERROR;
        }
    }
    
    // 创建配置连接消息队列
    g_config_conn_q = msgQCreate(DATA_QUEUE_CAPACITY, sizeof(NewConnectionMsg), MSG_Q_FIFO);
    if (g_config_conn_q == NULL)
    {
        LOG_ERROR("FATAL: Failed to create config message queue.\n");
        return ERROR;
    }
    
    // 创建配置互斥锁 
    g_config_mutex = semMCreate(SEM_Q_PRIORITY | SEM_INVERSION_SAFE);
    if (g_config_mutex == NULL)
    {
        LOG_ERROR("FATAL: Failed to create config mutex.\n");
        return ERROR;
    }
    
    return OK;
}

/**
 * @brief 初始化通道状态
 */
void init_channel_states(void) {
    int i, j;
    
    for (i = 0; i < NUM_PORTS; i++)
    {
        g_system_config.channels[i].uart_state = UART_STATE_CLOSED;
        for(j = 0; j < MAX_CLIENTS_PER_CHANNEL; j++) 
        {
            g_system_config.channels[i].data_net_info.client_fds[j] = -1;    
        }
        
        // 初始化环形缓冲区
        ring_buffer_init(&g_system_config.channels[i].buffer_net,  g_system_config.channels[i].net_buffer_mem,  RING_BUFFER_SIZE);
        ring_buffer_init(&g_system_config.channels[i].buffer_uart, g_system_config.channels[i].uart_buffer_mem, RING_BUFFER_SIZE);
    }
}
