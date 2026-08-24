/********************************************************************************
 * @file    app_net_con.c
 * @author  Gemini Architect & <Your Name>
 * @brief   网络连接管理器模块
 *
 * @description
 * 本模块实现了一个核心任务 ConnectionManagerTask，作为整个系统的“网络总机”。
 * 其主要职责包括：
 * - 启动时根据全局配置 g_system_config 初始化所有16个通道的网络服务。
 * - 统一监听所有 TCP Server 端口，并接受（accept）新的客户端连接。
 * - 在接受新连接前，检查并实施每个通道的最大连接数限制。
 * - 统一管理所有 TCP Client 的非阻塞连接（connect）过程。
 * - 将所有准备就绪（已连接或已绑定）的 socket 文件描述符 (fd) 通过消息队列
 * 准确地分发给对应的上层业务任务（如 RealTimeSchedulerTask）。
 * - 监听一个控制消息队列，以支持对任意通道进行动态的网络模式切换，并处理
 * 来自业务任务的连接关闭通知。
 *
 * @design_principles
 * - **单一职责**: 本任务只负责连接的“建立”、“分发”与“计数”，不参与任何业务数据的收发。
 * - **模块化**: 复杂的逻辑被分解为一系列职责清晰的 static 辅助函数。
 * - **事件驱动**: 采用 select() 模型，高效地处理网络事件和控制命令。
 * - **配置驱动**: 所有网络行为均由全局 g_system_config 结构驱动。
 ********************************************************************************/

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>        /* sockaddr_in, IPPROTO_TCP 等 */
#include <netinet/tcp.h>       /* 可选，包含 TCP 相关选项 */
#include <fcntl.h> 
#include <arpa/inet.h>

#include "app_net_con.h" // 模块自身的公共头文件
#include "app_com.h"     // 包含 SystemConfiguration, NewConnectionMsg 等核心结构

#include"system_abstraction_layer.h"

/* ================================================================================
 * 宏定义与内部数据结构
 * ================================================================================ */

// 任务配置
#define MANAGER_TASK_NAME       "tNetConnMgr"
#define MANAGER_TASK_PRIORITY   70
#define MANAGER_TASK_STACK_SIZE 8192 // 为网络操作提供充足的栈空间
#define MANAGER_CTRL_MSG_Q_SIZE 20   // 控制消息队列深度，以应对突发命令

// 内部使用的 "监听Socket映射表"，用于通过 listen_fd 快速反查其来源信息
typedef struct 
{
    int             listen_fd;
    int             channel_index;
    ConnectionType  conn_type;
    BOOL            is_in_use;
} ListenerMap;

// 内部使用的 "待连接TCP Client列表"，用于跟踪非阻塞 connect 的状态
typedef struct 
{
    int             socket_fd;
    int             channel_index;
    ConnectionType  conn_type;
    BOOL            is_in_use;
} PendingConnection;

#define MAX_LISTENERS           (16 * 4 + 1) // 16串口*2(RealCom)+1全局配置
#define MAX_PENDING_CONNECTIONS (16 * 8)     // 16串口*8(TCP Client)

/* ================================================================================
 * 模块级静态变量
 * ================================================================================ */

//static int      g_manager_task_id = 0;
static pthread_t g_manager_thread_id = 0;  // POSIX 线程 ID
static MSG_Q_ID g_manager_ctrl_q  = NULL; // 接收控制命令的消息队列

// 内部状态管理表
static ListenerMap       g_listener_map[MAX_LISTENERS];
static PendingConnection g_pending_connections[MAX_PENDING_CONNECTIONS];
int  g_active_tcp_connections[NUM_PORTS]; // 跟踪每个通道的活跃TCP连接数

// --- 外部依赖 ---
extern SystemConfiguration g_system_config;
extern MSG_Q_ID g_net_conn_q[NUM_PORTS];
extern MSG_Q_ID g_serial_port_ctrl_q[NUM_PORTS];

