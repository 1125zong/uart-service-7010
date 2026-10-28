/*
 * =====================================================================================
 *
 * Filename:  app_report_status.c
 *
 * Description:  设备状态监控功能实现
 * 处理功能ID为0x07的设备状态监控命令，包括实时状态查询、端口连接状态、工作模式等
 *
 * =====================================================================================
 */

#include "app_com.h"
#include "app_report_status.h"
#include "hal_flash.h"
#include "app_dev.h"
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/**
 * @brief 获取单个串口的网络连接信息
 * @details 从通道的 data_net_info 中提取连接信息并填入响应缓冲区
 *         格式: [index 1B] [is_locked 1B] [op_mode 1B] [client_ip 4B]
 * @param ch 通道状态指针
 * @param ch_idx 通道索引 (0-based)
 * @param buf 响应缓冲区
 * @param offset 当前偏移量 (输入输出)
 */
static void pack_port_network_info(ChannelState *ch, int ch_idx, unsigned char *buf, int *offset)
{
    /* 串口索引 (1-based) */
    buf[(*offset)++] = ch_idx + 1;

    /* 锁定状态: 0=未锁定 1=锁定 */
    buf[(*offset)++] = ch->is_locked;

    /* 操作模式 */
    buf[(*offset)++] = (unsigned char)ch->op_mode;

    /* 客户端IP: 有连接则显示第一个客户端IP，否则填0 */
    if (ch->data_net_info.num_clients > 0)
    {
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof(peer_addr);
        if (getpeername(ch->data_net_info.client_fds[0],
                        (struct sockaddr *)&peer_addr, &addr_len) == 0)
        {
            memcpy(&buf[*offset], &peer_addr.sin_addr.s_addr, 4);
        }
        else
        {
            memset(&buf[*offset], 0, 4);
        }
    }
    else
    {
        memset(&buf[*offset], 0, 4);
    }
    *offset += 4;

    LOG_DEBUG("  Port %d: locked=%d, mode=%d, ip=0x%08x\n",
              ch_idx + 1, ch->is_locked, ch->op_mode,
              *(unsigned int *)&buf[*offset - 4]);
}


/**
 * @brief 读取串口网络连接信息 (子功能ID 0x01)
 * @details 协议格式:
 *  请求: [A5 A5] [0x07] [0x01] [data_len 2B] [port_seq 2B] [5A 5A]
 *        port_seq=0xFFFF 表示读取所有串口
 *  响应: [A5 A5] [0x07] [0x01] [data_len 2B]
 *        [port_count 2B]
 *        [port_info_block x N]
 *        [5A 5A]
 *
 *  port_info_block:
 *   [index 1B] [is_locked 1B] [op_mode 1B] [client_ip 4B]
 */
void get_serial_port_network_info(int client_fd, unsigned char *data, int len)
{
    unsigned char response[1024];
    int offset = 0;
    int i;

    unsigned char main_id = data[2];
    unsigned char sub_id  = data[3];

    LOG_INFO("  Action: Read Serial Port Network Connection Info\n");

    /* 帧头 */
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;
    response[offset++] = main_id;
    response[offset++] = sub_id;

    /* 数据长度占位 (2字节, 稍后回填) */
    int data_len_pos = offset;
    response[offset++] = 0x00;
    response[offset++] = 0x00;

    /* 解析请求: 串口序列 (2字节, 0xFFFF=所有) */
    unsigned short port_seq = (data[4] << 8) | data[5];

    /* 串口数量 (2字节, 先占位) */
    int port_count_pos = offset;
    response[offset++] = 0x00;
    response[offset++] = 0x00;
    unsigned short port_count = 0;

    semTake(g_config_mutex, WAIT_FOREVER);

    if (port_seq == 0xFFFF)
    {
        /* 读取所有串口 */
        for (i = 0; i < NUM_PORTS; i++)
        {
            ChannelState *ch = &g_system_config.channels[i];
            pack_port_network_info(ch, i, response, &offset);
            port_count++;
        }
    }
    else
    {
        /* 读取指定的串口 (port_seq 的每个bit代表一个串口) */
        for (i = 0; i < NUM_PORTS; i++)
        {
            if (port_seq & (1 << i))
            {
                ChannelState *ch = &g_system_config.channels[i];
                pack_port_network_info(ch, i, response, &offset);
                port_count++;
            }
        }
    }

    semGive(g_config_mutex);

    /* 回填串口数量 */
    response[port_count_pos]     = (port_count >> 8) & 0xFF;
    response[port_count_pos + 1] = port_count & 0xFF;

    /* 回填数据长度 (从 main_id 之后到 frame tail 之前的长度) */
    unsigned short data_len = offset - 4; /* 去掉 frame header */
    response[data_len_pos]     = (data_len >> 8) & 0xFF;
    response[data_len_pos + 1] = data_len & 0xFF;

    /* 帧尾 */
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;

    send(client_fd, (char *)response, offset, 0);
    LOG_INFO("  Sent Serial Port Network Info response (%d bytes, %d ports)\n", offset, port_count);
}

