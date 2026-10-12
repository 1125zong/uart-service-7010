/*
 * =====================================================================================
 *
 * Filename:  app_net_scheduler.c
 *
 * Description:  实现 NetworkSchedulerTask 任务。
 * 该任务负责处理所有与网络套接字相关的非阻塞I/O操作，
 * 包括接收新连接、从客户端接收数据以及向客户端发送数据。
 *
 * =====================================================================================
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include "app_com.h"

#ifndef TX_CHUNK_SIZE
#define TX_CHUNK_SIZE (UART_HW_FIFO_SIZE / 2)
#endif

/* ------------------ Private Function Prototypes ------------------ */
void check_for_new_connections(void);
void run_net_recv(void);
void run_net_send(void);
static void cleanup_data_connection(int channel_index,int client_index_in_array);

static int is_locked_port_client_allowed(int channel_index, int client_fd);
extern int g_active_tcp_connections[NUM_PORTS];

///////////////////////////////////////////
extern struct timespec start, end, diff; // 声明时间变量

static struct timespec t_loopback_start, t_loopback_end;

extern struct timespec diff_timespec(struct timespec start, struct timespec end);

////////////////////////////////////////////

/**
 * @brief 网络调度任务的主入口函数
 */
void NetworkSchedulerTask(void) 
{
	// 检查并接管来自ConnectionManager的新数据连接
	check_for_new_connections();

	// 处理所有活跃数据通道的网络接收

    //clock_gettime(CLOCK_MONOTONIC, &start);
	run_net_recv();
	// 处理所有活跃数据通道的网络发送
	run_net_send();

    //clock_gettime(CLOCK_MONOTONIC, &end);

    //计算时间差
    //diff = diff_timespec(start, end);

    //打印时间差
    //printf("执行时间: %ld 秒 %ld 毫秒 %ld 微秒\n",diff.tv_sec,diff.tv_nsec / 1000000,    diff.tv_nsec / 1000);   // 微秒



}

/**
 * @brief (非阻塞)检查所有通道的新连接消息队列，并将新的fd分类存放到正确的管理结构中
 *
 * @description
 * 此函数被设计为在主调度循环中被频繁调用。它会轮询所有16个通道的消息队列
 * (g_net_conn_q[0] 到 g_net_conn_q[15])。
 * 当接收到一个由 ConnectionManagerTask 分发来的 NewConnectionMsg 时，它会：
 * 1. 根据 msg.channel_index 定位到对应的全局配置 ChannelState。
 * 2. 根据 msg.conn_type 判断连接的类型（如：普通数据、RealCOM数据、RealCOM命令等）。
 * 3. 将新的 client_fd 存入 ChannelState 中对应的 NetInfo 结构体
 * (data_net_info) 的 client_fds 数组中。
 * 4. 更新客户端计数器 (num_clients)，并处理连接数已满的情况。
 * 5. 使用互斥信号量 g_config_mutex 保证对共享配置 g_system_config 的原子操作。
 */
