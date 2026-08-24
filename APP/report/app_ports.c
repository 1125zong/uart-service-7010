/*
 * =====================================================================================
 *
 * Filename:  app_ports.c
 *
 * Description:  端口管理功能实现
 * 包含端口重置、锁定、解锁、查询、镜像配置等功能
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "app_ports.h"
#include "app_com.h"
#include "app_report.h"
#include "hal_flash.h"
#include "app_dev.h"
#include "hal_axi16550.h"



/* 全局变量声明 */
extern SEM_ID g_config_mutex;

extern ReportClientSession g_report_clients[MAX_REPORT_CLIENTS];

unsigned int g_locked_port_ips[MAX_LOCKED_PORT_IPS];


/**
 * @brief 从客户端文件描述符获取IP地址
 * @param client_fd 客户端文件描述符
 * @param client_addr 输出参数，用于接收客户端地址信息
 * @return int 成功返回0，失败返回-1
 */
static int get_client_ip_from_fd(int client_fd, struct sockaddr_in *client_addr)
{
    socklen_t addr_len = sizeof(*client_addr);

    if (getpeername(client_fd, (struct sockaddr*)client_addr, &addr_len) != 0)
    {
        LOG_ERROR("  ERROR: Failed to get client address, errno: %d", errno);
        return -1;
    }

    LOG_INFO("  Client IP address: %s\n", inet_ntoa(client_addr->sin_addr));

    return 0;
}


/**
 * @brief 查询某一个端口是否被锁定
 * @param port_index 端口索引
 * @return int 端口锁定状态，0表示未锁定，1表示已锁定，-1表示无效的端口索引
 */

static int is_port_locked(int port_index)
{
    if (port_index < 0 || port_index >= NUM_PORTS) 
    {
        LOG_ERROR("  ERROR: Invalid port index: %d\n", port_index);
        return -1;
    }
    return g_system_config.device.interface_lock_status[port_index];
}

/*
 * @brief 处理端口重置命令
 * @details 功能ID: 0x08, 子功能ID: 0x01
 * @note 接收数据格式:
 *       0xA5 0xA5 | main_id | sub_id | 要复位的端口数量 | 端口号列表 | 0x5A 0x5A
 * @note 返回数据格式:
 *       0xA5 0xA5 | main_id | sub_id | 成功端口数量 | 成功端口号列表 | 0x5A 0x5A
 * @note 软复位操作:
 *       1. 清空接收/发送FIFO
 *       2. 清空接收缓冲区
 *       3. 复位调制解调器状态
 */