/**
 * @brief 打包单个串口的统计信息
 * @details 格式: [index 1B] [TxCnt 4B] [RxCnt 4B] [TxTotalCnt 8B] [RxTotalCnt 8B]
 */
static void pack_port_serial_info(ChannelState *ch, int ch_idx, unsigned char *buf, int *offset)
{
    uint32_t tx_cnt, rx_cnt;
    uint64_t tx_total, rx_total;

    semTake(g_stats_mutex, WAIT_FOREVER);
    tx_cnt    = ch->tx_count;
    rx_cnt    = ch->rx_count;
    tx_total  = ch->tx_total_count;
    rx_total  = ch->rx_total_count;
    semGive(g_stats_mutex);

    /* 串口索引 (1-based) */
    buf[(*offset)++] = ch_idx + 1;

    /* TxCnt: 当前发送字节数 (4字节, 大端) */
    buf[(*offset)++] = (tx_cnt >> 24) & 0xFF;
    buf[(*offset)++] = (tx_cnt >> 16) & 0xFF;
    buf[(*offset)++] = (tx_cnt >> 8)  & 0xFF;
    buf[(*offset)++] =  tx_cnt        & 0xFF;

    /* RxCnt: 当前接收字节数 (4字节, 大端) */
    buf[(*offset)++] = (rx_cnt >> 24) & 0xFF;
    buf[(*offset)++] = (rx_cnt >> 16) & 0xFF;
    buf[(*offset)++] = (rx_cnt >> 8)  & 0xFF;
    buf[(*offset)++] =  rx_cnt        & 0xFF;

    /* TxTotalCnt: 累计发送字节数 (8字节, 大端) */
    buf[(*offset)++] = (tx_total >> 56) & 0xFF;
    buf[(*offset)++] = (tx_total >> 48) & 0xFF;
    buf[(*offset)++] = (tx_total >> 40) & 0xFF;
    buf[(*offset)++] = (tx_total >> 32) & 0xFF;
    buf[(*offset)++] = (tx_total >> 24) & 0xFF;
    buf[(*offset)++] = (tx_total >> 16) & 0xFF;
    buf[(*offset)++] = (tx_total >> 8)  & 0xFF;
    buf[(*offset)++] =  tx_total        & 0xFF;

    /* RxTotalCnt: 累计接收字节数 (8字节, 大端) */
    buf[(*offset)++] = (rx_total >> 56) & 0xFF;
    buf[(*offset)++] = (rx_total >> 48) & 0xFF;
    buf[(*offset)++] = (rx_total >> 40) & 0xFF;
    buf[(*offset)++] = (rx_total >> 32) & 0xFF;
    buf[(*offset)++] = (rx_total >> 24) & 0xFF;
    buf[(*offset)++] = (rx_total >> 16) & 0xFF;
    buf[(*offset)++] = (rx_total >> 8)  & 0xFF;
    buf[(*offset)++] =  rx_total        & 0xFF;

    LOG_DEBUG("  Port %d: tx=%u rx=%u tx_total=%llu rx_total=%llu\n",
              ch_idx + 1, tx_cnt, rx_cnt, tx_total, rx_total);
}

/**
 * @brief 读取串口统计信息 (子功能ID 0x02)
 * @details 协议格式:
 *  请求: [A5 A5] [0x07] [0x02] [data_len 2B] [port_seq 2B] [5A 5A]
 *        port_seq=0xFFFF 表示读取所有串口
 *  响应: [A5 A5] [0x07] [0x02] [data_len 2B]
 *        [port_count 2B]
 *        [port_info_block x N]
 *        [5A 5A]
 *
 *  port_info_block: [index 1B] [TxCnt 4B] [RxCnt 4B] [TxTotalCnt 8B] [RxTotalCnt 8B]
 */