/* ================================================================================
 * 内部辅助函数声明
 * ================================================================================ */

void *ConnectionManagerTask(void *arg);
//static void process_control_messages(void);
static void process_network_events(fd_set* p_readfds);
static void setup_channel(int channel_index);
//static void teardown_channel(int channel_index);
static void handle_new_connections(fd_set* p_readfds);
//static void handle_pending_connections(fd_set* p_writefds);
static int  create_tcp_listener(int port);
static void add_to_listener_map(int fd, int ch_index, ConnectionType type);
static void add_to_pending_list(int fd, int ch_index, ConnectionType type);
//static void remove_from_pending_list(int fd);

/* ================================================================================
 * 公共接口函数实现
 * ================================================================================ 
 */

STATUS ConnectionManager_TaskStart(void) 
{
    g_manager_ctrl_q = msgQCreate(MANAGER_CTRL_MSG_Q_SIZE, sizeof(ManagerCtrlMsg), MSG_Q_FIFO);
    if (g_manager_ctrl_q == NULL) 
    {
        LOG_ERROR("ConnectionManager: Failed to create control message queue");
        return ERROR;
    }
    
    // 使用pthread_create 创建线程
    int ret = pthread_create(&g_manager_thread_id, NULL, (void*)ConnectionManagerTask, NULL);
    if (ret != 0)
    {
        LOG_ERROR("ConnectionManager: Failed to create thread: %s", strerror(errno));
        msgQDelete(g_manager_ctrl_q);
        g_manager_ctrl_q = NULL;
        g_manager_thread_id = 0;
        return ERROR;
    }
    //sleep(1);
    LOG_INFO("Connection Manager Task started successfully (Thread ID: %ld).\n", (long)g_manager_thread_id);
    return OK;
}

/**
 * @brief 停止并回收连接管理器线程资源
 * @return 成功返回 OK，失败返回 ERROR
 */
STATUS ConnectionManager_TaskStop(void)
{
    if (g_manager_thread_id == 0) {
        LOG_INFO("ConnectionManager: Thread not running\n");
        return ERROR;
    }
    
    LOG_INFO("ConnectionManager: Stopping thread (ID: %ld)...\n", (long)g_manager_thread_id);
    
    // 等待线程退出并回收资源
    int ret = pthread_join(g_manager_thread_id, NULL);
    if (ret != 0) {
        LOG_ERROR("ConnectionManager: Failed to join thread: %s\n", strerror(errno));
        return ERROR;
    }
    
    g_manager_thread_id = 0;
    LOG_INFO("ConnectionManager: Thread stopped and resources cleaned up.\n");
    return OK;
}

#if 0
STATUS ConnectionManager_RequestReconfigure(int channel_index) 
{
    if (channel_index < 0 || channel_index >= NUM_PORTS) return ERROR;
    if (g_manager_ctrl_q == NULL) return ERROR;
    
    ManagerCtrlMsg msg;
    msg.cmd_type = CTRL_CMD_RECONFIGURE_CHANNEL;
    msg.channel_index = channel_index;
    return msgQSend(g_manager_ctrl_q, (char*)&msg, sizeof(msg), NO_WAIT, MSG_PRI_NORMAL);
}

STATUS ConnectionManager_NotifyConnectionClosed(int channel_index) 
{
    if (channel_index < 0 || channel_index >= NUM_PORTS) return ERROR;
    if (g_manager_ctrl_q == NULL) return ERROR;
    
    ManagerCtrlMsg msg;
    msg.cmd_type = CTRL_CMD_CONNECTION_CLOSED;
    msg.channel_index = channel_index;
    return msgQSend(g_manager_ctrl_q, (char*)&msg, sizeof(msg), NO_WAIT, MSG_PRI_NORMAL);
}
#endif


