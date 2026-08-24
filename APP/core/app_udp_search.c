/*
 * =====================================================================================
 *
 * Filename:  app_udp_search.c
 *
 * Description:  实现 UDP 搜索与应答任务，用于设备发现。
 *
 * =====================================================================================
 */

#include "app_com.h"
//#include <inetLib.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ------------------ Task-Specific Constants ------------------ */
#define UDP_SEARCH_PORT         48899 // 行业中常用的设备搜索端口
#define UDP_SEARCH_REQUEST_MSG  "SEARCH_DEVICE_WQ" // 上位机发送的搜索字符串
#define MAX_UDP_PACKET_SIZE     1024


extern void test(void);
extern void serial_no_to_bytes(const DevSerialNo* sn, unsigned char out[8]);

// 准备回复内容
static char response_msg[MAX_UDP_PACKET_SIZE];
static int build_device_response_msg(void)
{
    LOG_INFO("Action:build_device_response_msg\n");
  
    int offset = 0;
    unsigned char sn[8];
       
    //semTake(g_config_mutex, WAIT_FOREVER);
    {
        DeviceSettings* dev = &g_system_config.device;
        
        /* ========== 设备基础信息块 ========== */
        
        /* 型号名称 (40字节，不足补0) */
        memset(&response_msg[offset], 0, 40);
        strncpy((char*)&response_msg[offset], dev->model_name, MAX_MODEL_NAME_LEN);
        offset += 40;
        
        /* 序列号 (8字节) */
        serial_no_to_bytes(&dev->serial_no, sn);
        memcpy(&response_msg[offset], sn, 8);
        offset += 8;
        
        /* 设备名称 (40) */
        memset(&response_msg[offset], 0, 40);
        strncpy((char*)&response_msg[offset], dev->device_name, MAX_DEVICE_NAME_LEN);
        offset += 40;
        
        /* 固件版本 (4字节) */
        response_msg[offset++] = dev->firmware_version[0];
        response_msg[offset++] = dev->firmware_version[1];
        response_msg[offset++] = dev->firmware_version[2];
        response_msg[offset++] = dev->firmware_version[3];
        
        /* 设备状态 (1字节) */
        response_msg[offset++] = dev->status;
        
        /* 网口数量 (1字节) */
        response_msg[offset++] = dev->lan_count;
        
        /* IP地址1 (4字节，网络字节序) */
        unsigned int ip1 = htonl(dev->ip_address1);
        memcpy(&response_msg[offset], &ip1, 4);
        offset += 4;
        
        /* MAC地址1 (6字节) */
        memcpy(&response_msg[offset], dev->mac_address1, 6);
        offset += 6;
        
        /* LAN1速度 (1字节) */
        response_msg[offset++] = dev->lan_speed1;
        
        /* IP地址2 (4字节，网络字节序) */
        unsigned int ip2 = htonl(dev->ip_address2);
        memcpy(&response_msg[offset], &ip2, 4);
        offset += 4;
        
        /* MAC地址2 (6字节) */
        memcpy(&response_msg[offset], dev->mac_address2, 6);
        offset += 6;
        
        /* LAN2速度 (1字节) */
        response_msg[offset++] = dev->lan_speed2;
        
        /* LCM在位 (1字节) */
        response_msg[offset++] = dev->lcm_present;
        
        /* 电源1状态 (1字节) */
        response_msg[offset++] = dev->power1_status;
        
        /* 电源2状态 (1字节) */
        response_msg[offset++] = dev->power2_status;
        
        /* 接口类型数量 (4字节，网络字节序) */
        unsigned int iface_count = (dev->interface_type_count);
        memcpy(&response_msg[offset], &iface_count, 4);
        offset += 4;
        
        /* 接口状态 (16字节) */
        memcpy(&response_msg[offset], dev->interface_status, NUM_PORTS);
        offset += 16;
        
        //printf("udp dev->lock_status: %x\n", dev->lock_status);
        /* 设备锁定状态 (1字节) */
        response_msg[offset++] = dev->lock_status;
        
        /* 接口锁定状态 (16字节) */
        memcpy(&response_msg[offset], dev->interface_lock_status, NUM_PORTS);
        offset += 16;        
    }
    
    //semGive(g_config_mutex);
    return offset;
}

/* ------------------ Global Variable Definitions ------------------ */

void* UdpSearchTask(void *arg)
{
    int sock_fd;
    int id = *(int *)arg;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char buffer[MAX_UDP_PACKET_SIZE];
    int optval = 1; // 用于 setsockopt 的选项值
    
    int length = 1;

    LOG_INFO("UdpSearchTask: Starting, id=%d\n", id);

    int ret = pthread_setname_np(pthread_self(), "UdpSearchTask");
    if (ret != 0) {
        LOG_ERROR("UdpSearchTask: pthread_setname_np() failed: %s", strerror(ret));
        return NULL;
    }

    // 1. 创建 UDP socket
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        LOG_FATAL("UdpSearchTask: socket() failed: %s", strerror(errno));
        return NULL;
    }
    
    // *** 设置SO_BROADCAST选项以允许接收广播 ***
    if (setsockopt(sock_fd, SOL_SOCKET, SO_BROADCAST, (char *)&optval, sizeof(optval)) < 0) {
        LOG_FATAL("UdpSearchTask: setsockopt(SO_BROADCAST) failed: %s", strerror(errno));
        close(sock_fd);
        return NULL;
    }

    // 2. 准备服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网络接口
    server_addr.sin_port = htons(UDP_SEARCH_PORT);

    // 3. 绑定端口
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LOG_FATAL("UdpSearchTask: bind() failed on port %d: %s", UDP_SEARCH_PORT, strerror(errno));
        close(sock_fd);
        return NULL;
    }
    
    LOG_INFO("UdpSearchTask: Listening for broadcasts on UDP port %d", UDP_SEARCH_PORT);

    /* ------------------ 主循环，等待广播包 ------------------ */
    while (1)
    {
        // 4. 阻塞式地等待接收数据包。recvfrom 会填充发送方的地址信息
        int n = 0;
        n = recvfrom(sock_fd, buffer, MAX_UDP_PACKET_SIZE - 1, 0,
                         (struct sockaddr *)&client_addr, &client_addr_len);
        if (n > 0) 
        {
            buffer[n] = '\0';

            LOG_INFO("UdpSearchTask: Received %d bytes from %s:%d", n, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            LOG_INFO("before UdpSearchTask: Received buf: %s", buffer);
            // 5. 校验收到的消息是否是合法的搜索请求
            if (strncmp(buffer, UDP_SEARCH_REQUEST_MSG, strlen(UDP_SEARCH_REQUEST_MSG)) == 0) {
                
                char client_ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
                LOG_INFO("UdpSearchTask: Received search request from %s", client_ip_str);
                
                LOG_INFO("UdpSearchTask: Received buf: %s", buffer);

                
                // 以线程安全的方式读取全局配置
                semTake(g_config_mutex, WAIT_FOREVER);
                
                // --- 临界区开始 ---

                // 打包响应报文
                length = build_device_response_msg();
                LOG_INFO("length: %d\n", length);
                
                

                // 7. 将回复单播发送回请求方
                sendto(sock_fd, response_msg,  length,  0, (struct sockaddr *)&client_addr, client_addr_len);
                // --- 临界区结束 ---
                semGive(g_config_mutex);
            }
        }
    }

    return NULL;
}