void handle_port_reset(int client_fd, unsigned char *data, int len)
{
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];

    // 要复位的端口数量
    uint8_t port_num = data[4];

    // 提取要复位的端口号列表
    unsigned char port_list[NUM_PORTS];
    for (int i = 0; i < port_num && i < NUM_PORTS; i++)
    {
        port_list[i] = data[5 + i];
    }

    LOG_INFO("  Action: Port Reset, SubID=0x01, port_num=%d\n", port_num);

    // 设备被锁定，直接返回
    if (is_device_locked())
    {
        LOG_ERROR("  ERROR: Device is locked, cannot reset port\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    unsigned char response[64];
    int offset = 0;

    // 帧头
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;

    // 主ID和子ID
    response[offset++] = main_id;
    response[offset++] = sub_id;

    // 成功的端口数量和端口号列表
    unsigned char success_count = 0;
    unsigned char success_port_list[NUM_PORTS];

    // 对请求的每个端口进行软复位
    for (int i = 0; i < port_num && i < NUM_PORTS; i++)
    {
        unsigned char port_id = port_list[i];

        if (port_id > NUM_PORTS)
        {
            LOG_WARN("  Port %d: Invalid port ID, skip\n", port_id);
            continue;
        }

        LOG_INFO("  Resetting Port %d...\n", port_id);

        // 清空接收缓冲区
        unsigned char recv_buf[64];
        unsigned int recv_len = sizeof(recv_buf);
        axi16550Recv(port_id, recv_buf, &recv_len, NULL, NULL, NULL, NULL);

        // 复位FIFO (FCR bit 1=1 清空TX FIFO, bit 2=1 清空RX FIFO)
        userAxiCfgWrite(port_id, AXI_16550_FCR, 0x06);

        // 复位调制解调器状态 (MCR bit 0=1 触发DTR, bit 1=1 触发RTS)
        // 然后清零以恢复正常状态
        userAxiCfgWrite(port_id, AXI_16550_MCR, 0x03);
        usleep(1000);  // 1ms
        userAxiCfgWrite(port_id, AXI_16550_MCR, 0x00);

        // 清空LSR状态寄存器 (读取可清除错误状态)
        unsigned char lsr = userAxiCfgRead(port_id, AXI_16550_LSR);

        // 再次清空接收缓冲区
        recv_len = sizeof(recv_buf);
        axi16550Recv(port_id, recv_buf, &recv_len, NULL, NULL, NULL, NULL);

        LOG_INFO("  Port %d: RESET DONE (LSR=0x%02X)\n", port_id, lsr);
        success_port_list[success_count++] = port_id;
    }

    // 成功端口数量
    response[offset++] = success_count;

    // 成功端口号列表
    for (int i = 0; i < success_count; i++)
    {
        response[offset++] = success_port_list[i];
    }

    // 帧尾
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;

    // 发送响应
    send(client_fd, (char*)response, offset, 0);

    LOG_INFO("  Port reset completed, success_count=%d\n", success_count);
}


static void update_channel_lock_status(int port_index, int lock_status)
{
    // 更新通道锁定状态
    g_system_config.channels[port_index].is_locked = lock_status;
}

/**
 * @brief 处理端口锁定命令
 * @details 功能ID: 0x08, 子功能ID: 0x02
 * @param client_fd 客户端文件描述符
 * @param data 包含端口信息的数据
 * @param len 数据长度
 * 请求格式：A5 A5 08 02 端口数 端口号... 6字节MAC地址 5A 5A
 * 响应格式：A5 A5 08 02 01(成功) 6字节(MAC地址) 5A 5A
 */
void handle_port_lock(int client_fd, unsigned char *data, int len)
{
    LOG_INFO("====handle_port_lock====\n");
    uint8_t response[32];
    uint8_t offset = 0;
    unsigned char main_id = data[2];
    unsigned char sub_id  = data[3];
    int i = 0;
    //锁定几个端口
    uint8_t port_cnt = data[4]; // data[4]：要锁定的端口数量
    unsigned char client_mac[6];

    LOG_INFO("  Action: Port Lock, SubID=0x02\n");

    LOG_INFO("  Locking %d ports\n", port_cnt);

    // 检查数据长度：帧头(2) + 主功能ID(1) + 子功能ID(1) + 端口数(1) + 端口号(port_cnt) + MAC地址(6) + 帧尾(2)
    if (len < 2 + 1 + 1 + 1 + port_cnt + 6 + 2)
    {
        LOG_ERROR("  ERROR: Invalid data length, expected at least %d, got %d\n", 2 + 1 + 1 + 1 + port_cnt + 6 + 2, len);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    //设备被锁定，直接返回
    if (is_device_locked())        
    {
        LOG_ERROR("  ERROR: Device is locked, cannot lock port\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    // 从请求数据中提取MAC地址（紧跟在端口号之后）
    memcpy(client_mac, &data[5 + port_cnt], 6);

    LOG_INFO("  Client MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
             client_mac[0], client_mac[1], client_mac[2],
             client_mac[3], client_mac[4], client_mac[5]);

    // 获取配置锁以安全访问设备配置
    semTake(g_config_mutex, WAIT_FOREVER);

    // 使用封装的函数获取客户端IP地址
    struct sockaddr_in client_addr;
    if (get_client_ip_from_fd(client_fd, &client_addr) != 0)
    {
        semGive(g_config_mutex);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    // 记录客户端IP地址
    unsigned int client_ip = client_addr.sin_addr.s_addr;

    LOG_INFO("  client_addr.sin_addr.s_addr: 0x%x\n", client_ip);
    LOG_INFO("  Client IP address: %s\n", inet_ntoa(client_addr.sin_addr));

    // 首先检查指定锁定的端口当前是否已经被锁定
    for (i = 0; i < port_cnt; i++)
    {
        int port_index = data[5 + i] - 1; // 修正：端口号从data[5]开始
        if (port_index < 0 || port_index >= NUM_PORTS)
        {
            LOG_ERROR("  ERROR: Invalid port index: %d\n", port_index);
            continue;
        }

        LOG_INFO("  Locked port %d: IP=0x%x, MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
                 port_index,
                 g_system_config.channels[port_index].locked_client_ip[0],
                 g_system_config.channels[port_index].locked_client_mac[0],
                 g_system_config.channels[port_index].locked_client_mac[1],
                 g_system_config.channels[port_index].locked_client_mac[2],
                 g_system_config.channels[port_index].locked_client_mac[3],
                 g_system_config.channels[port_index].locked_client_mac[4],
                 g_system_config.channels[port_index].locked_client_mac[5]);

        if ( g_system_config.device.interface_lock_status[port_index] == INTERFACE_LOCK_STATE_LOCKED )
        {
            // 比较MAC地址是否匹配
            if (memcmp(g_system_config.channels[port_index].locked_client_mac, client_mac, 6) != 0)
            {
                LOG_ERROR("  ERROR: Port %d is already locked by other PC (MAC mismatch)\n", port_index);
                semGive(g_config_mutex);
                send_report_response(client_fd, main_id, sub_id, FAILED);
                return;
            }
            // MAC匹配，是当前PC再次锁定，成功继续
            LOG_INFO("Port %d is already locked by this PC, re-lock success\n", port_index);
        }

        // 设置端口锁定状态数组
        g_system_config.device.interface_lock_status[port_index] = INTERFACE_LOCK_STATE_LOCKED;

        //更新该通道的锁定状态
        update_channel_lock_status(port_index, INTERFACE_LOCK_STATE_LOCKED);

        /* 记录锁定的客户端的mac地址 */
        memcpy(g_system_config.channels[port_index].locked_client_mac, client_mac, 6);

        /* 点亮端口对应的LED灯 */
        Portled(port_index, 1);

        LOG_INFO("  Locked port %d, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                 port_index,
                 g_system_config.channels[port_index].locked_client_mac[0],
                 g_system_config.channels[port_index].locked_client_mac[1],
                 g_system_config.channels[port_index].locked_client_mac[2],
                 g_system_config.channels[port_index].locked_client_mac[3],
                 g_system_config.channels[port_index].locked_client_mac[4],
                 g_system_config.channels[port_index].locked_client_mac[5]);
    }

    //写入flash

    if (dev_config_save() != OK)
	{
		LOG_ERROR("ERROR[reboot]: Failed to save  configuration to flash.\n");       
        send_report_response(client_fd, main_id, sub_id, FAILED);
        //解锁释放
        semGive(g_config_mutex);
        return;
	}
    
    // 释放配置锁
    semGive(g_config_mutex);
  
    LOG_INFO("Successfully locked %d ports", port_cnt);
   
    //返回锁定的端口信息: 成功:0x01 端口已被其他客户端锁定:0xE1 端口号无效: 0xE2
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;
    response[offset++] = main_id;
    response[offset++] = sub_id;
    response[offset++] = 0x01;
    memcpy(response + offset, client_mac, 6);
    offset += 6;
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;
    send(client_fd, response, offset, 0);  
    return;
}


/**
 * @brief 检查发送请求的MAC地址是否与锁定的端口的MAC地址匹配
 * @param port_index 端口索引
 * @param provided_mac 提供的MAC地址
 * @return int 0表示匹配，-1表示不匹配
 */

static int check_port_lock_identity(int port_index, unsigned char *provided_mac)
{
    // 检查端口索引是否有效
    if (port_index < 0 || port_index > (NUM_PORTS - 1) )
    {
        LOG_ERROR("  ERROR: Invalid port index: %d\n", port_index);
        return -1;
    }

    // 比较MAC地址
    if (memcmp(provided_mac, g_system_config.channels[port_index].locked_client_mac, 6) != 0)
    {
        LOG_ERROR("  ERROR: Client MAC address does not match\n");
        LOG_ERROR("  ERROR: Expected: %02x:%02x:%02x:%02x:%02x:%02x\n",
                  g_system_config.channels[port_index].locked_client_mac[0],
                  g_system_config.channels[port_index].locked_client_mac[1],
                  g_system_config.channels[port_index].locked_client_mac[2],
                  g_system_config.channels[port_index].locked_client_mac[3],
                  g_system_config.channels[port_index].locked_client_mac[4],
                  g_system_config.channels[port_index].locked_client_mac[5]);
        LOG_ERROR("  ERROR: Got: %02x:%02x:%02x:%02x:%02x:%02x\n",
                  provided_mac[0], provided_mac[1], provided_mac[2],
                  provided_mac[3], provided_mac[4], provided_mac[5]);
        return -1;
    }

    return 0; // 匹配

}

/**
 * @brief 处理端口解锁命令
 * @details 功能ID: 0x08, 子功能ID: 0x03
 * 请求格式：A5 A5 08 03 端口数 端口号... 6字节MAC地址 5A 5A
 * 响应格式：A5 A5 08 03 01(成功)/0xE1(失败) 5A 5A
 * 注意：解锁时会自动验证客户端的IP地址和MAC地址是否与锁定时一致
 */

void handle_port_unlock(int client_fd, unsigned char *data, int len)
{
    uint8_t  response[64];
    int offset = 0;
    int i = 0;
    uint8_t  unlock_success = 1; // 标记是否所有端口都解锁成功
    unsigned char client_mac[6];

    uint8_t  main_id = data[2];
    uint8_t  sub_id  = data[3];
    uint8_t  port_cnt = data[4];
    uint8_t  port_index = 0;

    LOG_INFO("  Action: Port Unlock, SubID=0x03\n");

    // 检查数据长度：帧头(2) + 主功能ID(1) + 子功能ID(1) + 端口数(1) + 端口号(port_cnt) + MAC地址(6) + 帧尾(2)
    if (len < 2 + 1 + 1 + 1 + port_cnt + 6 + 2)
    {
        LOG_ERROR("  ERROR: Invalid data length, expected at least %d, got %d\n", 2 + 1 + 1 + 1 + port_cnt + 6 + 2, len);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    //设备被锁定，直接返回
    if (is_device_locked())        
    {
        LOG_ERROR("  ERROR: Device is locked, cannot unlock port\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    // 从请求数据中提取MAC地址（紧跟在端口号之后）
    memcpy(client_mac, &data[5 + port_cnt], 6);

    LOG_INFO("  Client MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
             client_mac[0], client_mac[1], client_mac[2],
             client_mac[3], client_mac[4], client_mac[5]);

    // 获取配置锁以安全访问设备配置
    semTake(g_config_mutex, WAIT_FOREVER);

    for (i = 0; i < port_cnt; i++)
    {
        port_index = data[5 + i] - 1;

        //首先检查该端口是否被锁定，如果未被锁定则返回错误
        if (is_port_locked(port_index) != INTERFACE_LOCK_STATE_LOCKED)
        {
            LOG_ERROR("  ERROR: Port %d is not locked\n", port_index);
            unlock_success = 0;
            continue;
        }

        //检查MAC地址是否与锁定的端口的MAC地址一致
        if (check_port_lock_identity(port_index, client_mac) != 0)
        {
            LOG_ERROR("  ERROR: Port unlock failed, identity does not match\n");
            unlock_success = 0;
            continue;
        }

        // 更新端口锁定状态为未锁定
        g_system_config.device.interface_lock_status[port_index] = INTERFACE_LOCK_STATE_UNLOCK;
        update_channel_lock_status(port_index, INTERFACE_LOCK_STATE_UNLOCK);

        /* 熄灭端口对应的LED灯 */
        Portled(port_index, 0);

        LOG_INFO("  Port %d unlocked successfully\n", port_index);
    }

    // 释放配置锁
    semGive(g_config_mutex);

    // 如果解锁成功，需要保存状态到 flash
    if (unlock_success)
    {
        if (dev_config_save() != OK)
        {
            LOG_ERROR("ERROR[unlock]: Failed to save configuration to flash.\n");
            send_report_response(client_fd, main_id, sub_id, FAILED);
            return;
        }
    }

    // 发送确认响应
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;
    response[offset++] = main_id;
    response[offset++] = sub_id;
    response[offset++] = unlock_success ? 0x01 : 0xE1; // 成功:0x01, 失败:0xE1
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;
    send(client_fd, response, offset, 0);
}

/**
 * @brief 处理端口锁定查询命令
 * @details 功能ID: 0x08, 子功能ID: 0x04
 * 响应格式：A5 A5 08 04 [端口总数] [端口0信息] [端口1信息] ... [端口N-1信息] 5A 5A
 * 每个端口信息：端口状态(1字节) + MAC地址(6字节)
 */

void handle_port_lock_query(int client_fd, unsigned char *data, int len)
{
    unsigned char response[512];  // 响应缓冲区：帧头(2) + 功能ID(2) + 端口总数(1) + 16端口*(1+6) + 帧尾(2) = 2+2+1+112+2 = 119字节
    int offset = 0;
    unsigned char main_id = data[2];
    unsigned char sub_id  = data[3];
    int i;

    LOG_INFO("  Action: Port Lock Query, SubID=0x04\n");

    if(len < 0)
    {
        LOG_ERROR("  ERROR: Invalid data length\n");
    }

    // 帧头
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;

    // 主功能ID和子功能ID
    response[offset++] = main_id;
    response[offset++] = sub_id;

    // 端口总数 (1字节)
    response[offset++] = NUM_PORTS;

    // 获取配置锁以安全访问设备配置
    semTake(g_config_mutex, WAIT_FOREVER);

    // 遍历所有16个端口，添加每个端口的信息到响应中
    for (i = 0; i < NUM_PORTS; i++)
    {
        // 端口锁定状态 (1字节): 0x00(未锁定) 0x01(已锁定)
        response[offset++] = g_system_config.device.interface_lock_status[i];

        // MAC地址 (6字节): 如果端口被锁定，返回实际锁定的MAC地址，否则返回全0
        if (g_system_config.device.interface_lock_status[i] == INTERFACE_LOCK_STATE_LOCKED)
        {
            // 端口被锁定，返回实际锁定的MAC地址
            memcpy(&response[offset], g_system_config.channels[i].locked_client_mac, 6);
            LOG_INFO("  Port %d: LOCKED, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                     i,
                     g_system_config.channels[i].locked_client_mac[0],
                     g_system_config.channels[i].locked_client_mac[1],
                     g_system_config.channels[i].locked_client_mac[2],
                     g_system_config.channels[i].locked_client_mac[3],
                     g_system_config.channels[i].locked_client_mac[4],
                     g_system_config.channels[i].locked_client_mac[5]);
        }
        else
        {
            // 端口未锁定，返回全0的MAC地址
            memset(&response[offset], 0, 6);
            LOG_INFO("  Port %d: UNLOCKED\n", i);
        }
        offset += 6;
    }

    // 释放配置锁
    semGive(g_config_mutex);

    // 帧尾
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;

    // 发送响应
    send(client_fd, (char*)response, offset, 0);

    LOG_INFO("  Port Lock Query response sent, total %d ports, response size: %d\n", NUM_PORTS, offset);
}

/**
 * @brief 处理端口镜像配置命令
 * @details 功能ID: 0x08, 子功能ID: 0x05
 */

void handle_port_mirror_config(int client_fd, unsigned char *data, int len)
{
    unsigned char sub_id  = data[3];  
    unsigned char main_id = data[2];  
    
    LOG_INFO("  Action: Port Mirror Config, SubID=0x05\n");

    if(len < 7)
    {
        LOG_ERROR("  ERROR: Invalid data length: %d\n", len);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    //设备被锁定，直接返回
    if (is_device_locked())        
    {
        LOG_ERROR("  ERROR: Device is locked, cannot configure port mirror\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    /* 解析参数 */
    unsigned char port_number = data[4];  /* 端口号: 1~NUM_PORTS */
    unsigned char enable = data[5];      /* 使能: 01=启用, 00=禁用 */
    
    /* 校验端口号: 1 ~ NUM_PORTS */
    if (port_number < 1 || port_number > NUM_PORTS)
    {
        LOG_ERROR("  ERROR: Invalid port number %d\n", port_number);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    /* 校验使能值 */
    if (enable > 1)
    {
        LOG_ERROR("  ERROR: Invalid enable value %d\n", enable);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    int ch_idx = port_number - 1;
    
    semTake(g_config_mutex, WAIT_FOREVER);
    g_system_config.channels[ch_idx].mirror_enable = enable;
    semGive(g_config_mutex);
    
    LOG_INFO("  Port %d mirror %s\n", port_number, enable ? "enabled" : "disabled");
    
    // 发送确认响应
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
}

/**
 * @brief 处理端口自检命令
 * @details 功能ID: 0x08, 子功能ID: 0x08
 * @note 接收数据格式:
 *       0xA5 0xA5 | main_id | sub_id | 要自检的端口数量 | 自检的端口号列表 | 0x5A 0x5A
 * @note 返回数据格式:
 *       0xA5 0xA5 | main_id | sub_id | 成功端口数量 | 成功端口号列表 | 0x5A 0x5A
 */
void handle_port_self_check(int client_fd, unsigned char *data)
{
    unsigned char sub_id  = data[3];
    unsigned char main_id = data[2];

    // 要自检的端口数量
    uint8_t port_num = data[4];

    // 提取要自检的端口号列表 (从data[5]开始)
    unsigned char port_list[NUM_PORTS];
    for (int i = 0; i < port_num && i < NUM_PORTS; i++)
    {
        port_list[i] = data[5 + i];
    }

    LOG_INFO("  Action: Port Self Check, SubID=0x08, port_num=%d\n", port_num);

    // 检查设备是否被锁定
    if (is_device_locked())
    {
        LOG_ERROR("  ERROR: Device is locked, cannot perform self check\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    unsigned char response[64];
    int offset = 0;

    // 帧头
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;

    // 主ID和子ID
    response[offset++] = main_id;
    response[offset++] = sub_id;

    // 测试数据模式
    const unsigned char test_pattern[] = {0x55, 0xAA, 0x5A, 0xA5, 0x00, 0x01, 0x02, 0x03};
    unsigned char recv_buf[64];
    unsigned int recv_len = 0;

    // 成功的端口数量和端口号列表
    unsigned char success_count = 0;
    unsigned char success_port_list[NUM_PORTS];

    // 对请求的每个端口进行自检
    for (int i = 0; i < port_num && i < NUM_PORTS; i++)
    {
        unsigned char port_id = port_list[i];

        if (port_id >= NUM_PORTS)
        {
            LOG_WARN("  Port %d: Invalid port ID, skip\n", port_id);
            continue;
        }

        LOG_INFO("  Testing Port %d...\n", port_id);

        // 初始化为内环模式 (数据从TX直接回传到RX)
        axi16550CheckSelfInit(port_id, 115200);

        // 发送测试数据
        axi16550Send(port_id, (unsigned char*)test_pattern, sizeof(test_pattern));

        // 短暂延时等待内环数据返回
        usleep(5000);  // 5ms

        // 尝试接收数据
        recv_len = sizeof(recv_buf);
        if (axi16550Recv(port_id, recv_buf, &recv_len, NULL, NULL, NULL, NULL) == 0 && recv_len > 0)
        {
            // 打印接收到的数据
            LOG_INFO("  Port %d: recv_len=%d, data=", port_id, recv_len);
            for (unsigned int k = 0; k < recv_len; k++)
            {
                LOG_INFO("%02X ", recv_buf[k]);
            }
            LOG_INFO("\n");

            // 检查接收到的数据是否与发送的测试模式匹配
            int match = 1;
            if (recv_len >= sizeof(test_pattern))
            {
                // 检查最后收到的完整测试模式
                int start_pos = recv_len - sizeof(test_pattern);
                for (int j = 0; j < (int)sizeof(test_pattern) && (start_pos + j) < (int)recv_len; j++)
                {
                    if (recv_buf[start_pos + j] != test_pattern[j])
                    {
                        match = 0;
                        break;
                    }
                }
            }
            else
            {
                match = 0;
            }

            if (match)
            {
                LOG_INFO("  Port %d: SELF CHECK PASS\n", port_id);
                success_port_list[success_count++] = port_id;
            }
            else
            {
                LOG_WARN("  Port %d: Data mismatch (sent=%d, recv=%d)\n", port_id, (int)sizeof(test_pattern), recv_len);
            }
        }
        else
        {
            LOG_WARN("  Port %d: No data received (hardware issue)\n", port_id);
        }

        /* Normal mode:(bit 4 = 0) loopback mode:(bit 4 = 1)*/
        userAxiCfgWrite(port_id, AXI_16550_MCR, 0x00);
        // 清空FIFO
        userAxiCfgWrite(port_id, AXI_16550_FCR, 0x06);
    }

    if(success_count == 0)
    {
        LOG_ERROR("  ERROR: No ports passed self check\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    // 成功端口数量
    response[offset++] = success_count;

    // 成功端口号列表
    for (int i = 0; i < success_count; i++)
    {
        response[offset++] = success_port_list[i];
    }

    // 帧尾
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;

    // 发送响应
    send(client_fd, (char*)response, offset, 0);

    LOG_INFO("  Port self check completed, success_count=%d\n", success_count);
}