/* ================================================================================
 * 任务主函数
 * ================================================================================ */

 void *ConnectionManagerTask(void *arg)
{
    (void)arg;
    int i;
    LOG_INFO("Connection Manager Task entering main loop.\n");

    int ret = pthread_setname_np(pthread_self(), "tNetConnMgr");
    if (ret != 0)
    {
        LOG_ERROR("ConnectionManager: Failed to set thread name: %s", strerror(errno));
        return NULL;
    }

    // --- 1. 初始化 ---
    memset(g_listener_map, 0, sizeof(g_listener_map));
    memset(g_pending_connections, 0, sizeof(g_pending_connections));
    memset(g_active_tcp_connections, 0, sizeof(g_active_tcp_connections));
    
    for (i = 0; i < NUM_PORTS; i++)
    {
        setup_channel(i);
    }
    
    // --- 2. 主循环 ---
    while (1) 
    {
        fd_set readfds, writefds;
        int max_fd = 0;
        struct timeval timeout = {0, 200 * 1000}; // 200ms

        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        
        for (i = 0; i < MAX_LISTENERS; i++)
        {
            if (g_listener_map[i].is_in_use) 
            {
                FD_SET(g_listener_map[i].listen_fd, &readfds);
                if (g_listener_map[i].listen_fd > max_fd)
                {
                	max_fd = g_listener_map[i].listen_fd;
                }
                	
            }
        }
        for ( i = 0; i < MAX_PENDING_CONNECTIONS; i++) 
        {
            if (g_pending_connections[i].is_in_use)
            {
                FD_SET(g_pending_connections[i].socket_fd, &writefds);
                if (g_pending_connections[i].socket_fd > max_fd) 
				{
                	max_fd = g_pending_connections[i].socket_fd;
				}
            }
        }
        
        //process_control_messages();
        
        int ret = select(max_fd + 1, &readfds, &writefds, NULL, &timeout);
        
        if (ret > 0) 
        {
            process_network_events(&readfds);
        } 
        else if (ret < 0)
        {
            perror("ConnectionManager: select() error");
            taskDelay(1);
        }
    }

    return NULL;
}

/* ================================================================================
 * 内部辅助函数实现
 * ================================================================================ */
#if 0
static void process_control_messages(void) 
{
    ManagerCtrlMsg msg;
    if (msgQReceive(g_manager_ctrl_q, (char*)&msg, sizeof(msg), NO_WAIT) == OK)
    {
        switch (msg.cmd_type) 
        {
            case CTRL_CMD_RECONFIGURE_CHANNEL:
                LOG_DEBUG("Received reconfigure command for channel %d.\n", msg.channel_index);
                teardown_channel(msg.channel_index);
                setup_channel(msg.channel_index);
                break;
            case CTRL_CMD_CONNECTION_CLOSED:
                if (g_active_tcp_connections[msg.channel_index] > 0) 
                {
                    g_active_tcp_connections[msg.channel_index]--;
                }
                break;
        }
    }
}
#endif

static void process_network_events(fd_set* p_readfds) 
{
    handle_new_connections(p_readfds);
    
    // @todo pendig untested
    //handle_pending_connections(p_writefds);
    
}