void check_for_new_connections(void) 
{
    NewConnectionMsg msg;
    int i; // 用于遍历通道
    // 1. 遍历所有通道的消息队列
    for (i = 0; i < NUM_PORTS; i++) 
    {
        // 使用 while 循环来排空当前通道队列中的所有消息
        if (msgQReceive(g_net_conn_q[i], (char*) &msg, sizeof(msg), NO_WAIT) == sizeof(NewConnectionMsg))
        {
            // 验证消息的合法性
            if ( msg.channel_index != i ) 
            {
                LOG_ERROR("NetScheduler: Mismatched channel index in message! Queue_idx=%d, msg.ch_idx=%d. Closing fd=%d\n",i, msg.channel_index, msg.client_fd);
                close(msg.client_fd);
                continue; // 处理下一条消息
            }

            ChannelState* channel = &g_system_config.channels[i];

            /* 端口锁定时，只允许锁定该端口的PC建立数据连接 */
            if ((msg.type == CONN_TYPE_REALCOM_DATA || msg.type == CONN_TYPE_TCPSERVER) && !is_locked_port_client_allowed(i, msg.client_fd))
            {
                struct sockaddr_in peer_addr;
                socklen_t peer_len = sizeof(peer_addr);

                memset(&peer_addr, 0, sizeof(peer_addr));

                if (getpeername(msg.client_fd, (struct sockaddr *)&peer_addr, &peer_len) == 0)
                {
                    LOG_WARN("Reject client %s: port %d is locked by another PC\n", inet_ntoa(peer_addr.sin_addr), i + 1);
                }
                else
                {
                    LOG_WARN("Reject client: port %d is locked by another PC\n", i + 1);
                }

                shutdown(msg.client_fd, SHUT_RDWR);
                close(msg.client_fd);

                if (g_active_tcp_connections[i] > 0)
                {
                    g_active_tcp_connections[i]--;
                }
                continue;
            } 


            // 2. 根据连接类型，选择正确的 NetInfo 结构体来存放 fd
            switch (msg.type)
            {
				case CONN_TYPE_TCPSERVER:
				case CONN_TYPE_REALCOM_DATA:
					channel->data_net_info.state = NET_STATE_CONNECTED;
					channel->data_net_info.client_fds[channel->data_net_info.num_clients] = msg.client_fd;
					channel->data_net_info.num_clients++;
					channel->is_connected = PORT_CONNECTD;

					/* TCP Server: 客户端连接时自动初始化串口(若未初始化) */
					if (msg.type == CONN_TYPE_TCPSERVER && channel->uart_state != UART_STATE_OPENED)
					{
						usart_info_t uart_cfg;
						memset(&uart_cfg, 0, sizeof(uart_cfg));
						uart_cfg.baud_rate = channel->baudrate;
						uart_cfg.data_bit   = channel->data_bits;
						uart_cfg.stop_bit   = channel->stop_bits;
						uart_cfg.parity     = (channel->parity == 2) ? 2 : (channel->parity != 0);
						axi165502CInit(&uart_cfg, i);
						channel->uart_state = UART_STATE_OPENED;
						LOG_INFO("NetScheduler: Ch %d UART auto-init (baud=%d)\n",
								 i, channel->baudrate);
					}
				break;

                case CONN_TYPE_TCPCLIENT:
				case CONN_TYPE_UDP:

				break;
                
                default:
                    LOG_ERROR("NetScheduler: Ch %d received unknown conn_type %d. Closing fd=%d\n",i, msg.type, msg.client_fd);
                    close(msg.client_fd);
                    continue; // 跳过此无效消息
            }
			
			LOG_DEBUG("NetScheduler: Ch %d accepted new connection fd=%d, type=%d. Total clients: %d (Data).\n",
					 i, msg.client_fd, msg.type,
					 channel->data_net_info.num_clients);
        }
    }
    // (处理全局配置连接队列 g_config_conn_q 的逻辑可以放在这里，如果需要的话)
}

/*
 * =====================================================================================
 *
 * 原有 `run_net_recv` 的问题:
 * 1. 在 `run_net_recv` 中，`recv()` 是在一个循环中被无条件调用的。
 * 2. 如果 `client_fd` 是阻塞模式 (默认)，`recv()` 会阻塞整个实时任务，导致
 * 上游的 `handle_serial_rx` 无法执行，引发UART FIFO溢出和数据丢失。
 *
 * 修改方案:
 * 1. **依赖非阻塞Socket**: 我们依赖上一步在 `app_net_con.c` 中所做的修改，
 * 即所有 `accept()` 的 `fd` 都已被设为 `O_NONBLOCK`。
 * 2. **引入 `select()` 检测可读性**:
 * - 在 `recv()` 之前，我们构建一个包含所有客户端 `fd` 的 `readfds` 集合。
 * - 使用 `select()` (超时设为0) 立即返回哪些 `fd` 真正有数据可读。
 * 3. **按需 `recv()`**:
 * - 只有 `FD_ISSET(fd, &readfds)` 为真的 `fd`，我们才对其调用 `recv()`。
 *
 * 优点:
 * - **杜绝阻塞**: 彻底解决了 `recv()` 阻塞实时任务的风险。
 * - **极高效率**: `select()` 是一次系统调用，远快于循环中 N 次无效的 `recv()`
 * (并检查 EWOULDBLOCK)。CPU只处理真正有数据的socket。
 * - **代码一致性**: 此逻辑与我们修改后的 `run_net_send` 保持一致。
 *
 * =====================================================================================
 */
