/*
 * =====================================================================================
 *
 * Filename:  app_report.c
 *
 * Description:  设置信息和上报信息任务实现
 * 负责创建TCP服务端(端口5000)，处理客户端连接，
 * 接收设置命令和发送设备上报信息。
 *
 * =====================================================================================
 */

#include "app_com.h"
#include "app_report.h"
#include "app_report_basic.h"
#include "app_report_network.h"
#include "app_report_serial.h"
#include "app_report_mode.h"
#include "app_report_lock.h"
#include "app_report_status.h"
//#include <corecrt_search.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <errno.h>

#define LISTEN_BACKLOG 10


/* 静态变量 */
static int g_report_listen_fd = -1;
ReportClientSession g_report_clients[MAX_REPORT_CLIENTS];

/* 私有函数声明 */
static int setup_report_listening_socket(int port);
static void set_tcp_keepalive(int sock_fd, int keepidle, int keepintl, int keepcnt);
static int handle_report_client_data(ReportClientSession *session);
static void process_report_command(ReportClientSession *session, unsigned char *data, int len);
void send_report_response(int client_fd, const unsigned char main_id,  unsigned char sub_id, unsigned char status);


/**
 * @brief 处理上报命令
 * @details 根据协议格式解析命令：[A5 5A] [功能ID] [子ID(可选)] [数据...] [5A 5A]
 */
static void process_report_command(ReportClientSession *session, unsigned char *data, int len)
{
    LOG_DEBUG("ReportManagerTask: Received %d bytes from fd=%d\n", len, session->fd);

    /* 检查最小帧长度：帧头(2) + 功能ID(1) + 帧尾(2) = 5字节 */
    if (len < 5) 
    {
        LOG_WARN("ReportManagerTask: Frame too short (len=%d)\n", len);
        return;
    }

    /* 验证帧头 */
    if (data[0] != 0xA5 || data[1] != 0xA5) 
    {
        LOG_WARN("ReportManagerTask: Invalid frame header: %02X %02X\n", data[0], data[1]);
        return;
    }

    /* 验证帧尾 */
    if (data[len-2] != 0x5A || data[len-1] != 0x5A)
    {
        LOG_WARN("ReportManagerTask: Invalid frame tail: %02X %02X\n", data[len-2], data[len-1]);
        return;
    }

    /* 解析功能ID */
    unsigned char func_id = data[2];
    
    LOG_INFO("ReportManagerTask: Processing command: FuncID=0x%02X\n", func_id);

    /* 根据功能ID分发到对应的处理函数 */
    switch (func_id)
    {
        case 0x01:
            handle_device_overview(session->fd, data, len);
            break;
        case 0x02:
            handle_device_name_operation(session->fd, data, len);
            break;
        case 0x03:
            handle_network_settings(session->fd, data, len);
            break;
        case 0x04:
            handle_serial_port_settings(session->fd, data, len);
            break;
        case 0x05:
            handle_serial_port_mode(session->fd, data, len);
            break;
        case 0x06:
            handle_read_operating_mode(session->fd, data, len);
            break;
        //状态监控
        case 0x07:
            handle_device_status_monitoring(session->fd, data, len);
            break;

        /* 设备管理功能 */
        case 0x08:  
        	handle_device_management(session->fd, data, len);
        	break;
        default:
            LOG_WARN("  Unknown function ID: 0x%02X\n", func_id);
            {
                send_report_response(session->fd, func_id, 0x00, FAILED);
            }
            break;
    }
}


/**
 * @brief 处理客户端接收到的数据
 */
