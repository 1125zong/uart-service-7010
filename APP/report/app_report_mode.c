/*
 * =====================================================================================
 *
 * Filename:  app_report_mode.c
 *
 * Description:  操作模式设置相关处理函数
 *
 * =====================================================================================
 */

#include "app_report_mode.h"
#include "app_dev.h"

/**
 * @brief 处理0x06 - 读取各串口模式: realcom/tcp server
 */
void handle_read_operating_mode(int client_fd, unsigned char *data, int len)
{
    if (data == NULL || len < 6)
    {
        return;
    }

    const uint8_t sub_id = data[3];
    const int all_ports = sub_id == 0x02;

    if ((sub_id != 0x01 && sub_id != 0x02) || len != 7 || (all_ports && data[4] != 0xFF) || (!all_ports && (data[4] < 1 || data[4] > NUM_PORTS)))
    {
        send_report_response(client_fd, 0x06, sub_id, FAILED);
        return;
    }

    if (semTake(g_config_mutex, WAIT_FOREVER) != OK)
    {
        send_report_response(client_fd, 0x06, sub_id, FAILED);
        return;
    }

    uint8_t response[4 + 1 + NUM_PORTS * 4 + 2] = {0xA5, 0xA5, 0x06, sub_id};
    int offset = 4;
    const int first_port = all_ports ? 1 : data[4];
    const int count = all_ports ? NUM_PORTS : 1;

    if (all_ports)
    {
        response[offset++] = count;
    }

    for (int port = first_port; port < first_port + count; ++port)
    {
        const ChannelState *ch = &g_system_config.channels[port - 1];
        response[offset++] = port;
        response[offset++] = ch->op_mode;
        response[offset++] = ch->max_connections;
        response[offset++] = ch->tcp_alive_check_time_min;
    }

    semGive(g_config_mutex);

    response[offset++] = 0x5A;
    response[offset++] = 0x5A;
    send(client_fd, (const char *)response, offset, 0);
}