#define TX_NET_SIZE (4096) 
unsigned char temp_buffer_net_rx[TX_NET_SIZE];

void run_net_recv(void) 
{
    int i, j;
    fd_set readfds; // 用于 select 的可读文件描述符集合
    int max_fd = 0;
    int has_fds = 0;
    struct timeval timeout = {0, 0}; // 完全非阻塞的 select

    FD_ZERO(&readfds);

    // 1. 构建 fd_set，包含所有通道的所有活动客户端
    for (i = 0; i < NUM_PORTS; i++)
    {
        ChannelState* channel = &g_system_config.channels[i];
        for (j = channel->data_net_info.num_clients - 1; j >= 0; j--)
        {
            int fd = channel->data_net_info.client_fds[j];
            if (fd >= 0)
            {
                FD_SET(fd, &readfds);
                if (fd > max_fd) 
                {
                    max_fd = fd;
                }
                has_fds = 1;
            }
        }
    }

    if (!has_fds) 
    {
        return; // 没有任何活动的客户端，直接返回
    }

    // 2. 非阻塞地轮询哪些 fd 准备好了
    int ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

    if (ret < 0)
    {
        LOG_ERROR("NetScheduler: select() error in run_net_recv, errno=%d\n", errno);
        return;
    }
    if (ret == 0)
    {
        return; // 没有任何 fd 可读
    }

    // 3. 仅对 select() 报告为可读的 fd 执行 recv()
    for (i = 0; i < NUM_PORTS; i++) //NUM_PORTS
    {
        ChannelState* channel = &g_system_config.channels[i];
        for (j = channel->data_net_info.num_clients - 1; j >= 0; j--)
        {
            int fd = channel->data_net_info.client_fds[j];
            
            // 检查这个 fd 是否在可读集合中
            if (fd >= 0 && FD_ISSET(fd, &readfds)) 
            {
                
                // (由于 select() 保证了可读性，此处的 recv() 不会阻塞)
                int n = recv(fd, (char*) temp_buffer_net_rx, sizeof(temp_buffer_net_rx), 0);
                //printf("waiting for data to recv...\n");
                if (n > 0)
                {
                    // 记录环回测试起始时间
                    //clock_gettime(CLOCK_MONOTONIC, &t_loopback_start);
                    // 成功读取数据
                    channel->tx_net += n;



                    /******    临时添加    *****/
                    LOG_DEBUG("[NET RX] Port=%d, len=%d, data=%02X\n", i + 1, n, (unsigned char)temp_buffer_net_rx[0]);
                    /****/


                    //LOG_DEBUG("Data: NetScheduler: Ch %d received %d bytes on fd %d.\n", i, n, fd);
                    //printf("Data: NetScheduler: Ch %d received %d bytes on fd %d.\n", i, n, fd);
                    // 打印接收到的网络数据
                    //LOG_DEBUG("netRecvData: %s\n", temp_buffer_net_rx);

                    ring_buffer_queue_arr(&channel->buffer_net, (char*) temp_buffer_net_rx, n);
                } 
                else if (n == 0)
                {
                    // 客户端主动关闭 (TCP FIN)
                    cleanup_data_connection(i, j);
                } 
                else 
                {
                    // n < 0
                    // 理论上，因为我们用了 select()，不应再收到 EWOULDBLOCK。
                    // 但为健壮性，我们仍然检查：忽略 EWOULDBLOCK，其他视为错误。
                    if (errno != EWOULDBLOCK && errno != EAGAIN) 
                    {
                        // 发生真实错误 (如 RST)
                        cleanup_data_connection(i, j);
                    }
                }
            }
        }
    }
}



unsigned char temp_buffer_net_tx[TX_NET_SIZE]; // 使用更小的块大小

/**
 * @brief 对所有活跃的数据通道执行非阻塞send
 * @details 从“串口到网络”的环形缓冲区读取数据，并发送给所有连接的客户端。
 */