static void handle_new_connections(fd_set* p_readfds) 
{
	int i;
    for ( i = 0; i < MAX_LISTENERS; i++) 
    {
        if (g_listener_map[i].is_in_use && FD_ISSET(g_listener_map[i].listen_fd, p_readfds)) 
        {
            int channel_index = g_listener_map[i].channel_index;
            uint8_t max_connections = g_system_config.channels[channel_index].max_connections;

            if (g_active_tcp_connections[channel_index] > max_connections) 
            {
                LOG_DEBUG("Max connection limit (%d) reached for channel %d. Rejecting.\n", max_connections, channel_index);
                int temp_fd = accept(g_listener_map[i].listen_fd, NULL, NULL);
                if (temp_fd >= 0) 
                {
                    close(temp_fd);
                }
                continue;
            }

            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(g_listener_map[i].listen_fd, (struct sockaddr*)&client_addr, &addr_len);
            LOG_DEBUG("Accepted new connection (fd=%d) from %s:%d for channel %d.\n", client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), channel_index);
            if (client_fd >= 0) 
            {
            	int flag = 1;
            	setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
                
            	/* TCP_NODELAY: 禁用 Nagle 算法, 小包立即发送 */
            	flag = 1;
            	setsockopt(client_fd, IPPROTO_TCP, TCP_QUICKACK, (char*)&flag, sizeof(int));
            	/* TCP_QUICKACK: 禁用 Delayed ACK, 立即回复 ACK */
            	
            	
                g_active_tcp_connections[g_listener_map[i].channel_index]++;

                LOG_DEBUG("g_active_tcp_connections[%d] = %d\n", g_listener_map[i].channel_index, g_active_tcp_connections[g_listener_map[i].channel_index]);
                
                NewConnectionMsg msg;
                msg.client_fd = client_fd;
                msg.channel_index = g_listener_map[i].channel_index;
                msg.type = g_listener_map[i].conn_type;

                // printf("msg.type = %d\n", msg.type);
                // printf("msg.channel_index = %d\n", msg.channel_index);
                // printf("msg.client_fd = %d\n", msg.client_fd);
                
                // 根据 conn_type 决定派发到哪个队列 ---
                switch (msg.type)
                {
                    case CONN_TYPE_REALCOM_CMD:
                    {
                        LOG_DEBUG("Dispatching CMD connection (fd=%d) to ConfigTaskManager.\n", client_fd);
                        if (msgQSend(g_config_conn_q, (char*)&msg, sizeof(msg), NO_WAIT, 0) != OK)
                        {
                            LOG_ERROR("Failed to dispatch CMD fd=%d. Closing.\n", client_fd);
                            close(client_fd);
                            g_active_tcp_connections[msg.channel_index]--; 
                        }
                    }
                    break;

                    case CONN_TYPE_SETTING:
                    {
                        LOG_DEBUG("Dispatching SETTING connection (fd=%d) to ConfigTaskManager.\n", client_fd);
                        if (msgQSend(g_config_conn_q, (char*)&msg, sizeof(msg), NO_WAIT, 0) != OK)
                        {
                            LOG_ERROR("Failed to dispatch SETTING fd=%d. Closing.\n", client_fd);
                            close(client_fd);
                            g_active_tcp_connections[msg.channel_index]--; 
                        }
                    }
                    break;

                    default:
                    {
                        int ret;
                        LOG_DEBUG("Dispatching DATA connection (fd=%d) to NetSchedulerTask channel %d.\n", client_fd, msg.channel_index);
                        if ( (ret = msgQSend(g_net_conn_q[msg.channel_index], (char*)&msg, sizeof(msg), NO_WAIT, 0)) != OK) 
                        {
                            LOG_ERROR("Failed to dispatch DATA fd=%d. ret=%d. sizeof(msg)=%d Closing.\n", client_fd, ret, sizeof(msg));
                            close(client_fd);
                            g_active_tcp_connections[msg.channel_index]--;
                        }
                    }
                    break;
                }

            } 
            else 
            {
                 LOG_ERROR("accept() failed");
            }
        }
    }
}

