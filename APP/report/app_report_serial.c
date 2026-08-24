/*
 * =====================================================================================
 *
 * Filename:  app_report_serial.c
 *
 * Description:  串口设置和串口模式相关处理函数
 *
 * =====================================================================================
 */

#include "app_report_serial.h"



/**
 * @brief 处理0x04 - 0x01 读取串口设置
 */
void handle_read_serial_port_settings(int client_fd, unsigned char *data, int len)
{
    unsigned char response[1024];
    int offset = 0;
    LOG_INFO(" handle_read_serial_port_settings\n");
    if (len < 6) {
        LOG_ERROR("  ERROR: Data length insufficient\n");
        return;
    }
    
    unsigned char port_index = data[4];
    /* 0xFF 表示读取所有端口，1~NUM_PORTS 表示单个端口 */
    if (port_index != 0xFF && (port_index < 1 || port_index > NUM_PORTS)) 
    {
        LOG_ERROR("  ERROR: Invalid port index %d\n", port_index);
        return;
    }
    
    int ch_idx = (port_index == 0xFF) ? -1 : (port_index - 1);
    response[offset++] = 0xA5; response[offset++] = 0xA5;
    response[offset++] = 0x04; response[offset++] = 0x01;
    
    if (port_index == 0xFF)
    {
        /* 上报所有串口的设置 */
        semTake(g_config_mutex, WAIT_FOREVER);
        for (int i = 0; i < NUM_PORTS; i++)
        {
            ChannelState* ch = &g_system_config.channels[i];
            
            response[offset++] = i + 1;
            
            unsigned char alias_len = strlen(ch->alias);
            if (alias_len > MAX_ALIAS_LEN)
                alias_len = MAX_ALIAS_LEN;
            response[offset++] = alias_len;
            memcpy(&response[offset], ch->alias, MAX_ALIAS_LEN);
            offset += MAX_ALIAS_LEN;
            
            unsigned int baud = ch->baudrate;
            memcpy(&response[offset], &baud, 4);
            offset += 4;
            
            response[offset++] = ch->data_bits;
            response[offset++] = ch->stop_bits;
            response[offset++] = ch->parity;
            response[offset++] = ch->flow_ctrl;
            response[offset++] = ch->interface_type;
        }
        semGive(g_config_mutex);
    }
    
    else 
    {
        semTake(g_config_mutex, WAIT_FOREVER);
        {
            ChannelState* ch = &g_system_config.channels[ch_idx];
            
            /* 端口索引 */
            response[offset++] = port_index;
            
            /* 别名 */
            unsigned char alias_len = strlen(ch->alias);
            if (alias_len > MAX_ALIAS_LEN) 
                alias_len = MAX_ALIAS_LEN;
            response[offset++] = alias_len;
            memcpy(&response[offset], ch->alias, MAX_ALIAS_LEN);
            offset += MAX_ALIAS_LEN;
            
            /* 波特率 */
            unsigned int baud = ch->baudrate;
            memcpy(&response[offset], &baud, 4);
            offset += 4;
            
            /* 数据位、停止位、校验位 */
            response[offset++] = ch->data_bits;
            response[offset++] = ch->stop_bits;
            response[offset++] = ch->parity;
            response[offset++] = ch->flow_ctrl;
            response[offset++] = ch->interface_type;
        }
        semGive(g_config_mutex);
    }


    response[offset++] = 0x5A; response[offset++] = 0x5A;
    send(client_fd, (char*)response, offset, 0);
    LOG_INFO("  Sent Serial Port %d settings\n", port_index);
}



/**
 * @brief 处理0x04 - 0x02 设置单个串口
 */