void run_net_send(void)
{
    int i, j;
    fd_set writefds;
    int max_fd = 0;
    int has_fds = 0;
    int has_data[NUM_PORTS];
    struct timeval timeout = {0, 0}; // 完全非阻塞的 select

    FD_ZERO(&writefds);

    // 1. 构建全局 fd_set，收集所有有数据要发的通道的可写 fd
    for (i = 0; i < NUM_PORTS; i++)
    {
        ChannelState* channel = &g_system_config.channels[i];

        if (channel->data_net_info.num_clients == 0 || ring_buffer_is_empty(&channel->buffer_uart))
        {
            has_data[i] = 0;
            continue;
        }

        has_data[i] = 1;

        for (j = 0; j < channel->data_net_info.num_clients; j++) 
        {
            int fd = channel->data_net_info.client_fds[j];
            if (fd >= 0)
            {
                FD_SET(fd, &writefds);
                if (fd > max_fd) 
                {
                    max_fd = fd;
                }
                has_fds = 1;
            }
        }
    }

    if (!has_fds) 
        return;

    // 2. 一次性非阻塞地检查所有写集合
    int ret = select(max_fd + 1, NULL, &writefds, NULL, &timeout);

    if (ret <= 0)
        return; // 没有任何 fd 可写 (ret=0) 或 select 错误 (ret<0)

    // 3. 仅对有 fd 可写的通道，从缓冲区取数据并发送
    for (i = 0; i < NUM_PORTS; i++) //NUM_PORTS
    {
        if (!has_data[i])
            continue;

        ChannelState* channel = &g_system_config.channels[i];

        // 检查该通道是否有 fd 在可写集合中
        int any_writable = 0;
        for (j = 0; j < channel->data_net_info.num_clients; j++)
        {
            int fd = channel->data_net_info.client_fds[j];
            if (fd >= 0 && FD_ISSET(fd, &writefds))
            {
                any_writable = 1;
                break;
            }
        }

        if (!any_writable)
            continue;

        // 从环形缓冲区读取一小块数据
        unsigned int bytes_to_send = ring_buffer_dequeue_arr(&channel->buffer_uart, (char*)temp_buffer_net_tx, TX_NET_SIZE);

        if (bytes_to_send > 0)
        {
            channel->rx_net += bytes_to_send;

            // 将数据发送给所有可写的客户端
            for (j = channel->data_net_info.num_clients - 1; j >= 0; j--) 
            {
                int fd = channel->data_net_info.client_fds[j];

                if (fd >= 0 && FD_ISSET(fd, &writefds))
                {
                    int sent = send(fd, (char*)temp_buffer_net_tx, bytes_to_send, 0);
                    // 可删除
channel->tcp_send_requested_bytes += bytes_to_send;

if (sent > 0)
{
    channel->tcp_send_actual_bytes +=
        (unsigned int)sent;

    if ((unsigned int)sent < bytes_to_send)
    {
        channel->tcp_short_write_count++;

        channel->tcp_unsent_bytes +=
            bytes_to_send - (unsigned int)sent;
    }
}
else if (sent == 0)
{
    channel->tcp_short_write_count++;
    channel->tcp_unsent_bytes += bytes_to_send;
}
else
{
    channel->tcp_unsent_bytes += bytes_to_send;

    if (errno == EWOULDBLOCK ||
        errno == EAGAIN)
    {
        channel->tcp_send_eagain_count++;
    }
    else
    {
        channel->tcp_send_error_count++;
    }
}
/*** */
                    // if (sent > 0)
                    // {
                        // 环回测试：计算并打印延迟
                        //clock_gettime(CLOCK_MONOTONIC, &t_loopback_end);
                        //struct timespec diff = diff_timespec(t_loopback_start, t_loopback_end);
                        //unsigned long us = diff.tv_sec * 1000000 + diff.tv_nsec / 1000;
                        //printf("==== loopback delay: %ld us ====\n", us);
                    // }
                    if (sent < 0) 
                    {
                        if (errno != EWOULDBLOCK && errno != EAGAIN)
                        {
                            cleanup_data_connection(i, j);
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief 清理一个已断开的数据连接
 */
static void cleanup_data_connection(int channel_index, int client_index_in_array)
{
	semTake(g_config_mutex, WAIT_FOREVER);
	ChannelState* channel = &g_system_config.channels[channel_index];
	if (client_index_in_array < 0 || client_index_in_array >= channel->data_net_info.num_clients)
	{
		semGive(g_config_mutex);
		return;
	}

	int fd_to_close = channel->data_net_info.client_fds[client_index_in_array];
	close(fd_to_close);

	int last_index = channel->data_net_info.num_clients - 1;
	if (client_index_in_array != last_index) 
	{
		channel->data_net_info.client_fds[client_index_in_array] = channel->data_net_info.client_fds[last_index];
	}
	channel->data_net_info.client_fds[last_index] = -1;
	channel->data_net_info.num_clients--;

	// 更新全局TCP连接计数
	extern int g_active_tcp_connections[NUM_PORTS];
	if (g_active_tcp_connections[channel_index] > 0) 
	{
		g_active_tcp_connections[channel_index]--;
		LOG_DEBUG("NetScheduler: Ch %d g_active_tcp_connections decreased to %d\n", 
		          channel_index, g_active_tcp_connections[channel_index]);
	}

	if (channel->data_net_info.num_clients == 0) 
	{
		// 最后一个客户端断开，状态从 CONNECTED 变回 LISTENING
		channel->data_net_info.state = NET_STATE_LISTENING;
		channel->uart_state = UART_STATE_CLOSED;
		// channel->tx_net = 0;
		// channel->rx_net = 0;
		// channel->rx_count = 0;
		// channel->tx_count = 0;
        
        // 可删除---删除前记得解开下面两行代码的注释
		// ring_buffer_init(&channel->buffer_net,  channel->net_buffer_mem,  RING_BUFFER_SIZE);
		// ring_buffer_init(&channel->buffer_uart, channel->uart_buffer_mem, RING_BUFFER_SIZE);
        ring_buffer_reset(&channel->buffer_net);
        ring_buffer_reset(&channel->buffer_uart);
        /**/

		/* 检查命令通道是否也无连接 */
		if (channel->cmd_net_info.num_clients == 0) {
			channel->is_connected = PORT_CONNECT_NONE;
		}
		LOG_INFO("NetScheduler: Ch %d has no clients left. State -> LISTENING.\n", channel_index);
	}
	semGive(g_config_mutex);
}


/**
 * @brief 用于独立测试网络调度器的任务入口函数
 * @details
 * 这个任务会以固定的频率循环执行网络调度的三个核心功能：
 * 1. 检查新连接 (check_for_new_connections)
 * 2. 处理网络数据接收 (run_net_recv)
 * 3. 处理网络数据发送 (run_net_send)
 * 任务末尾会有一个 taskDelay，以让出CPU给其他任务。
 */
void NetSchedulerTestTask_Entry(void)
{
    LOG_INFO("----> Starting independent Network Scheduler Test Task (tNetSchedTest)...\\n", 0,0,0,0,0,0);
    printf("NetSchedulerTestTask_Entry... \r\n");
    taskDelay(1);

    for (;;)
    {
        // 调用原 NetworkSchedulerTask 的核心功能
        NetworkSchedulerTask();

        // 延时，让出 CPU，控制任务执行频率
        //taskDelay(sysClkRateGet()/800); // 大约每 1.25ms 执行一次
        taskDelay(1); // 大约每 1.25ms 执行一次
    }
}


static int is_locked_port_client_allowed(int channel_index, int client_fd)
{
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    unsigned int owner_ip;
    unsigned char locked;

    if (channel_index < 0 || channel_index >= NUM_PORTS)
    {
        return 0;
    }

    if (semTake(g_config_mutex, WAIT_FOREVER) != OK)
    {
        return 0;
    }

    locked = g_system_config.device.interface_lock_status[channel_index];
    owner_ip = g_system_config.channels[channel_index].locked_client_ip[0];

    semGive(g_config_mutex);

    if (locked != INTERFACE_LOCK_STATE_LOCKED)
    {
        return 1;
    }
    memset(&peer_addr, 0, sizeof(peer_addr));

    if (getpeername(client_fd, (struct sockaddr *)&peer_addr, &peer_len) != 0)
    {
        return 0;
    }

    return peer_addr.sin_addr.s_addr == owner_ip;
}
