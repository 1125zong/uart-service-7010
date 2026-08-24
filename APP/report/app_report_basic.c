
#include "app_report_basic.h"
#include "app_dev.h"



extern void send_report_response(int client_fd, const unsigned char main_id,  unsigned char sub_id, unsigned char status);



/**
 * @brief 串口配置结构体
 * @details 仅包含串口的基本物理参数，用于配置传递和临时使用
 */
typedef struct {
    int baudrate;              /**< 波特率 (例如: 9600, 115200, 230400等) */
    unsigned char data_bits;   /**< 数据位 (5/6/7/8) */
    unsigned char stop_bits;   /**< 停止位 (1/2) */
    unsigned char parity;      /**< 校验位 (0=无校验, 1=奇校验, 2=偶校验) */
} SerialPortConfig;

/**
 * @brief 串口工作状态结构体
 */
 typedef struct {
    uint8_t port_index;         /**< 串口索引 */
    SerialPortConfig config;   /**< 串口配置参数 */
    uint8_t is_connected;     /**< 是否连接 */
    uint8_t is_lockd;         /**< 是否锁定 */
    uint8_t interface_type;   /**< 接口类型 */
    uint8_t work_mode;        /**< 工作模式 */
} SerialPortWorkingStatus;



/* 各端口状态 */
SerialPortWorkingStatus all_ports[NUM_PORTS];

/*
 * @brief  : 打印串口各端口状态信息
*/

static void print_serial_port_status(void)
{ 
    int i = 0;
    LOG_INFO("Serial Port Status:\n");
    for (i = 0; i < NUM_PORTS; i++) {
        LOG_INFO("Port %d: baud=%d, data=%d, stop=%d, parity=%d, connected=%d, locked=%d, interface=%d, mode=%d\n",
                  all_ports[i].port_index, all_ports[i].config.baudrate, all_ports[i].config.data_bits, 
                  all_ports[i].config.stop_bits, all_ports[i].config.parity, all_ports[i].is_connected, 
                  all_ports[i].is_lockd, all_ports[i].interface_type, all_ports[i].work_mode);
    }
}


/**
 * @brief 从全局配置的通道中获取指定串口通道的工作状态信息
 * @param port_index 串口索引 (1-NUM_PORTS)
 * @param status 输出参数，填充串口工作状态信息
 * @return 0=成功, -1=失败(无效索引)
 */

#if 0
static int get_serial_port_config(uint8_t port_index, SerialPortWorkingStatus *status)
{
    if (port_index < 1 || port_index > NUM_PORTS || status == NULL) {
        LOG_ERROR("get_serial_port_config: Invalid port_index=%d\n", port_index);
        return -1;
    }
    
    int ch_idx = port_index - 1;
    
    semTake(g_config_mutex, WAIT_FOREVER);
    {
        ChannelState* ch = &g_system_config.channels[ch_idx];
        
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
    }
    semGive(g_config_mutex);
    
    LOG_DEBUG("get_serial_port_config: Port %d: baud=%d, data=%d, stop=%d, parity=%d, connected=%d, locked=%d\n",
              port_index, status->config.baudrate, status->config.data_bits, 
              status->config.stop_bits, status->config.parity,
              status->is_connected, status->is_lockd);
    
    return 0;
}
#endif
/**
 * @brief 获取所有串口通道的工作状态信息
 * @param status_array 输出参数，存储所有通道状态的数组，长度必须>=NUM_PORTS
 * @return 成功获取的通道数量
 */
static int get_all_serial_ports_config(SerialPortWorkingStatus status_array[NUM_PORTS])
{
    int i;
    int success_count = 0;
    
    if (status_array == NULL) {
        LOG_ERROR("get_all_serial_ports_config: status_array is NULL\n");
        return 0;
    }
    
    LOG_INFO("get_all_serial_ports_config: Retrieving %d channels' status\n", NUM_PORTS);
    
    //semTake(g_config_mutex, WAIT_FOREVER);
    {
        for (i = 0; i < NUM_PORTS; i++) {
            ChannelState* ch = &g_system_config.channels[i];
            SerialPortWorkingStatus* status = &status_array[i];
            
            /* 填充串口索引 (1-based) */
            status->port_index = i;
            
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
            
            success_count++;
        }
    }
    //semGive(g_config_mutex);
    
    LOG_INFO("get_all_serial_ports_config: Successfully retrieved %d channels' status\n", success_count);
    
    return success_count;
}


/**
 * @brief 处理0x01 - 读取设备概述信息
 * 
 * @details 报文格式（二进制）：
 * [帧头 2B] [功能ID 1B] [子ID 1B] [数据长度 2B] [载荷] [帧尾 2B]
 * 
 * 载荷结构：
 * - 设备基础信息块（固定长度）
 * - 串口数量 (1B)
 * - N个串口信息块（每个15B）
 * 
 * 接收端解析步骤：
 * 1. 验证帧头(0xA55A)和帧尾(0x5A5A)
 * 2. 读取数据长度字段
 * 3. 解析固定长度的设备基础信息
 * 4. 读取串口数量N
 * 5. 循环读取N个串口信息块
 */