#if 0
static void handle_pending_connections(fd_set* p_writefds) 
{
	int i;
    PendingConnection temp_pending[MAX_PENDING_CONNECTIONS];
    memcpy(temp_pending, g_pending_connections, sizeof(temp_pending));

    for ( i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
        if (temp_pending[i].is_in_use && FD_ISSET(temp_pending[i].socket_fd, p_writefds)) {
            int err = 0, len = sizeof(err), fd = temp_pending[i].socket_fd;
            remove_from_pending_list(fd);
            
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len) == 0 && err == 0) {
                LOG_DEBUG("TCP Client (fd=%d) connected for channel %d.\n", fd, temp_pending[i].channel_index);
                NewConnectionMsg msg;
                msg.client_fd = fd;
                msg.channel_index = temp_pending[i].channel_index;
                msg.type = temp_pending[i].conn_type;
                    
                if (msgQSend(g_net_conn_q[msg.channel_index], (char*)&msg, sizeof(msg), NO_WAIT, 0) != OK) {
                     LOG_ERROR("Failed to dispatch connected fd=%d. Closing.\n", fd);
                     close(fd);
                }
            } else {
                LOG_ERROR("TCP Client (fd=%d) failed for channel %d: %s\n", fd, temp_pending[i].channel_index, strerror(err));
                close(fd);
            }
        }
    }
}
#endif

#if 0
static void teardown_channel(int channel_index) 
{
	int i;
    PortTaskCtrlMsg msg = {PORT_TASK_CTRL_CMD_CLOSE_ALL_FDS};
    msgQSend(g_serial_port_ctrl_q[channel_index], (char*)&msg, sizeof(msg), NO_WAIT, 0);
    
    for ( i = 0; i < MAX_LISTENERS; i++) {
        if (g_listener_map[i].is_in_use && g_listener_map[i].channel_index == channel_index) {
            close(g_listener_map[i].listen_fd);
            g_listener_map[i].is_in_use = FALSE;
        }
    }
    for ( i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
        if (g_pending_connections[i].is_in_use && g_pending_connections[i].channel_index == channel_index) {
            close(g_pending_connections[i].socket_fd);
            g_pending_connections[i].is_in_use = FALSE;
        }
    }
    g_active_tcp_connections[channel_index] = 0;
    LOG_DEBUG("Network resources for channel %d torn down.\n", channel_index);
}
#endif