static int handle_report_client_data(ReportClientSession *session)
{
    int nbytes;
    int ret;
    unsigned char temp_buffer[1024];

    /* 从socket读取数据 */
    nbytes = recv(session->fd, temp_buffer, sizeof(temp_buffer), 0);
    LOG_INFO("ReportManagerTask: Received %d bytes from fd=%d\n", nbytes, session->fd);
    
    if (nbytes <= 0) 
    {
        /* 连接关闭或出错 */
        if (nbytes == 0)
        {
            LOG_INFO("ReportManagerTask: Client disconnected (fd=%d)\n", session->fd);
            
        } 
        
        /* 防止重复关闭已关闭的socket */
        if (session->fd >= 0) 
        {
        	shutdown(session->fd, SHUT_RDWR);
            ret = close(session->fd);  
            if ( ret < 0)
            {
                printf("disconnect error: %s(errno: %d)\n", strerror(errno), errno);
            }
        }       
        session->fd = -1;
        session->active = 0;
        session->rx_bytes = 0;
        return nbytes;
    }
    

    /* 更新最后活动时间 */
    session->last_activity_time = time(NULL);

    /* 处理接收到的数据 */
    process_report_command(session, temp_buffer, nbytes);
    return nbytes;
}
/**
 * @brief 上报管理任务主函数
 */
void* ReportManagerTask(void* arg)
{
    int i;
    int id = *(int *)arg;
    fd_set read_fds;
    int max_fd;
    struct timeval timeout;
    int ret;

    LOG_INFO("ReportManagerTask: Starting, id=%d\n", id);

    ret = pthread_setname_np(pthread_self(), "ReportTask");
    if(ret != 0)
    {
        LOG_ERROR("ReportManagerTask: set thread name failed: %s\n", strerror(ret));
        return NULL;
    }

    /* 初始化客户端会话数组 */
    memset(g_report_clients, 0, sizeof(g_report_clients));
    for (i = 0; i < MAX_REPORT_CLIENTS; i++) 
    {
        g_report_clients[i].fd = -1;
        g_report_clients[i].active = 0;
    }

    /* 创建TCP监听socket */
    g_report_listen_fd = setup_report_listening_socket(TCP_REPORT_PORT);
    if (g_report_listen_fd < 0) 
    {
        LOG_ERROR("FATAL: Failed to create report listening socket on port %d\n", TCP_REPORT_PORT);
        return NULL;
    }

    LOG_INFO("ReportManagerTask: Listening on port %d (fd=%d)\n", TCP_REPORT_PORT, g_report_listen_fd);

    while (1) 
    {
        FD_ZERO(&read_fds);
        max_fd = g_report_listen_fd;
        FD_SET(g_report_listen_fd, &read_fds);

        /* 将所有活跃的客户端socket加入fd_set */
         for (i = 0; i < MAX_REPORT_CLIENTS; i++)
         {
             if (g_report_clients[i].active && g_report_clients[i].fd >= 0)
             {
                 FD_SET(g_report_clients[i].fd, &read_fds);
                 if (g_report_clients[i].fd > max_fd) 
                 {
                     max_fd = g_report_clients[i].fd;
                 }
             }
         }

        /* 设置超时时间为1秒 */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ret < 0) 
        {
            if (errno != EINTR)
            {
                LOG_ERROR("ReportManagerTask: select() error\n");
                //taskDelay(sysClkRateGet());
                taskDelay(1);
            }
            continue;
        }

        if (ret == 0) 
        {
            /* 超时，继续下一次循环 */
            continue;
        }

        /* 检查是否有新连接 */
        if (FD_ISSET(g_report_listen_fd, &read_fds)) 
        {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_fd = accept(g_report_listen_fd, (struct sockaddr *)&client_addr, &addr_len);
 
#if 0
            //将接收到的数据原样返回
            int nbytes;
            unsigned char temp_buffer[1024];
            while ((nbytes = recv(client_fd, temp_buffer, sizeof(temp_buffer), 0)) > 0)
            {
                printf("Received %d bytes from client: ", nbytes);
                printf("\n");
                send(client_fd, temp_buffer, nbytes, 0);
            }
#endif
            
            if (client_fd >= 0)
            {
                /* 查找空闲的会话槽 */
                int found = 0;
                for (i = 0; i < MAX_REPORT_CLIENTS; i++)
                {
                    if (!g_report_clients[i].active) 
                    {
                        g_report_clients[i].fd = client_fd;
                        g_report_clients[i].active = 1;
                        g_report_clients[i].rx_bytes = 0;
                        g_report_clients[i].last_activity_time = time(NULL);
                        g_report_clients[i].client_addr = client_addr; // 保存客户端地址信息
                        //set_socket_non_blocking(client_fd);
                        set_tcp_keepalive(client_fd, 60, 5, 3);
                        
                        LOG_INFO("ReportManagerTask: New client connected (fd=%d, slot=%d, IP=%s): %x\n", client_fd, i, 
                        										inet_ntoa(client_addr.sin_addr), inet_addr(inet_ntoa(client_addr.sin_addr)));
                       
                        found = 1;
                        break;
                    }
                }

                if (!found) 
                {
                    LOG_WARN("ReportManagerTask: Too many clients, rejecting connection\n");
                    close(client_fd);
                }
            }
        }

        /* 检查所有活跃客户端的数据 */
        for (i = 0; i < MAX_REPORT_CLIENTS; i++)
        {
            if (g_report_clients[i].active && g_report_clients[i].fd >= 0) 
            {
                if (FD_ISSET(g_report_clients[i].fd, &read_fds)) 
                {
                    ret = handle_report_client_data(&g_report_clients[i]);
                    
                }
            }
        }
    }

    return NULL;
}