static void serial_no_to_bytes(const DevSerialNo* sn, unsigned char out[8])
{
    unsigned int number = sn->number;
    out[0] = (unsigned char)((sn->year >> 8) & 0xFF);
    out[1] = (unsigned char)(sn->year & 0xFF);
    out[2] = (unsigned char)(sn->month & 0xFF);
    out[3] = (unsigned char)((sn->day) & 0xFF);
    out[4] = (unsigned char)((number >> 24) & 0xFF);
    out[5] = (unsigned char)((number >> 16) & 0xFF);
    out[6] = (unsigned char)((number >> 8) & 0xFF);
    out[7] = (unsigned char)(number & 0xFF);
}


/**
 * @brief 填充设备概述响应数据
 * @param response 存储响应数据的缓冲区
 * @param[out] offset 填充数据后的偏移量
 * @param count 串口数量
 * @return 0=成功, -1=失败
 */
static int fill_device_overview_response(unsigned char *response, int *offset, int count)
{
    int i;
    DeviceSettings* dev = &g_system_config.device;
    unsigned char sn[8];
    
    /* ========== 设备基础信息块 ========== */
    
    /* 型号名称 (40字节，不足补0) */
    memset(&response[*offset], 0, 40);
    strncpy((char*)&response[*offset], dev->model_name, MAX_MODEL_NAME_LEN);
    *offset += 40;
    
    /* 序列号 (8字节) */
    serial_no_to_bytes(&dev->serial_no, sn);
    memcpy(&response[*offset], sn, 8);
    *offset += 8;
    
    /* 设备名称 (40) */
    memset(&response[*offset], 0, 40);
    strncpy((char*)&response[*offset], dev->device_name, MAX_DEVICE_NAME_LEN);
    *offset += 40;
    
    /* 固件版本 (4字节) */
    response[(*offset)++] = dev->firmware_version[0];
    response[(*offset)++] = dev->firmware_version[1];
    response[(*offset)++] = dev->firmware_version[2];
    response[(*offset)++] = dev->firmware_version[3];
    
    /* 设备状态 (1字节) */
    response[(*offset)++] = dev->status;
    
    /* 网口数量 (1字节) */
    response[(*offset)++] = dev->lan_count;
    
    /* IP地址1 (4字节) */
    unsigned int ip1 = (dev->ip_address1);
    memcpy(&response[*offset], &ip1, 4);
    *offset += 4;
    
    /* MAC地址1 (6字节) */
    memcpy(&response[*offset], dev->mac_address1, 6);
    *offset += 6;
    
    /* LAN1速度 (1字节) */
    response[(*offset)++] = dev->lan_speed1;
    
    /* IP地址2 (4字节，网络字节序) */
    unsigned int ip2 = (dev->ip_address2);
    memcpy(&response[*offset], &ip2, 4);
    *offset += 4;
    
    /* MAC地址2 (6字节) */
    memcpy(&response[*offset], dev->mac_address2, 6);
    *offset += 6;
    
    /* LAN2速度 (1字节) */
    response[(*offset)++] = dev->lan_speed2;
    
    /* LCM在位 (1字节) */
    response[(*offset)++] = dev->lcm_present;
    
    /* 电源1状态 (1字节) */
    response[(*offset)++] = dev->power1_status;
    
    /* 电源2状态 (1字节) */
    response[(*offset)++] = dev->power2_status;
    
    /* 接口类型数量 (4字节) */
    unsigned int iface_count = htonl(dev->interface_type_count);
    memcpy(&response[*offset], &iface_count, 4);
    *offset += 4;
    
    /* 接口状态 (16字节) */
    memcpy(&response[*offset], dev->interface_status, NUM_PORTS);
    *offset += 16;
    
    /* 设备锁定状态 (1字节) */
    response[(*offset)++] = dev->lock_status;
    
    //printf("basic dev->lock_status: %x\n", dev->lock_status);
    
    /* 接口锁定状态 (16字节) */
    memcpy(&response[*offset], dev->interface_lock_status, NUM_PORTS);
    *offset += 16;        
    
    /* ========== 串口信息块 ========== */
    
    /* 每个串口的信息 (每个15字节) */
    for (i = 0; i < count; i++) 
    {
        /* 端口索引 (1字节) */
        response[(*offset)++] = all_ports[i].port_index;
        
        /* 波特率 (4字节) */
        unsigned int baud = all_ports[i].config.baudrate;
        memcpy(&response[*offset], &baud, 4);
        *offset += 4;
        
        /* 数据位 (1字节) */
        response[(*offset)++] = all_ports[i].config.data_bits;
        
        /* 停止位 (1字节) */
        response[(*offset)++] = all_ports[i].config.stop_bits;
        
        /* 校验位 (1字节) */
        response[(*offset)++] = all_ports[i].config.parity;
        
        /* 连接状态 (1字节) */
        response[(*offset)++] = all_ports[i].is_connected;
        
        /* 锁定状态 (1字节) */
        response[(*offset)++] = all_ports[i].is_lockd;
        
        /* 接口类型 (1字节) */
        response[(*offset)++] = all_ports[i].interface_type;
        
        /* 工作模式 (1字节) */
        response[(*offset)++] = all_ports[i].work_mode;
        
        /* 预留字段 (2字节，用于未来扩展) */
        response[(*offset)++] = 0x00;
        response[(*offset)++] = 0x00;
    }
    
    return 0;
}