static void setup_channel(int channel_index) 
{
    ChannelState* cfg = &g_system_config.channels[channel_index];
    
    //LOG_INFO("op_mode: %d\n", cfg->op_mode);
    
    switch (cfg->op_mode) 
    {   	
        case OP_MODE_REAL_COM: 
        {
            int data_fd = create_tcp_listener(cfg->data_port);
            if (data_fd >= 0)
            {
            	add_to_listener_map(data_fd, channel_index, CONN_TYPE_REALCOM_DATA);
            }
            if (cfg->command_port > 0) 
            {
                 int cmd_fd = create_tcp_listener(cfg->command_port);
                 if (cmd_fd >= 0)
                 {                	               
                	 add_to_listener_map(cmd_fd, channel_index, CONN_TYPE_REALCOM_CMD);
                 }
            }
            break;
        }
        
        case OP_MODE_TCP_SERVER:
        {
            int data_fd = create_tcp_listener(cfg->local_tcp_port);
            if (data_fd >= 0)
            {
            	add_to_listener_map(data_fd, channel_index, CONN_TYPE_TCPSERVER);
            }
            if (cfg->command_port > 0)
            {
                 int cmd_fd = create_tcp_listener(cfg->command_port);
                 if (cmd_fd >= 0) 
                 {
                	 add_to_listener_map(cmd_fd, channel_index, CONN_TYPE_REALCOM_CMD);
                 }
            }
            break;
        }
        
        case OP_MODE_TCP_CLIENT:
        {
        	int j;
            for (j = 0; j < 4; j++) {
                if (cfg->tcp_destinations[j].destination_ip == 0 || cfg->tcp_destinations[j].destination_port == 0)
                {
                	continue;
                }
                int client_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (client_fd < 0) 
				{
					continue;
				}
                
                fcntl(client_fd, F_SETFL, O_NONBLOCK);
                int one = 1;
                setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(int));
                setsockopt(client_fd, IPPROTO_TCP, TCP_QUICKACK, (char*)&one, sizeof(int));
                
                struct sockaddr_in server_addr = {0};
                server_addr.sin_family = AF_INET;
                server_addr.sin_port = htons(cfg->tcp_destinations[j].destination_port);
                server_addr.sin_addr.s_addr = cfg->tcp_destinations[j].destination_ip;
                
                if (connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
                {
                    if (errno == EINPROGRESS) 
                    {
                        add_to_pending_list(client_fd, channel_index, CONN_TYPE_TCPCLIENT);
                    } 
                    else
                    {
                        perror("connect() error");
                        close(client_fd);
                    }
                }
                else 
                {
                     NewConnectionMsg msg;
                     msg.client_fd = client_fd;
                     msg.channel_index = channel_index;
                     msg.type = CONN_TYPE_TCPCLIENT;
                     msgQSend(g_net_conn_q[channel_index], (char*)&msg, sizeof(msg), NO_WAIT, 0);
                }
            }
            break;
        }
        
        case OP_MODE_UDP: 
        {
            int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (udp_fd < 0) break;
            
            struct sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port = htons(cfg->udp_destinations[0].port);
            
            if (bind(udp_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) 
            {
                NewConnectionMsg msg;
                msg.client_fd = udp_fd;
                msg.channel_index = channel_index;
                msg.type = CONN_TYPE_UDP;
                msgQSend(g_net_conn_q[channel_index], (char*)&msg, sizeof(msg), NO_WAIT, 0);
            } 
            else 
            {
                perror("UDP bind failed");
                close(udp_fd);
            }
            break;
        }
        
        default:
        {
        	break;
        }
    }
    LOG_DEBUG("Network resources for channel %d set up for mode %d.\n", channel_index, cfg->op_mode);
}

static int create_tcp_listener(int port) 
{
    //LOG_INFO("port: %d\n", port);
	if (port == 0) 
	{
		return ERROR;		
	}
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) 
    {
    	printf("listen_fd: %d\n", listen_fd);
    	perror("socket()"); 
    	return ERROR; 
    }
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
    {
        perror("bind()"); 
        close(listen_fd); 
        return ERROR;
    }
    if (listen(listen_fd, 20) < 0) 
    {
        perror("listen()");
        close(listen_fd); 
        return ERROR;
    }
   int flags = fcntl(listen_fd, F_GETFL, 0);
   if(flags == -1)
   {
   	perror("fcntl F_GETFL failed");
   }
   int ret = fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
   if(ret ==  -1)
   {
   	perror("fcntl F_SETFL failed");
   }
    
    
    return listen_fd;
}

static void add_to_listener_map(int fd, int ch_index, ConnectionType type)
{
	int i;
    for ( i = 0; i < MAX_LISTENERS; i++)
    {
        if (!g_listener_map[i].is_in_use)
        {
            g_listener_map[i].listen_fd = fd;
            g_listener_map[i].channel_index = ch_index;
            g_listener_map[i].conn_type = type;
            g_listener_map[i].is_in_use = TRUE;
            return;
        }
    }
    
    LOG_ERROR("Listener map is full! Cannot add fd %d.\n", fd);
}

static void add_to_pending_list(int fd, int ch_index, ConnectionType type) 
{
	int i;
    for ( i = 0; i < MAX_PENDING_CONNECTIONS; i++)
    {
        if (!g_pending_connections[i].is_in_use) 
        {
            g_listener_map[i].listen_fd = fd;
            g_listener_map[i].channel_index = ch_index;
            g_listener_map[i].conn_type = type;
            g_listener_map[i].is_in_use = TRUE;
            return;
        }
    }
    LOG_ERROR("Pending connections list is full! Cannot add fd %d.\n", fd);
}

#if 0
static void remove_from_pending_list(int fd) 
{
	int i;
    for ( i = 0; i < MAX_PENDING_CONNECTIONS; i++) {
        if (g_pending_connections[i].is_in_use && g_pending_connections[i].socket_fd == fd) {
            g_pending_connections[i].is_in_use = FALSE;
            return;
        }
    }
}
#endif