/**
 * @brief 创建并配置上报服务的监听socket
 */
static int setup_report_listening_socket(int port)
{
    int sock_fd;
    struct sockaddr_in server_addr;
    int optval = 1;

    /* 创建socket */
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        LOG_ERROR("socket() failed for report port %d\n", port);
        return -1;
    }

    /* 设置SO_REUSEADDR选项 */
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) < 0) {
        LOG_ERROR("setsockopt(SO_REUSEADDR) failed\n");
        close(sock_fd);
        return -1;
    }

    /* 准备服务器地址结构体 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    /* 绑定地址 */
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind() failed for report port");
        close(sock_fd);
        return -1;
    }

    /* 开始监听 */
    if (listen(sock_fd, LISTEN_BACKLOG) < 0) 
    {
        LOG_ERROR("listen() failed for report port %d\n", port);
        close(sock_fd);
        return -1;
    }

    LOG_INFO("Successfully created report listening socket on port %d, fd=%d\n", port, sock_fd);
    return sock_fd;
}

/**
 * @brief 将socket设置为非阻塞模式
 */
//static void set_socket_non_blocking(int sock_fd)
//{
//    int on = 1;
//    if (ioctl(sock_fd, FIONBIO, (int *)&on) < 0) {
//        perror("ioctl(FIONBIO)");
//        LOG_ERROR("ioctl(FIONBIO) failed\n");
//    }
//}

/**
 * @brief 设置TCP Keep-Alive机制
 */
static void set_tcp_keepalive(int sock_fd, int keepidle, int keepintl, int keepcnt)
{
    int optval = 1;

    /* 启用 SO_KEEPALIVE */
    if (setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, (char *)&optval, sizeof(optval)) < 0) {
        LOG_ERROR("setsockopt(SO_KEEPALIVE) failed\n");
        return;
    }

    /* 设置 TCP_KEEPIDLE */
    if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPIDLE, (char *)&keepidle, sizeof(keepidle)) < 0) {
        LOG_ERROR("setsockopt(TCP_KEEPIDLE) failed\n");
    }

    /* 设置 TCP_KEEPINTVL */
    if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPINTVL, (char *)&keepintl, sizeof(keepintl)) < 0) {
        LOG_ERROR("setsockopt(TCP_KEEPINTVL) failed\n");
    }

    /* 设置 TCP_KEEPCNT */
    if (setsockopt(sock_fd, IPPROTO_TCP, TCP_KEEPCNT, (char *)&keepcnt, sizeof(keepcnt)) < 0) {
        LOG_ERROR("setsockopt(TCP_KEEPCNT) failed\n");
    }
}


/**
 * @brief 发送响应成功或失败给客户端
 */
void send_report_response(int client_fd, const unsigned char main_id,  unsigned char sub_id, unsigned char status)
{
	unsigned char response[1024];
	int offset = 0;
	/* 发送ACK */
	response[offset++] = 0xA5; response[offset++] = 0xA5;
	response[offset++] = main_id; response[offset++] = sub_id;
	response[offset++] = status; /* 成功或失败 */
	response[offset++] = 0x5A; response[offset++] = 0x5A;
	send(client_fd, (char*)response, offset, 0);   
}