void handle_set_single_serial_port(int client_fd, unsigned char *data, int len)
{
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];
    

    //检查设备是否锁定
    if (is_device_locked()) 
    {
        LOG_ERROR("  ERROR: Device is locked, cannot update serial port settings\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    //打印接收到的数据
    LOG_DEBUG("  Received Set Single Serial Port data: ");
    int i = 0;
    for ( i = 0; i < len; i++) 
    {
        LOG_DEBUG("%02X ", data[i]);
    }
    LOG_DEBUG("\n");
    
    /* 解析负载数据 */
    int data_idx = 4; /* 从帧头后开始 */
    unsigned char port_index = data[data_idx++];
    
    if (port_index < 1 || port_index > NUM_PORTS) 
    {
        LOG_ERROR("  ERROR: Invalid port index %d\n", port_index);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    int ch_idx = port_index - 1;
    
    /* 别名 */
    unsigned char alias_len = data[data_idx++];
    if (alias_len > MAX_ALIAS_LEN) 
    {
    	alias_len = MAX_ALIAS_LEN;
    }
    	
    char new_alias[alias_len];
    memset(new_alias, 0, sizeof(new_alias));
    memcpy(new_alias, &data[data_idx], alias_len);
    data_idx += alias_len;

    //打印new_alias数组
    for (i = 0; i < alias_len; i++) 
    {
        LOG_DEBUG("%02X ", new_alias[i]);
    }
    
    /* 波特率 */
    unsigned int new_baudrate;

    new_baudrate = data[data_idx] << 24 | data[data_idx + 1] << 16 | data[data_idx + 2] << 8 | data[data_idx + 3];
    data_idx += 4;
    
    /* 串口参数 */
    unsigned char new_data_bits = data[data_idx++];
    unsigned char new_stop_bits = data[data_idx++];
    unsigned char new_parity = data[data_idx++];
    unsigned char new_flow_ctrl = data[data_idx++];
    //unsigned char new_fifo = data[data_idx++];
    unsigned char interface_type = data[data_idx++];
    
    /* 更新配置 */
    semTake(g_config_mutex, WAIT_FOREVER);
    {
        ChannelState* ch = &g_system_config.channels[ch_idx];
        strncpy(ch->alias, new_alias, alias_len);
        ch->alias[alias_len] = '\0';
        
        ch->baudrate = new_baudrate;
        ch->data_bits = new_data_bits;
        ch->stop_bits = new_stop_bits;
        ch->parity = new_parity;
        ch->flow_ctrl = new_flow_ctrl;
        //ch->fifo_enable = new_fifo;
        ch->interface_type = interface_type;
        
        LOG_INFO("  Port %d: alias=%s, baud=%d, data=%d, stop=%d, parity=%d, flow=%d, interface=%d\n",
                 port_index, ch->alias, ch->baudrate, ch->data_bits, 
                 ch->stop_bits, ch->parity, ch->flow_ctrl,  ch->interface_type);
    }
    semGive(g_config_mutex);
    
    /* 发送成功ACK */  
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
    LOG_INFO("  Serial port %d configuration updated successfully\n", port_index);
}

/**
 * @brief 处理0x04 - 0x03 设置多个串口为同样的参数
 */
void handle_set_multiple_serial_ports(int client_fd, unsigned char *data, int len)
{
    int i;
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];

    LOG_INFO("  Action: Set Multiple Serial Ports, MainID=0x%02X, SubID=0x%02X\n", main_id, sub_id);

    //数据格式：帧头(2)+功能(1)+子ID(1)+端口数量(1)+端口号(根据端口数量)+端口配置(每个端口配置: 端口索引(1)+波特率(4)+数据位(1)+停止位(1)+校验位(1)+流控(1)+接口类型(1)) + 帧尾(2)

    LOG_DEBUG("  Received Set Multiple Serial Ports data: ");
    for (i = 0; i < len; i++) 
    {
        LOG_DEBUG("%02X ", data[i]);
    }
    LOG_DEBUG("\n");


    /* 检查设备是否被锁定 */    
    if (is_device_locked()) 
    {
        LOG_ERROR("  ERROR: Device is locked, cannot update serial port settings\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    /* 解析负载中的端口数量和配置 */
    if (len < 6) 
    {
        LOG_ERROR("  ERROR: Data length insufficient for set multiple ports\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    
    int data_idx = 4;
    unsigned char port_count        = data[data_idx++];
    unsigned char port_params_index = 5 + port_count;
    
    LOG_INFO("  Setting %d serial port(s) configuration\n", port_count);
    LOG_INFO("  port_params_index: %d\n", port_params_index);
    
    /* 每个端口配置: 波特率(4)+数据位(1)+停止位(1)+校验位(1)+流控(1)+接口类型(1) = 10字节 */
    int expected_len = 2 + 1 + 1 + 1 +1 + 9 + 2; /*  最少17字节 */
    if (len < expected_len) 
    {
        LOG_ERROR("  ERROR: Data length mismatch (expected %d, got %d)\n", expected_len, len);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    semTake(g_config_mutex, WAIT_FOREVER);
    for (i = 0; i < port_count; i++) 
    {

        //5a 5a 04 03 02 01 02  00 00 00 00 00 00 00 00 00 a5 a5 
        unsigned char port_index = data[data_idx++];
        if (port_index < 1 || port_index > NUM_PORTS) 
        {
            LOG_WARN("  WARNING: Skipping invalid port index %d\n", port_index);
            data_idx += 28; /* 跳过这个端口的数据 */
            continue;
        }
        
        int ch_idx = port_index - 1;

        //打印端口索引
        LOG_DEBUG("  ch_idx:%d\n", ch_idx);

        ChannelState* ch = &g_system_config.channels[ch_idx];
        
        /* 波特率 数据位应该从*/
        //unsigned int baudrate;
        ch->baudrate = data[port_params_index] << 24 | data[port_params_index + 1] << 16 | data[port_params_index + 2] << 8 | data[port_params_index + 3];
        port_params_index += 4;
        
        /* 串口参数 */
        ch->data_bits       = data[port_params_index++];
        ch->stop_bits       = data[port_params_index++];
        ch->parity          = data[port_params_index++];
        ch->flow_ctrl       = data[port_params_index++];
        ch->interface_type  = data[port_params_index++];

        //LOG_DEBUG(" before port_params_index: %d\n", port_params_index);
        //返回到波特率索引
        port_params_index -= 9;

        //LOG_DEBUG(" after port_params_index: %d\n", port_params_index);
        
        LOG_INFO("Port %d updated: baud=%d, data=%d, stop=%d, parity=%d, flow=%d, interface=%d\n", port_index, ch->baudrate, ch->data_bits, ch->stop_bits, ch->parity, ch->flow_ctrl, ch->interface_type);
    }
    semGive(g_config_mutex);
    
    /* 发送成功ACK */
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
    LOG_INFO("  %d serial port(s) configuration updated successfully\n", port_count);
}

/**
 * @brief 处理0x04 - 串口设置操作
 */
void handle_serial_port_settings(int client_fd, unsigned char *data, int len)
{
    unsigned char sub_id = data[3];
    
    LOG_INFO("  Action: Serial Port Settings, SubID=0x%02X\n", sub_id);
    
    switch (sub_id) 
    {
        case 0x01: /* 读取串口设置 */
            handle_read_serial_port_settings(client_fd, data, len);
            break;
            
        case 0x02: /* 设置单个串口 */
            handle_set_single_serial_port(client_fd, data, len);
            break;
            
        case 0x03: /* 设置多个串口 */
            handle_set_multiple_serial_ports(client_fd, data, len);
            break;
            
        default:
            LOG_ERROR("  ERROR: Unsupported sub_id 0x%02X\n", sub_id);
            break;
    }
}






/**
 * @brief 处理0x05 - 串口模式设置
 */
void handle_serial_port_mode(int client_fd, unsigned char *data, int len)
{
    
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];
    LOG_INFO("  Action: Set Serial Port Mode, MainID=0x%02X, SubID=0x%02X\n", main_id, sub_id);
    
    switch (sub_id) {
        case 0x01: /* 设置单个串口为RealCom模式 */
            handle_set_single_realcom_mode(client_fd, data, len);
            break;
            
        case 0x02: /* 设置多个串口为RealCom模式 */
            handle_set_multiple_realcom_mode(client_fd, data, len);
            break;
            
        case 0x03: /* 设置单个串口为TCP Server模式 */
            handle_set_single_tcp_server_mode(client_fd, data, len);
            break;
            
        case 0x04: /* 设置多个串口为TCP Server模式 */
            handle_set_multiple_tcp_server_mode(client_fd, data, len);
            break;
            
        default:
        {
            LOG_ERROR("  ERROR: Unsupported sub_id 0x%02X\n", sub_id);
            send_report_response(client_fd, main_id, sub_id, FAILED);      
            break;
        }
        
 
    }
}
/**
 * @brief 处理0x05 - 0x01 设置单个串口为RealCom模式
 */
void handle_set_single_realcom_mode(int client_fd, unsigned char *data, int len)
{
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];
    LOG_INFO("  Action: Set Single RealCom Mode, MainID=0x%02X, SubID=0x%02X\n", main_id, sub_id);
    /* 检查设备是否被锁定 */
    if (is_device_locked()) 
    {
        LOG_ERROR("  ERROR: Device is locked, cannot update serial port settings\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    //5a 5a 05 01 01 01 02 12 a5 a5

    if (len < 10 || len > 10) 
    {
        LOG_ERROR("  ERROR: Data length insufficient\n");
        
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    uint8_t port_index = data[4];
    LOG_INFO("  Port Index: %d\n", port_index);


    LOG_INFO("  Port Mode: %d\n", data[5]);

    uint8_t port_connect_max = data[6];
    LOG_INFO("  Port Connect Max: %d\n", port_connect_max);

    uint8_t Tcp_connect_timeout = data[7];
    LOG_INFO("  Tcp_connect_timeout: %d\n", Tcp_connect_timeout);

    if (port_index < 1 || port_index > NUM_PORTS) 
    {
        LOG_ERROR("  ERROR: Invalid port index %d\n", port_index);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    /* 解析RealCom模式参数（如果有额外参数的话） */
    int ch_idx = port_index - 1;
    semTake(g_config_mutex, WAIT_FOREVER);
    g_system_config.channels[ch_idx].op_mode = OP_MODE_REAL_COM;
    g_system_config.channels[ch_idx].max_connections = port_connect_max;
    g_system_config.channels[ch_idx].tcp_alive_check_time_min = Tcp_connect_timeout;
    semGive(g_config_mutex);
    
    LOG_INFO("  Port %d set to RealCom mode\n", port_index);
    
    /* 发送ACK */
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
    LOG_INFO("  Port %d set to RealCom mode successfully\n", port_index);
}

/**
 * @brief 处理0x05 - 0x02 设置多个串口为RealCom模式
 */
void handle_set_multiple_realcom_mode(int client_fd, unsigned char *data, int len)
{
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];
    uint8_t port_mode;
    LOG_INFO("  Action: Set Multiple RealCom Mode, MainID=0x%02X, SubID=0x%02X\n", main_id, sub_id);
    
    if (is_device_locked()) 
    {
        LOG_ERROR("  ERROR: Device is locked, cannot update serial port settings\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }


    //5a 5a 05 02     02 04 05     01 03 07 a5 a5
    if (len < 11) 
    {
        LOG_ERROR("  ERROR: Data length insufficient\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    
    unsigned char port_count = data[4];
    int data_idx = 5;
    int i;

    port_mode = data[5 + port_count];
    uint8_t port_connect_max = data[6 + port_count];
    uint8_t Tcp_connect_timeout = data[7 + port_count];
    
    semTake(g_config_mutex, WAIT_FOREVER);
    for (i = 0; i < port_count; i++)
    {
        unsigned char port_index = data[data_idx++];
        if (port_index >= 1 && port_index <= NUM_PORTS)
        {
            g_system_config.channels[port_index - 1].op_mode = port_mode;
            g_system_config.channels[port_index - 1].max_connections = port_connect_max;
            g_system_config.channels[port_index - 1].tcp_alive_check_time_min = Tcp_connect_timeout;
            LOG_INFO("  Port %d set to RealCom mode max_connections: %d, tcp_alive_check_time_min: %d\n", port_index, port_connect_max, Tcp_connect_timeout);
        }
    }
    semGive(g_config_mutex);
    
    /* 发送ACK */   
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
    LOG_INFO("  %d serial port(s) set to RealCom mode successfully\n", port_count);
}

/**
 * @brief 处理0x05 - 0x03 设置单个串口为TCP Server模式
 */
void handle_set_single_tcp_server_mode(int client_fd, unsigned char *data, int len)
{
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];
    LOG_INFO("  Action: Set Single TCP Server Mode, MainID=0x%02X, SubID=0x%02X\n", main_id, sub_id);
    
    /* 检查设备是否被锁定 */
    if (is_device_locked()) 
    {
        LOG_ERROR("  ERROR: Device is locked, cannot update serial port settings\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    if (len < 10 || len > 10) 
    {
        LOG_ERROR("  ERROR: Data length insufficient\n");
        
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    uint8_t port_index = data[4];
    LOG_INFO("  Port Index: %d\n", port_index);

    
    LOG_INFO("  Port Mode: %d\n", data[5]);

    uint8_t port_connect_max = data[6];
    LOG_INFO("  Port Connect Max: %d\n", port_connect_max);

    uint8_t Tcp_connect_timeout = data[7];
    LOG_INFO("  Tcp_connect_timeout: %d\n", Tcp_connect_timeout);

    if (port_index < 1 || port_index > NUM_PORTS) 
    {
        LOG_ERROR("  ERROR: Invalid port index %d\n", port_index);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    /* 解析TCP Server模式参数（如果有额外参数的话） */
    int ch_idx = port_index - 1;
    semTake(g_config_mutex, WAIT_FOREVER);
    g_system_config.channels[ch_idx].op_mode = OP_MODE_TCP_SERVER;
    g_system_config.channels[ch_idx].max_connections = port_connect_max;
    g_system_config.channels[ch_idx].tcp_alive_check_time_min = Tcp_connect_timeout;
    semGive(g_config_mutex);
    
    LOG_INFO("  Port %d set to TCP Server mode\n", port_index);
    
    /* 发送ACK */
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
    LOG_INFO("  Port %d set to TCP Server mode successfully\n", port_index);
}

/**
 * @brief 处理0x05 - 0x04 设置多个串口为TCP Server模式
 */
void handle_set_multiple_tcp_server_mode(int client_fd, unsigned char *data, int len)
{
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];
    //uint8_t port_mode;
    LOG_INFO("  Action: Set Multiple TCP Server Mode, MainID=0x%02X, SubID=0x%02X\n", main_id, sub_id);
    
    if (is_device_locked()) 
    {
        LOG_ERROR("  ERROR: Device is locked, cannot update serial port settings\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }


    if (len < 11) 
    {
        LOG_ERROR("  ERROR: Data length insufficient\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    
    unsigned char port_count = data[4];
    int data_idx = 5;
    int i;

    //port_mode = data[5 + port_count];
    uint8_t port_connect_max = data[6 + port_count];
    uint8_t Tcp_connect_timeout = data[7 + port_count];
    
    semTake(g_config_mutex, WAIT_FOREVER);
    for (i = 0; i < port_count; i++)
    {
        unsigned char port_index = data[data_idx++];
        if (port_index >= 1 && port_index <= NUM_PORTS)
        {
            g_system_config.channels[port_index - 1].op_mode = OP_MODE_TCP_SERVER;
            g_system_config.channels[port_index - 1].max_connections = port_connect_max;
            g_system_config.channels[port_index - 1].tcp_alive_check_time_min = Tcp_connect_timeout;
            LOG_INFO("  Port %d set to TCP Server mode max_connections: %d, tcp_alive_check_time_min: %d\n", port_index, port_connect_max, Tcp_connect_timeout);
        }
    }
    semGive(g_config_mutex);
    
    /* 发送ACK */   
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
    LOG_INFO("  %d serial port(s) set to TCP Server mode successfully\n", port_count);
}




#if 0
/**
 * @brief 串口配置结构体
 * @details 仅包含串口的基本物理参数，用于配置传递和临时使用
 */
typedef struct {
    int baudrate;              /**< 波特率 (例如: 9600, 115200, 230400等) */
    unsigned char data_bits;   /**< 数据位 (5/6/7/8) */
    unsigned char stop_bits;   /**< 停止位 (1/2) */
    unsigned char parity;      /**< 校验位 (0=无校验, 1=奇校验, 2=偶校验) */
} SerialPortParams;

/**
 * @brief 串口工作状态结构体
 */
 typedef struct {
    uint8_t port_index;         /**< 串口索引 */
    SerialPortParams params;   /**< 串口配置参数 */
    uint8_t is_connected;     /**< 是否连接 */
    uint8_t is_lockd;         /**< 是否锁定 */
    uint8_t interface_type;   /**< 接口类型 */
    uint8_t work_mode;        /**< 工作模式 */
} SerialPortWorkingStatus;
/**
 * @brief 获取指定端口信息
 * @param port_index 串口索引 (0-NUM_PORTS-1)
 * @param status 输出参数，填充串口工作状态信息
 * @return 0=成功, -1=失败(无效索引)
 */
static int get_port_info(uint8_t port_index, SerialPortWorkingStatus *status)
{
    if (port_index >= NUM_PORTS || status == NULL) {
        LOG_ERROR("get_port_info: Invalid port_index=%d\n", port_index);
        return -1;
    }
    
    ChannelState* ch = &g_system_config.channels[port_index];
    
    /* 填充串口索引 */
    status->port_index = port_index;
    
    /* 填充串口配置参数 */
    status->config.baudrate = ch->baudrate;
    status->config.data_bits = ch->data_bits;
    status->config.stop_bits = ch->stop_bits;
    status->config.parity = ch->parity;
    
    /* 填充工作状态信息 */
    status->is_connected = ch->is_connected;
    status->is_lockd = ch->is_locked;
    status->interface_type = ch->interface_type;
    status->work_mode = (uint8_t)ch->op_mode;
    
    LOG_DEBUG("get_port_info: Port %d: baud=%d, data=%d, stop=%d, parity=%d, connected=%d, locked=%d\n",
              port_index, status->config.baudrate, status->config.data_bits, 
              status->config.stop_bits, status->config.parity,
              status->is_connected, status->is_lockd);
    
    return 0;
}
#endif