void get_serial_port_info(int client_fd, unsigned char *data, int len)
{
    unsigned char response[1024];
    int offset = 0;
    int i;

    unsigned char main_id = data[2];
    unsigned char sub_id  = data[3];

    LOG_INFO("  Action: Read Serial Port Info\n");

    /* 帧头 */
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;
    response[offset++] = main_id;
    response[offset++] = sub_id;

    /* 数据长度占位 */
    int data_len_pos = offset;
    response[offset++] = 0x00;
    response[offset++] = 0x00;

    /* 解析请求: 串口序列 */
    unsigned short port_seq = (data[4] << 8) | data[5];

    /* 串口数量占位 */
    int port_count_pos = offset;
    response[offset++] = 0x00;
    response[offset++] = 0x00;
    unsigned short port_count = 0;

    if (port_seq == 0xFFFF)
    {
        for (i = 0; i < NUM_PORTS; i++)
        {
            ChannelState *ch = &g_system_config.channels[i];
            pack_port_serial_info(ch, i, response, &offset);
            port_count++;
        }
    }
    else
    {
        for (i = 0; i < NUM_PORTS; i++)
        {
            if (port_seq & (1 << i))
            {
                ChannelState *ch = &g_system_config.channels[i];
                pack_port_serial_info(ch, i, response, &offset);
                port_count++;
            }
        }
    }

    /* 回填串口数量 */
    response[port_count_pos]     = (port_count >> 8) & 0xFF;
    response[port_count_pos + 1] = port_count & 0xFF;

    /* 回填数据长度 */
    unsigned short data_len = offset - 4;
    response[data_len_pos]     = (data_len >> 8) & 0xFF;
    response[data_len_pos + 1] = data_len & 0xFF;

    /* 帧尾 */
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;

    send(client_fd, (char *)response, offset, 0);
    LOG_INFO("  Sent Serial Port Info response (%d bytes, %d ports)\n", offset, port_count);
}

/**
 * @brief 打包单个串口的错误统计信息
 * @details 格式: [index 1B] [FrameErr 4B] [ParityErr 4B] [Overrun 4B] [Break 4B]
 */
static void pack_port_error_info(ChannelState *ch, int ch_idx, unsigned char *buf, int *offset)
{
    uint32_t frame_cnt, parity_cnt, overrun_cnt, break_cnt;

    semTake(g_config_mutex, WAIT_FOREVER);
    frame_cnt   = ch->frame_error_count;
    parity_cnt  = ch->parity_error_count;
    overrun_cnt = ch->overrun_error_count;
    break_cnt   = ch->break_count;
    semGive(g_config_mutex);

    /* 串口索引 (1-based) */
    buf[(*offset)++] = ch_idx + 1;

    /* Frame Error (4字节, 大端) */
    buf[(*offset)++] = (frame_cnt  >> 24) & 0xFF;
    buf[(*offset)++] = (frame_cnt  >> 16) & 0xFF;
    buf[(*offset)++] = (frame_cnt  >> 8)  & 0xFF;
    buf[(*offset)++] =  frame_cnt         & 0xFF;

    /* Parity Error (4字节, 大端) */
    buf[(*offset)++] = (parity_cnt >> 24) & 0xFF;
    buf[(*offset)++] = (parity_cnt >> 16) & 0xFF;
    buf[(*offset)++] = (parity_cnt >> 8)  & 0xFF;
    buf[(*offset)++] =  parity_cnt        & 0xFF;

    /* Overrun Error (4字节, 大端) */
    buf[(*offset)++] = (overrun_cnt >> 24) & 0xFF;
    buf[(*offset)++] = (overrun_cnt >> 16) & 0xFF;
    buf[(*offset)++] = (overrun_cnt >> 8)  & 0xFF;
    buf[(*offset)++] =  overrun_cnt        & 0xFF;

    /* Break Event (4字节, 大端) */
    buf[(*offset)++] = (break_cnt >> 24) & 0xFF;
    buf[(*offset)++] = (break_cnt >> 16) & 0xFF;
    buf[(*offset)++] = (break_cnt >> 8)  & 0xFF;
    buf[(*offset)++] =  break_cnt        & 0xFF;

    LOG_DEBUG("  Port %d: frame=%u parity=%u overrun=%u break=%u\n",
              ch_idx + 1, frame_cnt, parity_cnt, overrun_cnt, break_cnt);
}