void handle_device_overview(int client_fd, unsigned char *data, int len)
{
    LOG_INFO("====Action: Read Device Overview====\n");

    if(data == NULL)
    {
        LOG_ERROR("  ERROR: Invalid data pointer\n");
        return;
    }
    if(len < 0)
    {
        LOG_ERROR("  ERROR: Invalid data length\n");
    }

    int count;
    unsigned char response[2048];  
    int offset = 0;
    
    /* 帧头 */
    response[offset++] = 0xA5;
    response[offset++] = 0xA5;
    
    /* 功能ID和子ID */
    response[offset++] = 0x01;
    response[offset++] = 0x01;
    
    semTake(g_config_mutex, WAIT_FOREVER);
    {
        /* 获取串口各端口状态 */
        count = get_all_serial_ports_config(all_ports);
        print_serial_port_status();
        
        /* 填充响应数据 */
        fill_device_overview_response(response, &offset, count);
    }
    semGive(g_config_mutex);
    
    /* 帧尾 */
    response[offset++] = 0x5A;
    response[offset++] = 0x5A;

    /* 发送响应 */
    send(client_fd, (char*)response, offset, 0);
    LOG_INFO("  Sent Overview response (%d bytes, %d ports)\n", offset, count);
}


/**
 * @brief 处理0x02 - 设备名字操作
 */
void handle_device_name_operation(int client_fd, unsigned char *data, int len)
{
    unsigned char response[1024];
    int offset = 0;  
    int i;
    int ret = 0;
    unsigned char main_id = data[2];
    unsigned char sub_id = data[3];
    
    LOG_INFO("  Action: Device Name Operation, main_id=0x%02X  SubID=0x%02X\n", main_id, sub_id);

    //设备被锁定，直接返回
    if (is_device_locked())        
    {
        LOG_ERROR("  ERROR: Device is locked, cannot update device name\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    if (sub_id == 0x00) 
    {    /* 读取设备名字 */
        response[offset++] = 0xA5; 
        response[offset++] = 0xA5;
        response[offset++] = main_id; 
        response[offset++] = sub_id;
        
        semTake(g_config_mutex, WAIT_FOREVER);
        {
            unsigned char name_len = strlen(g_system_config.device.device_name);
            if (name_len > MAX_DEVICE_NAME_LEN) 
            {
            	name_len = MAX_DEVICE_NAME_LEN;
            }           	
            response[offset++] = name_len;
            memcpy(&response[offset], g_system_config.device.device_name, MAX_DEVICE_NAME_LEN);
            offset += MAX_DEVICE_NAME_LEN;
        }
        semGive(g_config_mutex);
        
        response[offset++] = 0x5A; 
        response[offset++] = 0x5A;
        ret = send(client_fd, (char*)response, offset, 0);
        if (ret < 0) 
        {
            LOG_ERROR("  ERROR: Failed to send response\n");
            
        }
        LOG_INFO("  Sent Device Name read response\n");
        
    } 
    else if (sub_id == 0x01) 
    { /* 设置设备名字 */
        if (len < 7) 
        {
            LOG_ERROR("  ERROR: Data length insufficient\n");
            return;
        }
        
        unsigned char name_len = data[4];
        if (name_len > MAX_DEVICE_NAME_LEN) name_len = MAX_DEVICE_NAME_LEN;

        /* 打印接收到的数据 */
        for(i = 0; i < name_len; i++)
        {
            LOG_INFO("  Data[%d]: %02X\n", i, data[5 + i]);
        }
        
        /* 检查设备是否被锁定 */
        if (is_device_locked())        
        {
            LOG_ERROR("  ERROR: Device is locked, cannot update device name\n");
            /* 发送失败响应 */
            send_report_response(client_fd, main_id, sub_id, FAILED);
            return;
        }
        
        semTake(g_config_mutex, WAIT_FOREVER);
        memset(g_system_config.device.device_name, 0, sizeof(g_system_config.device.device_name));
        memcpy(g_system_config.device.device_name, &data[5], name_len);
        
        /* 写入flash */
        
        semGive(g_config_mutex);
        
        /* 发送ACK */    
        send_report_response(client_fd, main_id,  sub_id, SUCCESSED);
        
        LOG_INFO("  Device name updated successfully\n");
    }
}



