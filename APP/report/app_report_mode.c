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
    uint8_t response[1024];
    int offset = 0;  
    int i;
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];
    uint8_t port_index;
    uint8_t op_mode;
    uint8_t num_ports;
    
    if(len < 0)
    {
        LOG_ERROR("  Invalid data length: %d\n", len);
    }
    LOG_INFO("  Action: Operating Mode Settings, MainID=0x%02X, SubID=0x%02X\n", main_id, sub_id);
    
    switch (sub_id)
    {
        case 0x01:
        	port_index = data[4];
            
            /* 帧头 */
            response[offset++] = 0xA5; 
            response[offset++] = 0xA5;
            response[offset++] = main_id; 
            response[offset++] = sub_id;
            response[offset++] = port_index;

            semTake(g_config_mutex, WAIT_FOREVER);
            {
                int ch_idx = port_index - 1;
                ChannelState* ch = &g_system_config.channels[ch_idx];
                
                response[offset++] = (unsigned char)ch->op_mode;
                
                LOG_INFO("  Port %d: Op Mode = %d\n", port_index, ch->op_mode);
                
            }

            semGive(g_config_mutex); 
            /* 帧尾 */
            response[offset++] = 0x5A; 
            response[offset++] = 0x5A;
            send(client_fd, (char*)response, offset, 0);
            LOG_INFO("  Sent Operating Mode Settings response (%d bytes)\n", offset);

            break;
            
        case 0x02:

            /* 读取全部端口的操作模式 */
            /* 帧头 */
            response[offset++] = 0xA5; 
            response[offset++] = 0xA5;
            response[offset++] = main_id; 
            response[offset++] = sub_id;

            semTake(g_config_mutex, WAIT_FOREVER);
            
            response[offset++] = NUM_PORTS;
            
            for (i = 0; i < NUM_PORTS; i++)
            {
                ChannelState* ch = &g_system_config.channels[i];
                response[offset++] = i + 1;                  /* 端口号 */
                response[offset++] = (unsigned char)ch->op_mode; /* 操作模式 */
                LOG_INFO("  Port %d: Op Mode = %d\n", i + 1, ch->op_mode);
            }
            
            semGive(g_config_mutex);
            
            /* 帧尾 */
            response[offset++] = 0x5A; 
            response[offset++] = 0x5A;
            
            send(client_fd, (char*)response, offset, 0);
            LOG_INFO("  Sent all ports operating modes (%d bytes, %d ports)\n", offset, NUM_PORTS);
            break;
            
        case 0x03:
            /* 预留功能 */
            LOG_INFO("  SubID 0x03: Reserved\n");
            send_report_response(client_fd, 0x06, sub_id, FAILED);
            break;
            
        default:
            LOG_ERROR("  ERROR: Unsupported sub_id 0x%02X\n", sub_id);
            send_report_response(client_fd, 0x06, sub_id, FAILED);
            break;
    }   
}