/**
 * @brief 读取串口错误信息 (子功能ID 0x03)
 * @details 协议格式:
 *  请求: [A5 A5] [0x07] [0x03] [data_len 2B] [port_seq 2B] [5A 5A]
 *  响应: [A5 A5] [0x07] [0x03] [data_len 2B]
 *        [port_count 2B]
 *        [port_info_block x N]
 *        [5A 5A]
 *
 *  port_info_block: [index 1B] [FrameErr 4B] [ParityErr 4B] [Overrun 4B] [Break 4B]
 */
void get_serial_port_errors(int client_fd, unsigned char *data, int len)
{
    unsigned char response[1024];
    int offset = 0;
    int i;

    unsigned char main_id = data[2];
    unsigned char sub_id  = data[3];

    LOG_INFO("  Action: Read Serial Port Errors\n");

    /* 帧头 */
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;
    response[offset++] = main_id;
    response[offset++] = sub_id;

    /* 数据长度占位 */
    int data_len_pos = offset;
    response[offset++] = 0x00;
    response[offset++] = 0x00;

    /* 解析请求: 串口序列 */
    unsigned short port_seq = (data[4] << 8) | data[5];

    /* 串口数量占位 */
    int port_count_pos = offset;
    response[offset++] = 0x00;
    response[offset++] = 0x00;
    unsigned short port_count = 0;

    if (port_seq == 0xFFFF)
    {
        for (i = 0; i < NUM_PORTS; i++)
        {
            ChannelState *ch = &g_system_config.channels[i];
            pack_port_error_info(ch, i, response, &offset);
            port_count++;
        }
    }
    else
    {
        for (i = 0; i < NUM_PORTS; i++)
        {
            if (port_seq & (1 << i))
            {
                ChannelState *ch = &g_system_config.channels[i];
                pack_port_error_info(ch, i, response, &offset);
                port_count++;
            }
        }
    }

    /* 回填串口数量 */
    response[port_count_pos]     = (port_count >> 8) & 0xFF;
    response[port_count_pos + 1] = port_count & 0xFF;

    /* 回填数据长度 */
    unsigned short data_len = offset - 4;
    response[data_len_pos]     = (data_len >> 8) & 0xFF;
    response[data_len_pos + 1] = data_len & 0xFF;

    /* 帧尾 */
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;

    send(client_fd, (char *)response, offset, 0);
    LOG_INFO("  Sent Serial Port Errors response (%d bytes, %d ports)\n", offset, port_count);
}

/* ---------- 以下函数预留，后续子功能由对应 handler 直接实现 ---------- */


/**
 * @brief 处理0x07 - 设备状态监控
 * 
 * @details 报文格式（二进制）：
 * [帧头 2B] [功能ID 1B] [子ID 1B] [数据长度 2B] [载荷] [帧尾 2B]
 * 
 * 载荷结构：
 * - 系统运行状态块（固定长度13字节）
 * - 串口数量 (1B)
 * - N个串口状态块（每个20字节）
 * 
 * 接收端解析步骤：
 * 1. 验证帧头(0xA55A)和帧尾(0x5A5A)
 * 2. 读取数据长度字段
 * 3. 解析固定长度的系统运行状态
 * 4. 读取串口数量N
 * 5. 循环读取N个串口状态块
 */
void handle_device_status_monitoring(int client_fd, unsigned char *data, int len)
{
    LOG_INFO("====Action: Device Status Monitoring====\n");
  
    unsigned char sub_id = data[3];  // 子ID，用于区分不同的监控请求
    
    LOG_INFO("  Action: Device Status Monitoring, main_id=0x%02X  SubID=0x%02X\n", data[2], sub_id);
    if(client_fd < 0)
    {
        LOG_ERROR("  ERROR: Invalid client_fd: %d\n", client_fd);
        return;
    }
    
    if(len < 0)
    {
        LOG_ERROR("  ERROR: Invalid data length: %d\n", len);
    }

    switch (sub_id)
     {
        // 0x01: 读取串口网络连接信息
        case 0x01:
            get_serial_port_network_info(client_fd, data, len);
            break;

        // 0x02: 读取串口信息
        case 0x02:
            get_serial_port_info(client_fd, data, len);
            break;

        // 0x03: 读取串口错误
        case 0x03:
            get_serial_port_errors(client_fd, data, len);
            break;
        default:
            LOG_ERROR("  ERROR: Invalid sub ID: 0x%02X\n", sub_id);
            send_report_response(client_fd, data[2], sub_id, FAILED);
            break;
    }
  
}
