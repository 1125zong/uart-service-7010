/*
 * =====================================================================================
 *
 * Filename:  app_dev.c
 *
 * Description:  实现设备配置管理功能，包括加载、保存和恢复出厂设置。
 *
 * =====================================================================================
 */
#include "app_com.h" // For g_system_config definition
#include "app_net_cfg.h" 
#include "app_dev_cfg.h" 
#include "hal_flash.h"
#include "app_dev.h"
#include "hal_axi16550.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stddef.h>
#include <sys/reboot.h>
#include <unistd.h>



#define 	LAN1	0
#define 	LAN2	1
// 全局配置变量的实体定义
SystemConfiguration g_system_config;

extern int init_pl_memory_access(void);
int set_device_ip_address1(uint32_t net_ip) ;
extern void device_locked_led(int action);
static int set_device_mac_address(int lan_index);


/**
 * @brief 用于Flash存储的精简通道配置结构体
 * @details 排除了运行时缓冲区，只保存配置参数
 */
typedef struct
{
    /* -- Serial Settings (0x04) -- */
    char alias[MAX_ALIAS_LEN+1];
    OperationMode  op_mode;

    /* -- 串口物理参数 -- */
    int baudrate;
    unsigned char data_bits;
    unsigned char stop_bits;
    unsigned char parity;
    unsigned char flow_ctrl;
    unsigned char fifo_enable;
    unsigned char interface_type;
    
    /* -- 串口控制参数 -- */
    unsigned char space;
    unsigned char mark;
    unsigned char usart_mcr_dtr;
    unsigned char usart_mcr_rts;
    unsigned char usart_crtscts;
    unsigned char IX_on;
    unsigned char IX_off;

    struct {
        int send_interval_ms;    // 发送时间间隔
        int packet_size;         // 发送包大小
    } net_send_cfg;

    DataPackingSettings packing_settings;

    /* --- 2. TCP/UDP 连接控制参数 (适用于 TCP Server/Client, UDP) --- */
    unsigned char  tcp_alive_check_time_min; // TCP keep-alive (0-99 min)
    unsigned short inactivity_time_ms;       // Inactivity timeout (0-65535 ms)
    unsigned char  ignore_jammed_ip;         // Ignore jammed IP (0: No, 1: Yes)
    
    /* --- 3. 模式特定配置 --- */
    
    // a) TCP Server / Real COM 模式特定参数
    unsigned char  allow_driver_control;     // 允许驱动控制 (仅 Real COM/TCP Server)
    unsigned char  max_connections;          // 最大连接数 (仅 Real COM/TCP Server)
    unsigned short local_tcp_port;           // 本地监听端口 (TCP Server/Real COM)
    unsigned short command_port;           // 本地监听端口 (TCP Server/Real COM)
    unsigned short data_port;
    unsigned short connection_control;     // 连接控制 (仅 TCP Client Mode)

    // b) TCP Client / UDP 模式特定参数
    UDP_Mode_Settings udp_destinations[4];  // udp 模式目标端点列表
    TCP_Client_Mode_Settings tcp_destinations[4];  // tcp 模式标端点列表
    unsigned short local_udp_listen_port;    // UDP模式下的本地监听端口

    /* -- Operating Settings -- */
    unsigned int op_mode_ip1;
    unsigned int op_mode_ip2;
    unsigned int op_mode_ip3;
    unsigned int op_mode_ip4;

    /* 锁定状态 */
    unsigned char is_locked;  
	
	 /* 锁定的客户端的ip地址 */
    unsigned int locked_client_ip[MAX_LOCKED_PORT_IPS];
    /* 锁定的客户端的mac地址 */
    unsigned char locked_client_mac[6];
    /* 工作状态 */
    unsigned char is_connected;

    /* 端口镜像使能 */
    unsigned char mirror_enable;

    UartPhysicalState uart_state;
    
} ChannelConfig;

/**
 * @brief 用于Flash存储的精简系统配置结构体
 * @details 排除了运行时缓冲区，只保存配置参数
 */
typedef struct 
{
	uint32_t magic_num;                  
    DeviceSettings device;
    ChannelConfig channels[NUM_PORTS];

	unsigned char mirror_rx_port;
	unsigned char mirror_tx_port;
} SystemConfigForFlash;


extern int save_config(void);

static void init_device_interface_status(void);


/*********************重启标志位相关****************************/
int set_restore_flag(void)
{
    LOG_INFO("  Action: Set Restore Flag\n");
    int ret;
    char flag = 1;
    ret = hal_flash_write(FLASH_START_ADDR_RESTORE_FLAG, &flag, sizeof(flag));
    return ret;
}

int clear_restore_flag(void)
{
	char flag = 0;
    int ret = hal_flash_write(FLASH_START_ADDR_RESTORE_FLAG, &flag, sizeof(flag));
    return ret;
}

int8_t read_restore_flag(void)
{
	char flag;
    int ret = hal_flash_read(FLASH_START_ADDR_RESTORE_FLAG, &flag, sizeof(flag));
    if(ret < 0)
    {
    	LOG_ERROR(" read_restore_flag err..\n");
    	return ERROR;
    }
    return flag ;
}

/*********************重启标志位相关****************************/

static void copy_channel_config_to_flash(const ChannelState* src, ChannelConfig* dst)
{
	// 复制通道配置参数，不包括运行时缓冲区和统计信息
	// 从 alias 到 interface_type
	memcpy(&dst->alias, &src->alias, sizeof(src->alias));
	dst->op_mode = src->op_mode;
	dst->baudrate = src->baudrate;
	dst->data_bits = src->data_bits;
	dst->stop_bits = src->stop_bits;
	dst->parity = src->parity;
	dst->flow_ctrl = src->flow_ctrl;
	dst->fifo_enable = src->fifo_enable;
	dst->interface_type = src->interface_type;
	dst->space = src->space;
	dst->mark = src->mark;
	dst->usart_mcr_dtr = src->usart_mcr_dtr;
	dst->usart_mcr_rts = src->usart_mcr_rts;
	dst->usart_crtscts = src->usart_crtscts;
	dst->IX_on = src->IX_on;
	dst->IX_off = src->IX_off;
	dst->net_send_cfg.send_interval_ms = src->net_send_cfg.send_interval_ms;
	dst->net_send_cfg.packet_size = src->net_send_cfg.packet_size;
	memcpy(&dst->packing_settings, &src->packing_settings, sizeof(DataPackingSettings));
	dst->tcp_alive_check_time_min = src->tcp_alive_check_time_min;
	dst->inactivity_time_ms = src->inactivity_time_ms;
	dst->ignore_jammed_ip = src->ignore_jammed_ip;
	dst->allow_driver_control = src->allow_driver_control;
	dst->max_connections = src->max_connections;
	dst->local_tcp_port = src->local_tcp_port;
	dst->command_port = src->command_port;
	dst->data_port = src->data_port;
	dst->connection_control = src->connection_control;
	memcpy(&dst->udp_destinations, &src->udp_destinations, sizeof(UDP_Mode_Settings) * 4);
	memcpy(&dst->tcp_destinations, &src->tcp_destinations, sizeof(TCP_Client_Mode_Settings) * 4);
	dst->local_udp_listen_port = src->local_udp_listen_port;
	dst->op_mode_ip1 = src->op_mode_ip1;
	dst->op_mode_ip2 = src->op_mode_ip2;
	dst->op_mode_ip3 = src->op_mode_ip3;
	dst->op_mode_ip4 = src->op_mode_ip4;
	
	// 复制运行时状态
	dst->uart_state = src->uart_state;
	dst->is_locked = src->is_locked;
	memcpy(dst->locked_client_ip, src->locked_client_ip, sizeof(src->locked_client_ip));
	memcpy(dst->locked_client_mac, src->locked_client_mac, sizeof(src->locked_client_mac));
	dst->is_connected = src->is_connected;
	dst->mirror_enable = src->mirror_enable;

//	打印dst
#if 0
	LOG_INFO("dst->alias: %s\n", dst->alias);
	LOG_INFO("dst->op_mode: %d\n", dst->op_mode);
	LOG_INFO("dst->baudrate: %d\n", dst->baudrate);
	LOG_INFO("dst->data_bits: %d\n", dst->data_bits);
	LOG_INFO("dst->stop_bits: %d\n", dst->stop_bits);
	LOG_INFO("dst->parity: %d\n", dst->parity);
	LOG_INFO("dst->flow_ctrl: %d\n", dst->flow_ctrl);
	LOG_INFO("dst->fifo_enable: %d\n", dst->fifo_enable);
	LOG_INFO("dst->interface_type: %d\n", dst->interface_type);
	LOG_INFO("dst->space: %d\n", dst->space);
	LOG_INFO("dst->mark: %d\n", dst->mark);
	LOG_INFO("dst->usart_mcr_dtr: %d\n", dst->usart_mcr_dtr);
	LOG_INFO("dst->usart_mcr_rts: %d\n", dst->usart_mcr_rts);
	LOG_INFO("dst->usart_crtscts: %d\n", dst->usart_crtscts);
	LOG_INFO("dst->IX_on: %d\n", dst->IX_on);
	LOG_INFO("dst->IX_off: %d\n", dst->IX_off);
	LOG_INFO("dst->tcp_alive_check_time_min: %d\n", dst->tcp_alive_check_time_min);
	LOG_INFO("dst->inactivity_time_ms: %d\n", dst->inactivity_time_ms);
	LOG_INFO("dst->ignore_jammed_ip: %d\n", dst->ignore_jammed_ip);
	LOG_INFO("dst->allow_driver_control: %d\n", dst->allow_driver_control);
	LOG_INFO("dst->max_connections: %d\n", dst->max_connections);
	LOG_INFO("dst->local_tcp_port: %d\n", dst->local_tcp_port);
	LOG_INFO("dst->command_port: %d\n", dst->command_port);
	LOG_INFO("dst->data_port: %d\n", dst->data_port);
	LOG_INFO("dst->connection_control: %d\n", dst->connection_control);
	LOG_INFO("dst->local_udp_listen_port: %d\n", dst->local_udp_listen_port);
	LOG_INFO("dst->op_mode_ip1: %d\n", dst->op_mode_ip1);
	LOG_INFO("dst->op_mode_ip2: %d\n", dst->op_mode_ip2);
	LOG_INFO("dst->op_mode_ip3: %d\n", dst->op_mode_ip3);
	LOG_INFO("dst->op_mode_ip4: %d\n", dst->op_mode_ip4);
	LOG_INFO("dst->is_locked: %d\n", dst->is_locked);
	LOG_INFO("dst->is_connected: %d\n", dst->is_connected);
	LOG_INFO("dst->locked_client_ip: %d\n", dst->locked_client_ip[0]);
	LOG_INFO("dst->locked_client_ip: %d\n", dst->locked_client_ip[1]);
	LOG_INFO("dst->locked_client_ip: %d\n", dst->locked_client_ip[2]);
	LOG_INFO("dst->locked_client_ip: %d\n", dst->locked_client_ip[3]);
	
	LOG_INFO("dst->locked_client_mac: %x\n", dst->locked_client_mac[0]);
	LOG_INFO("dst->locked_client_mac: %x\n", dst->locked_client_mac[1]);
	LOG_INFO("dst->locked_client_mac: %x\n", dst->locked_client_mac[2]);
	LOG_INFO("dst->locked_client_mac: %x\n", dst->locked_client_mac[3]);
	LOG_INFO("dst->locked_client_mac: %x\n", dst->locked_client_mac[4]);
	LOG_INFO("dst->locked_client_mac: %x\n", dst->locked_client_mac[5]);
#endif
}

/**
 * @brief 将通道配置从Flash存储结构复制到完整结构
 * @param src 源Flash存储通道配置结构
 * @param dst 目标完整通道状态结构
 */
static void copy_channel_config_from_flash(const ChannelConfig* src, ChannelState* dst)
{
	// 复制通道配置参数，不包括运行时缓冲区和统计信息
	// 从 alias 到 interface_type
	memcpy(&dst->alias, &src->alias, sizeof(dst->alias));
	dst->op_mode = src->op_mode;
	dst->baudrate = src->baudrate;
	dst->data_bits = src->data_bits;
	dst->stop_bits = src->stop_bits;
	dst->parity = src->parity;
	dst->flow_ctrl = src->flow_ctrl;
	dst->fifo_enable = src->fifo_enable;
	dst->interface_type = src->interface_type;
	dst->space = src->space;
	dst->mark = src->mark;
	dst->usart_mcr_dtr = src->usart_mcr_dtr;
	dst->usart_mcr_rts = src->usart_mcr_rts;
	dst->usart_crtscts = src->usart_crtscts;
	dst->IX_on = src->IX_on;
	dst->IX_off = src->IX_off;
	dst->net_send_cfg.send_interval_ms = src->net_send_cfg.send_interval_ms;
	dst->net_send_cfg.packet_size = src->net_send_cfg.packet_size;
	memcpy(&dst->packing_settings, &src->packing_settings, sizeof(DataPackingSettings));
	dst->tcp_alive_check_time_min = src->tcp_alive_check_time_min;
	dst->inactivity_time_ms = src->inactivity_time_ms;
	dst->ignore_jammed_ip = src->ignore_jammed_ip;
	dst->allow_driver_control = src->allow_driver_control;
	dst->max_connections = src->max_connections;
	dst->local_tcp_port = src->local_tcp_port;
	dst->command_port = src->command_port;
	dst->data_port = src->data_port;
	dst->connection_control = src->connection_control;
	memcpy(&dst->udp_destinations, &src->udp_destinations, sizeof(UDP_Mode_Settings) * 4);
	memcpy(&dst->tcp_destinations, &src->tcp_destinations, sizeof(TCP_Client_Mode_Settings) * 4);
	dst->local_udp_listen_port = src->local_udp_listen_port;
	dst->op_mode_ip1 = src->op_mode_ip1;
	dst->op_mode_ip2 = src->op_mode_ip2;
	dst->op_mode_ip3 = src->op_mode_ip3;
	dst->op_mode_ip4 = src->op_mode_ip4;
	
	// 保留运行时状态，如缓冲区和网络连接信息不从Flash恢复
	// 只恢复配置相关的状态
	dst->uart_state = src->uart_state;
	dst->is_locked = src->is_locked;
	dst->is_connected = src->is_connected;
	dst->mirror_enable = src->mirror_enable;
	memcpy(dst->locked_client_ip, src->locked_client_ip, sizeof(src->locked_client_ip));
	memcpy(dst->locked_client_mac, src->locked_client_mac, sizeof(src->locked_client_mac));

#if 0
	LOG_INFO("src->alias: %s\n", src->alias);
	LOG_INFO("src->op_mode: %d\n", src->op_mode);
	LOG_INFO("src->baudrate: %d\n", src->baudrate);
	LOG_INFO("src->data_bits: %d\n", src->data_bits);
	LOG_INFO("src->stop_bits: %d\n", src->stop_bits);
	LOG_INFO("src->parity: %d\n", src->parity);
	LOG_INFO("src->flow_ctrl: %d\n", src->flow_ctrl);
	LOG_INFO("src->fifo_enable: %d\n", src->fifo_enable);
	LOG_INFO("src->interface_type: %d\n", src->interface_type);
	LOG_INFO("src->space: %d\n", src->space);
	LOG_INFO("src->mark: %d\n", src->mark);
	LOG_INFO("src->usart_mcr_dtr: %d\n", src->usart_mcr_dtr);
	LOG_INFO("src->usart_mcr_rts: %d\n", src->usart_mcr_rts);
	LOG_INFO("src->usart_crtscts: %d\n", src->usart_crtscts);
	LOG_INFO("src->IX_on: %d\n", src->IX_on);
	LOG_INFO("src->IX_off: %d\n", src->IX_off);
	LOG_INFO("src->tcp_alive_check_time_min: %d\n", src->tcp_alive_check_time_min);
	LOG_INFO("src->inactivity_time_ms: %d\n", src->inactivity_time_ms);
	LOG_INFO("src->ignore_jammed_ip: %d\n", src->ignore_jammed_ip);
	LOG_INFO("src->allow_driver_control: %d\n", src->allow_driver_control);
	LOG_INFO("src->max_connections: %d\n", src->max_connections);
	LOG_INFO("src->local_tcp_port: %d\n", src->local_tcp_port);
	LOG_INFO("src->command_port: %d\n", src->command_port);
	LOG_INFO("src->data_port: %d\n", src->data_port);
	LOG_INFO("src->connection_control: %d\n", src->connection_control);
	LOG_INFO("src->local_udp_listen_port: %d\n", src->local_udp_listen_port);
	LOG_INFO("src->op_mode_ip1: %d\n", src->op_mode_ip1);
	LOG_INFO("src->op_mode_ip2: %d\n", src->op_mode_ip2);
	LOG_INFO("src->op_mode_ip3: %d\n", src->op_mode_ip3);
	LOG_INFO("src->op_mode_ip4: %d\n", src->op_mode_ip4);
	LOG_INFO("src->is_locked: %d\n", src->is_locked);
	LOG_INFO("src->is_connected: %d\n", src->is_connected);
	LOG_INFO("src->locked_client_ip: %d\n", src->locked_client_ip[0]);
	LOG_INFO("src->locked_client_ip: %d\n", src->locked_client_ip[1]);
	LOG_INFO("src->locked_client_ip: %d\n", src->locked_client_ip[2]);
	LOG_INFO("src->locked_client_ip: %d\n", src->locked_client_ip[3]);
	LOG_INFO("src->locked_client_mac: %x\n", src->locked_client_mac[0]);
	LOG_INFO("src->locked_client_mac: %x\n", src->locked_client_mac[1]);
	LOG_INFO("src->locked_client_mac: %x\n", src->locked_client_mac[2]);
	LOG_INFO("src->locked_client_mac: %x\n", src->locked_client_mac[3]);
	LOG_INFO("src->locked_client_mac: %x\n", src->locked_client_mac[4]);
	LOG_INFO("src->locked_client_mac: %x\n", src->locked_client_mac[5]);
#endif
}

/**
 * @brief 从Flash读取配置 (硬件相关)
 */
static int read_config_from_flash(SystemConfiguration* config) 
{
	int ret;
	SystemConfigForFlash flash_config;
	memset(&flash_config, 0, sizeof(flash_config));

	int i;
	//LOG_INFO("sizeof(SystemConfigForFlash): 0x%x\n", sizeof(SystemConfigForFlash));
	LOG_INFO("sizeof(flash_config.channels): 0x%x\n", sizeof(flash_config.channels));
	LOG_INFO("sizeof(flash_config.device): 0x%x\n", sizeof(flash_config.device));
	// 读取精简配置结构体
	ret = hal_flash_read(FLASH_START_ADDR, (char *)&flash_config, sizeof(SystemConfigForFlash));
	if (ret < 0)
	{
		LOG_ERROR("Failed to read from flash: %d\n", ret);
		return -1;
	}
	
	if(flash_config.magic_num != CONFIG_MAGIC_NUMBER)
	{
		LOG_ERROR("read from flash data head err: %d\n", flash_config.magic_num);
		return -2;
	}
	else
	{
		LOG_INFO("Read Head OK---%x\n", flash_config.magic_num);
	}
	LOG_INFO("Configuration successfully read from flash (%d bytes)\n", ret);
	
	// 将精简配置复制到完整配置结构体
	memcpy(&config->device, &flash_config.device, sizeof(DeviceSettings));
	
	// 复制每个通道的配置，同时保持运行时缓冲区不变
	for (i = 0; i < NUM_PORTS; i++) 
	{
		copy_channel_config_from_flash(&flash_config.channels[i], &config->channels[i]);
	}

	config->mirror_rx_port = 16;
	config->mirror_tx_port = 16;
	if (config->mirror_rx_port < 1 || config->mirror_rx_port > NUM_PORTS)
	{
		config->mirror_rx_port = 16;
	}

	if (config->mirror_tx_port < 1 || config->mirror_tx_port > NUM_PORTS)
	{
		config->mirror_tx_port = 16;
	}
	
	//dev_config_printf(&g_system_config);
	return OK; 
}

/**
 * @brief 将配置写入Flash (硬件相关)
 */
static int write_config_to_flash(const SystemConfiguration* config) 
{
	SystemConfigForFlash flash_config;
	int i;
	int ret;
	LOG_INFO("Writing configuration to flash...\n");
	
	// 将完整配置结构体转换为精简结构体
	flash_config.magic_num = CONFIG_MAGIC_NUMBER;
	memcpy(&flash_config.device, &config->device, sizeof(DeviceSettings));
		
	// 复制每个通道的配置参数，排除运行时缓冲区
	for (i = 0; i < NUM_PORTS; i++)	 
	{
		copy_channel_config_to_flash(&config->channels[i], &flash_config.channels[i]);
	}
	
	// 将镜像端口写入 flash 中
	flash_config.mirror_rx_port = 16;
	flash_config.mirror_tx_port = 16;

	// 写入精简配置结构体到Flash
	ret = hal_flash_write(FLASH_START_ADDR, (const char*)&flash_config, sizeof(SystemConfigForFlash));
	if (ret != FLASH_SUCCESS)
	{
    	LOG_ERROR("Failed to write configuration to flash: %d\n", ret);
    	return ERROR;
	}
		
	LOG_INFO("Configuration successfully written to flash\n");
	return OK;
}

int dev_config_save(void) 
{
	int status;
	
	LOG_INFO("Attempting to save configuration to flash...\n");
		
	status = write_config_to_flash(&g_system_config);
	if (status == OK) 
	{
		LOG_INFO("Configuration saved successfully.\n");
	} 
	else 
	{
		LOG_ERROR("ERROR: Failed to write configuration to flash.\n");
	}
	return status;
}

//组装设备序列号 0x2025 12 0012 000001
static DevSerialNo serial_no_pack(uint16_t year, uint8_t month, uint8_t day, uint32_t number)
{
	DevSerialNo sn;
	if (number > 0xFFFFFFFF) {
		number = 0xFFFFFFFF;
	}
	sn.year = year;
	sn.month = month;
	sn.day = day;
	sn.number = number;
	return sn;
}

/**
 * @brief 基于设备序列号生成唯一的MAC地址
 * @param serial_no 设备序列号
 * @param lan_index LAN索引 (0=LAN1, 1=LAN2)，用于生成两路不同的MAC
 * @param mac_out 输出缓冲区，至少6字节
 * @note MAC地址格式: XX:XX:XX:YY:YY:ZZ
 *       前3字节(XX:XX:XX)是厂商ID (OUI) - 使用自定义私有ID 00:0E:C6
 *       后3字节基于序列号生成，确保每台设备每个LAN都唯一
 */
static void generate_mac_from_serial(const DevSerialNo* serial_no, int lan_index, unsigned char mac_out[6])
{
    // 厂商ID (OUI) - 自定义私有ID，可向IEEE申请正式ID
    mac_out[0] = 0x00;  // 厂商标识字节1
    mac_out[1] = 0x0E;  // 厂商标识字节2
    mac_out[2] = 0xC6;  // 厂商标识字节3

    // 基于序列号生成后3字节
    // 序列号: year(2字节) + month(1字节) + day(1字节) + number(4字节)
    uint32_t sn_value = serial_no->number;  // 使用number字段

    // 将序列号的不同部分组合，确保唯一性
    // 高位使用 year ^ month ^ day 的混合
    mac_out[3] = (serial_no->year >> 8) ^ serial_no->year ^ serial_no->month;

    // 中位使用 number 的高字节
    mac_out[4] = (sn_value >> 16) ^ (sn_value >> 8);

    // 低位使用 number 的低字节 + LAN索引，确保两路MAC不同
    // LAN1 使用原值，LAN2 使用 (~原值 & 0xFF) 避免冲突
    if (lan_index == 0) 
	{
        mac_out[5] = sn_value & 0xFF;
    } 
	else 
	{
        mac_out[5] = (~sn_value) & 0xFF;
    }

    // 确保MAC地址是有效的单播地址 (最低位为0)
    mac_out[0] &= 0xFE;
}

/**
 * @brief 初始化单个通道的所有默认配置参数
 * @details 此函数为通道加载所有模式的默认参数，并将初始操作模式
 * 设置为 Real COM。这确保了所有字段都有一个已知的初始状态。
 * @param ch 指向要初始化的通道状态结构体的指针
 * @param channel_index 当前通道的索引号 (0-based)
 */
static void dev_config_init_channel_defaults(ChannelState* ch, int channel_index)
{
	// --- 0. 清零结构体以确保一个干净的状态 ---
	memset(ch, 0, sizeof(ChannelState));

	// --- 设置通道索引和别名 ---

	snprintf(ch->alias, MAX_ALIAS_LEN, "Port %d", channel_index + 1);

	//默认状态设置
	ch->is_locked = PORT_LOCK_NONE;
	ch->is_connected = PORT_CONNECT_NONE;
	ch->locked_client_ip[0] = 0;

	// --- 2. 设置默认的操作模式 ---
	ch->op_mode = DEFAULT_COM_OP_MODE;
	ch->interface_type = DEFAULT_COM_INTERFACE_TYPE;

	// --- 3. 设置通用的串口和网络参数 ---
	ch->baudrate = DEFAULT_COM_BAUDRATE;
	ch->data_bits = DATA_BITS_8;
	ch->stop_bits = STOP_BITS_1;
	ch->parity = PARITY_NONE;
	ch->flow_ctrl = 0;
	ch->tcp_alive_check_time_min = DEFAULT_REAL_COM_TCP_ALIVE_CHECK_MIN; // 通用
	ch->inactivity_time_ms = DEFAULT_TCPSERVER_INACTIVITY_TIME_MS; // 通用
	ch->ignore_jammed_ip = DEFAULT_REAL_COM_IGNORE_JAMMED_IP; // 通用

	// Real COM 模式参数
	ch->max_connections = DEFAULT_REAL_COM_MAX_CONNECTIONS;
	ch->allow_driver_control = DEFAULT_REAL_COM_ALLOW_DRIVER_CONTROL;

	// TCP Server 模式参数
	ch->local_tcp_port = DEFAULT_TCPSERVER_LOCAL_TCP_PORT + channel_index; //4001 --
	ch->command_port = DEFAULT_TCPSERVER_COMMAND_PORT + channel_index;     //966
	ch->data_port = DEFAULT_TCPSERVER_DATA_PORT + channel_index;     //950


}

void dev_config_load_defaults(void)
{

	// 获取互斥锁，以线程安全的方式修改全局配置
	if (semTake(g_config_mutex, WAIT_FOREVER) == OK)
	{
		int i = 0;
		unsigned long long tx_totals[NUM_PORTS];
		unsigned long long rx_totals[NUM_PORTS];
		// --- 进入临界区 ---

		/* 恢复默认配置时保留本次启动以来的串口累计计数。 */
		semTake(g_stats_mutex, WAIT_FOREVER);
		for (i = 0; i < NUM_PORTS; i++)
		{
			tx_totals[i] = g_system_config.channels[i].tx_total_count;
			rx_totals[i] = g_system_config.channels[i].rx_total_count;
		}

		// 清空整个结构体
		memset(&g_system_config, 0, sizeof(SystemConfiguration));

		/* --- 加载全局设备默认设置 --- */
		DeviceSettings* dev = &g_system_config.device;

		/* --- 加载每个通道的默认设置 --- */
		for (i = 0; i < NUM_PORTS; i++)
		{
			dev_config_init_channel_defaults(&g_system_config.channels[i], i);
			g_system_config.channels[i].tx_total_count = tx_totals[i];
			g_system_config.channels[i].rx_total_count = rx_totals[i];
		}
		semGive(g_stats_mutex);

		strncpy(dev->model_name, DEVICE_MODEL_NAME, MAX_MODEL_NAME_LEN);
		dev->serial_no = serial_no_pack(DEVICE_SERIAL_NO_YEAR, DEVICE_SERIAL_NO_MONTH, DEVICE_SERIAL_NO_DAY, DEVICE_NUMBER);
		strncpy(dev->device_name, DEVICE_NAME, MAX_DEVICE_NAME_LEN);

		dev->firmware_version[0] = FW_VERSION_MAJOR;
		dev->firmware_version[1] = FW_VERSION_MINOR;
		dev->firmware_version[2] = FW_VERSION_PATCH;
		dev->firmware_version[3] = FW_VERSION_BUILD;

		dev->status = DEV_STATE_FACTORY_MODE;
		dev->lan_count = MAX_LANS;

		// 生成基于序列号的唯一MAC地址
		unsigned char mac1[6], mac2[6];
		generate_mac_from_serial(&dev->serial_no, 0, mac1);  // LAN1
		generate_mac_from_serial(&dev->serial_no, 1, mac2);  // LAN2

		dev->ip_address1 = inet_addr("192.168.5.220");
		memcpy(dev->mac_address1, mac1, 6);
		dev->lan_speed1 = SPEED_1000M;
		dev->lan_state1 = NO_LINK;
		dev->ip_config_mode[0] = IP_MODE_STATIC; // static


		dev->ip_address2 = inet_addr("192.168.9.220");
		memcpy(dev->mac_address2, mac2, 6);
		dev->lan_speed2 = SPEED_1000M;
		dev->lan_state2 = NO_LINK;
		dev->ip_config_mode[1] = IP_MODE_STATIC; // static
		dev->lcm_present = LCM_PRESENT;
		
	    /* 将两路ip地址赋给ip_address数组 */
	    g_system_config.device.ip_address[0] = g_system_config.device.ip_address1;
	    g_system_config.device.ip_address[1] = g_system_config.device.ip_address2;

	    /* 初始化两路网络netmask */    
	    g_system_config.device.netmask[0] = inet_addr("255.255.255.0");
	    g_system_config.device.netmask[1] = inet_addr("255.255.255.0");

	    /* 初始化两路网络默认网关 */ 
	    g_system_config.device.gateway[0] = inet_addr("192.168.5.1");
	    g_system_config.device.gateway[1] = inet_addr("192.168.9.1");

	    /* 初始化两路网络默认dns */
	    g_system_config.device.dns_server1[0] = inet_addr("8.8.8.8");
	    g_system_config.device.dns_server2[0] = inet_addr("8.8.8.8");		
		
		
		// 默认电源与接口/锁定状态
		dev->power1_status = POWER_STATE_ON;
		dev->power2_status = POWER_STATE_OFF;
		dev->interface_type_count = INTERFACE_TYPE_RS422 << 8 | INTERFACE_NUMBER_16; // RS232/RS422/RS485
		
		
		// 初始化接口状态和接口锁定状态数组
		init_device_interface_status();
		
		dev->lock_status = LOCK_STATE_UNLOCK;
		
		// 初始化锁定密码为ASCII  “NULL”
		strcpy(dev->lock_password, "NULL");

		dev->locked_client_ip = 0; // 初始化锁定客户端IP为0，表示没有锁定设备

		// 默认镜像端口号(TX和RX通道)为 16
		g_system_config.mirror_rx_port = 16;
		g_system_config.mirror_tx_port = 16;
	
		semGive(g_config_mutex);
	} 
	
	else 
	{
		LOG_ERROR("FATAL: Could not take config mutex to load defaults.\n");
	}
}

int dev_config_init(void) 
{
	int ret;
	int8_t flag;

	//初始化pl空间地址
	init_pl_memory_access();
	
	//读flash是否恢复出厂设置标志位
	flag = read_restore_flag();
	if (flag < 0) 
	{
		LOG_ERROR("Failed to read restore flag from flash.\n");
	}
	
	LOG_INFO("Restore flag: %d\n", flag);
	
	if (flag == 1) 	
	{
		LOG_INFO("Restore flag found in flash. Loading factory defaults.\n");
		dev_config_load_defaults();

		flag = 0;
		// 将恢复出厂设置标志位清0写回Flash，为下一次恢复出厂设置做准备
		clear_restore_flag(); 
		
		if (dev_config_save() != OK)
		{
			LOG_ERROR("ERROR: Failed to save initial default configuration to flash.\n");
			return ERROR; // 返回错误，表示启动时未找到有效配置
		}
				
		save_config();
		// 设置 MAC 地址 (LAN1)
		set_device_mac_address(LAN1);
		//设置默认网络IP地址
		printf("ip1: %x\n", g_system_config.device.ip_address1);
		set_device_ip_address1(g_system_config.device.ip_address1);
		
		
		return 0;
		
	}
	
	ret = read_config_from_flash(&g_system_config);
	
	if (ret == OK)
	{
		// 读取成功,如何设备锁定,点亮锁定led
		if (g_system_config.device.lock_status == LOCK_STATE_LOCKED)
		{
			device_locked_led(1);
		}

		/* 读取端口锁定状态，点亮相应端口的LED */
		for (int i = 0; i < NUM_PORTS; i++)
		{
			if (g_system_config.device.interface_lock_status[i] == INTERFACE_LOCK_STATE_LOCKED)
			{
				Portled(i, 1);
			}
		}

		LOG_INFO("Configuration successfully loaded from flash.\n");

		// 设置 MAC 地址 (LAN1)
		set_device_mac_address(LAN1);
		//设置默认网络IP地址
		printf("ip1: %x\n", g_system_config.device.ip_address1);
		int ret_ip = set_device_ip_address1(g_system_config.device.ip_address1);
		if(ret_ip == 0)
		{
			g_system_config.device.lan_speed1 = SPEED_1000M; // Set LAN1 speed to 1000Mbps
			g_system_config.device.lan_state1 = LINK; // Set LAN1 state to LINK
		}
		printf("g_system_config.device.lan_speed1: %d\n", g_system_config.device.lan_speed1);
	
		return OK;
	}
	
	// else if(ret == (-1))
	// {
	// 	LOG_ERROR("Configuration failed loaded from flash.\n");
		
	// }
	
	// else if(ret == 2)
	// {
	// 	LOG_ERROR("Configuration successfully loaded from flash but head err.\n");
		
	// }

	else 
	{
		LOG_INFO("WARN: Failed to load configuration from flash. Loading factory defaults.\n");
		dev_config_load_defaults();

		// 设置 MAC 地址 (LAN1)
		set_device_mac_address(LAN1);
		//设置默认网络IP地址
		LOG_INFO("ip1: %x\n", g_system_config.device.ip_address1);
		int ret_ip = set_device_ip_address1(g_system_config.device.ip_address1);
		if(ret_ip == 0)
		{
			g_system_config.device.lan_speed1 = SPEED_1000M; // Set LAN1 speed to 1000Mbps
			g_system_config.device.lan_state1 = LINK; // Set LAN1 state to LINK
			
		}

		// 尝试将出厂设置保存一次
		if (dev_config_save() != OK)
		{
			LOG_ERROR("ERROR: Failed to save initial default configuration to flash.\n");
			return ERROR; // 返回错误，表示启动时未找到有效配置
		}
				
		save_config();
	}
	
	return 0;
}


static void sysReboot(void)
{
	// 同步数据到磁盘，确保数据不丢失
    sync();
    // 等待一段时间让sync完成
    sleep(1);
    // 执行系统重启
    printf("reboot...\n");
    reboot(RB_AUTOBOOT);
}

void dev_reboot(void)
{
	LOG_INFO("System rebooting...\n");
	sysReboot();
}


void serial_no_to_bytes(const DevSerialNo* sn, unsigned char out[8])
{
    unsigned int number = sn->number & 0xFFFFFFFF;
    out[0] = (unsigned char)((sn->year >> 8) & 0xFF);
    out[1] = (unsigned char)(sn->year & 0xFF);
    out[2] = (unsigned char)(sn->month & 0xFF);
    out[3] = (unsigned char)((sn->day) & 0xFF);
    out[4] = (unsigned char)((number >> 24) & 0xFF);
    out[5] = (unsigned char)((number >> 16) & 0xFF);
    out[6] = (unsigned char)((number >> 8) & 0xFF);
    out[7] = (unsigned char)(number & 0xFF);
}

void dev_config_dump_all(void)
{
    char ip_str[INET_ADDRSTRLEN];
    unsigned char sn_bytes[8];
    
    if (semTake(g_config_mutex, WAIT_FOREVER) != OK) {
        LOG_ERROR("FATAL: Could not take config mutex to dump configuration.");
        return;
    }

    LOG_INFO("\n======================basic info start======================================");

    // 序列号（结构与8字节大端表示）
    serial_no_to_bytes(&g_system_config.device.serial_no, sn_bytes);

    LOG_INFO("Model Name: %s", g_system_config.device.model_name);
    LOG_INFO("Serial No (struct): year=%u, month=%u, day=%u, number=%u",
             (unsigned)g_system_config.device.serial_no.year,
             (unsigned)g_system_config.device.serial_no.month,
             (unsigned)g_system_config.device.serial_no.day,
             (unsigned)g_system_config.device.serial_no.number);
    LOG_INFO("Serial No (bytes): %02X %02X %02X %02X %02X %02X %02X %02X",
             sn_bytes[0], sn_bytes[1], sn_bytes[2], sn_bytes[3],
             sn_bytes[4], sn_bytes[5], sn_bytes[6], sn_bytes[7]);

    LOG_INFO("Device Name: %s", g_system_config.device.device_name);
    LOG_INFO("Firmware Version: %u.%u.%u.%u",
             (unsigned)g_system_config.device.firmware_version[0],
             (unsigned)g_system_config.device.firmware_version[1],
             (unsigned)g_system_config.device.firmware_version[2],
             (unsigned)g_system_config.device.firmware_version[3]);
    LOG_INFO("Status: 0x%02X", (unsigned)g_system_config.device.status);
    LOG_INFO("LAN Count: %u", (unsigned)g_system_config.device.lan_count);

    inet_ntop(AF_INET, &g_system_config.device.ip_address1, ip_str, sizeof(ip_str));
    LOG_INFO("LAN1 IP: %s", ip_str);
    LOG_INFO("LAN1 IP: %x", g_system_config.device.ip_address1);
    LOG_INFO("LAN1 netmask: %x", g_system_config.device.netmask[0]);
    LOG_INFO("LAN1 MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             g_system_config.device.mac_address1[0], g_system_config.device.mac_address1[1],
             g_system_config.device.mac_address1[2], g_system_config.device.mac_address1[3],
             g_system_config.device.mac_address1[4], g_system_config.device.mac_address1[5]);
    LOG_INFO("LAN1 Speed: 0x%02X", (unsigned)g_system_config.device.lan_speed1);
    LOG_INFO("LAN1 ip_config_mode: 0x%02X", (unsigned)g_system_config.device.ip_config_mode[0]);

    inet_ntop(AF_INET, &g_system_config.device.ip_address2, ip_str, sizeof(ip_str));
    LOG_INFO("LAN2 IP: %s", ip_str);
    LOG_INFO("LAN2 MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             g_system_config.device.mac_address2[0], g_system_config.device.mac_address2[1],
             g_system_config.device.mac_address2[2], g_system_config.device.mac_address2[3],
             g_system_config.device.mac_address2[4], g_system_config.device.mac_address2[5]);
    LOG_INFO("LAN2 Speed: 0x%02X", (unsigned)g_system_config.device.lan_speed2);
    LOG_INFO("LAN2 ip_config_mode: 0x%02X", (unsigned)g_system_config.device.ip_config_mode[1]);


    LOG_INFO("LCM Present: %u", (unsigned)g_system_config.device.lcm_present);
    LOG_INFO("Power1: %u, Power2: %u",
             (unsigned)g_system_config.device.power1_status,
             (unsigned)g_system_config.device.power2_status);
    LOG_INFO("Interface Type Count: %08x", g_system_config.device.interface_type_count);
    LOG_INFO("Interface Status: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             g_system_config.device.interface_status[0], 
             g_system_config.device.interface_status[1], 
             g_system_config.device.interface_status[2], 
             g_system_config.device.interface_status[3], 
             g_system_config.device.interface_status[4], 
             g_system_config.device.interface_status[5], 
             g_system_config.device.interface_status[6], 
             g_system_config.device.interface_status[7],
             g_system_config.device.interface_status[8], 
             g_system_config.device.interface_status[9], 
             g_system_config.device.interface_status[10], 
             g_system_config.device.interface_status[11], 
             g_system_config.device.interface_status[12], 
             g_system_config.device.interface_status[13], 
             g_system_config.device.interface_status[14], 
             g_system_config.device.interface_status[15]);
    LOG_INFO("Lock Status: %u", (unsigned)g_system_config.device.lock_status);
    LOG_INFO("Locked Client IP: %x", g_system_config.device.locked_client_ip);
	
    LOG_INFO("Interface Lock Status: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             g_system_config.device.interface_lock_status[0], 
             g_system_config.device.interface_lock_status[1], 
             g_system_config.device.interface_lock_status[2], 
             g_system_config.device.interface_lock_status[3], 
             g_system_config.device.interface_lock_status[4], 
             g_system_config.device.interface_lock_status[5], 
             g_system_config.device.interface_lock_status[6], 
             g_system_config.device.interface_lock_status[7],
             g_system_config.device.interface_lock_status[8], 
             g_system_config.device.interface_lock_status[9], 
             g_system_config.device.interface_lock_status[10], 
             g_system_config.device.interface_lock_status[11], 
             g_system_config.device.interface_lock_status[12], 
             g_system_config.device.interface_lock_status[13], 
             g_system_config.device.interface_lock_status[14], 
             g_system_config.device.interface_lock_status[15]);

    LOG_INFO("=========================basic info end===================================\n");

#if 0
    /* 打印每个通道的参数设置 */
    uint8_t i = 0;
    for (i = 0; i < NUM_PORTS; i++) 
    {
        ChannelState* ch = &g_system_config.channels[i];
        
        LOG_INFO("==================== Channel %d Settings ====================", i + 1);
        LOG_INFO("Alias: %s", ch->alias);
        
        // 打印操作模式
        switch (ch->op_mode) {
            case OP_MODE_REAL_COM:
                LOG_INFO("Operation Mode: Real COM");
                break;
            case OP_MODE_TCP_SERVER:
                LOG_INFO("Operation Mode: TCP Server");
                break;
            case OP_MODE_TCP_CLIENT:
                LOG_INFO("Operation Mode: TCP Client");
                break;
            case OP_MODE_UDP:
                LOG_INFO("Operation Mode: UDP");
                break;
            case OP_MODE_DISABLED:
                LOG_INFO("Operation Mode: Disabled");
                break;
            default:
                LOG_INFO("Operation Mode: Unknown (0x%02X)", ch->op_mode);
                break;
        }
        
        // 串口物理参数
        LOG_INFO("Baudrate: %d", ch->baudrate);
        LOG_INFO("Data Bits: %d", ch->data_bits);
        LOG_INFO("Stop Bits: %d", ch->stop_bits);
        LOG_INFO("Parity: %d", ch->parity);
        LOG_INFO("Flow Control: %d", ch->flow_ctrl);
        LOG_INFO("FIFO Enable: %d", ch->fifo_enable);
        
        // 打印接口类型
        switch (ch->interface_type) {
            case INTERFACE_TYPE_RS232:
                LOG_INFO("Interface Type: RS232");
                break;
            case INTERFACE_TYPE_RS422:
                LOG_INFO("Interface Type: RS422");
                break;
            case INTERFACE_TYPE_RS485:
                LOG_INFO("Interface Type: RS485");
                break;
            case INTERFACE_TYPE_CAN:
                LOG_INFO("Interface Type: CAN");
                break;
            default:
                LOG_INFO("Interface Type: Unknown (0x%02X)", ch->interface_type);
                break;
        }

#if 0
        // 串口控制参数
        LOG_INFO("Space: 0x%02X", ch->space);
        LOG_INFO("Mark: 0x%02X", ch->mark);
        LOG_INFO("USART MCR DTR: %d", ch->usart_mcr_dtr);
        LOG_INFO("USART MCR RTS: %d", ch->usart_mcr_rts);
        LOG_INFO("USART C RTS/CTS: %d", ch->usart_crtscts);
        LOG_INFO("IX ON: %d", ch->IX_on);
        LOG_INFO("IX OFF: %d", ch->IX_off);
        
        // 网络发送配置
        LOG_INFO("Net Send Interval (ms): %d", ch->net_send_cfg.send_interval_ms);
        LOG_INFO("Net Packet Size: %d", ch->net_send_cfg.packet_size);
        
        // 数据打包设置
        LOG_INFO("Packing Length: %d", ch->packing_settings.packing_length);
        LOG_INFO("Delimiter 1: 0x%02X", ch->packing_settings.delimiter1);
        LOG_INFO("Delimiter 2: 0x%02X", ch->packing_settings.delimiter2);
        
        switch (ch->packing_settings.delimiter_process) {
            case DELIMITER_PROCESS_NONE:
                LOG_INFO("Delimiter Process: None");
                break;
            case DELIMITER_PROCESS_APPEND_DELIM1:
                LOG_INFO("Delimiter Process: Append Delimiter 1");
                break;
            case DELIMITER_PROCESS_APPEND_DELIM2:
                LOG_INFO("Delimiter Process: Append Delimiter 2");
                break;
            case DELIMITER_PROCESS_STRIP:
                LOG_INFO("Delimiter Process: Strip");
                break;
            default:
                LOG_INFO("Delimiter Process: Unknown (0x%02X)", ch->packing_settings.delimiter_process);
                break;
        }
        
        LOG_INFO("Force Transmit Time (ms): %d", ch->packing_settings.force_transmit_time_ms);
        
        // TCP/UDP 连接控制参数
        LOG_INFO("TCP Alive Check Time (min): %d", ch->tcp_alive_check_time_min);
        LOG_INFO("Inactivity Time (ms): %d", ch->inactivity_time_ms);
        LOG_INFO("Ignore Jammed IP: %d", ch->ignore_jammed_ip);
        
        // 模式特定参数
        LOG_INFO("Allow Driver Control: %d", ch->allow_driver_control);
        LOG_INFO("Max Connections: %d", ch->max_connections);
        LOG_INFO("Local TCP Port: %d", ch->local_tcp_port);
        LOG_INFO("Command Port: %d", ch->command_port);
        LOG_INFO("Data Port: %d", ch->data_port);
        LOG_INFO("Connection Control: %d", ch->connection_control);
        
        // UDP 模式参数
        for (j = 0; j < 4; j++) {
            LOG_INFO("UDP Destination %d - Begin IP: %u, End IP: %u, Port: %d", 
                     j + 1, ch->udp_destinations[j].begin_ip, ch->udp_destinations[j].end_ip, ch->udp_destinations[j].port);
        }
        LOG_INFO("Local UDP Listen Port: %d", ch->local_udp_listen_port);
        
        // TCP Client 模式参数
        for (j = 0; j < 4; j++) {
            LOG_INFO("TCP Client Destination %d - IP: %u, Port: %d, Local Port: %d", 
                     j + 1, ch->tcp_destinations[j].destination_ip, 
                     ch->tcp_destinations[j].destination_port, 
                     ch->tcp_destinations[j].designated_local_port);
        }
        
        // 操作设置
        LOG_INFO("Op Mode IP1: %u", ch->op_mode_ip1);
        LOG_INFO("Op Mode IP2: %u", ch->op_mode_ip2);
        LOG_INFO("Op Mode IP3: %u", ch->op_mode_ip3);
        LOG_INFO("Op Mode IP4: %u", ch->op_mode_ip4);
#endif        
        // 状态信息
        LOG_INFO("UART State: %d", ch->uart_state);
        LOG_INFO("Is Locked: %d", ch->is_locked);
        LOG_INFO("Is Connected: %d", ch->is_connected);
        
        LOG_INFO("============================================================");
    }
#endif   
    semGive(g_config_mutex);
}

#if 0
static void print_msg(char *msg)
{
	LOG_INFO("print_msg:\n");
	
	int offset = 0;
	char model_name[41];
	unsigned char serial_no[8];
	char device_name[41];
	unsigned char firmware_version[4];
	unsigned char status;
	unsigned char lan_count;
	unsigned int ip_address1;
	unsigned char mac_address1[6];
	unsigned char lan_speed1;
	unsigned int ip_address2;
	unsigned char mac_address2[6];
	unsigned char lan_speed2;
	unsigned char lcm_present;
	unsigned char power1_status;
	unsigned char power2_status;
	unsigned short interface_type_count;
	unsigned char interface_status[NUM_PORTS];
	unsigned char lock_status;
	unsigned char interface_lock_status[NUM_PORTS];
	
	/* 读取设备基础信息 */
	memcpy(model_name, &msg[offset], 40);
	model_name[40] = '\0';
	offset += 40;
	
	memcpy(serial_no, &msg[offset], 8);
	offset += 8;
	
	memcpy(device_name, &msg[offset], 40);
	device_name[40] = '\0';
	offset += 40;
	
	firmware_version[0] = msg[offset++];
	firmware_version[1] = msg[offset++];
	firmware_version[2] = msg[offset++];
	firmware_version[3] = msg[offset++];
	
	status = msg[offset++];
	lan_count = msg[offset++];
	
	memcpy(&ip_address1, &msg[offset], 4);
	ip_address1 = ntohl(ip_address1);
	offset += 4;
	
	memcpy(mac_address1, &msg[offset], 6);
	offset += 6;
	
	lan_speed1 = msg[offset++];
	
	memcpy(&ip_address2, &msg[offset], 4);
	ip_address2 = ntohl(ip_address2);
	offset += 4;
	
	memcpy(mac_address2, &msg[offset], 6);
	offset += 6;
	
	lan_speed2 = msg[offset++];
	lcm_present = msg[offset++];
	power1_status = msg[offset++];
	power2_status = msg[offset++];
	
	memcpy(&interface_type_count, &msg[offset], 2);
	interface_type_count = ntohs(interface_type_count);
	offset += 2;
	
	memcpy(&interface_status, &msg[offset], NUM_PORTS);
	offset += NUM_PORTS;
	
	lock_status = msg[offset++];
	
	memcpy(&interface_lock_status, &msg[offset], NUM_PORTS);
	offset += NUM_PORTS;
	
	/* 打印设备信息 */
	LOG_INFO("======= Device Information =======\n");
	LOG_INFO("Model Name: %s\n", model_name);
	LOG_INFO("Serial No: %02X%02X%02X%02X%02X%02X%02X%02X\n", 
		serial_no[0], serial_no[1], serial_no[2], serial_no[3],
		serial_no[4], serial_no[5], serial_no[6], serial_no[7]);
	LOG_INFO("Device Name: %s\n", device_name);
	LOG_INFO("Firmware Version: %d.%d.%d.%d\n", 
		firmware_version[0], firmware_version[1], firmware_version[2], firmware_version[3]);
	LOG_INFO("Status: 0x%02X\n", status);
	LOG_INFO("LAN Count: %d\n", lan_count);
	
	char ip_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &ip_address1, ip_str, sizeof(ip_str));
	LOG_INFO("IP Address 1: %s\n", ip_str);
	LOG_INFO("MAC Address 1: %02X:%02X:%02X:%02X:%02X:%02X\n",
		mac_address1[0], mac_address1[1], mac_address1[2],
		mac_address1[3], mac_address1[4], mac_address1[5]);
	LOG_INFO("LAN1 Speed: 0x%02X\n", lan_speed1);
	
	inet_ntop(AF_INET, &ip_address2, ip_str, sizeof(ip_str));
	LOG_INFO("IP Address 2: %s\n", ip_str);
	LOG_INFO("MAC Address 2: %02X:%02X:%02X:%02X:%02X:%02X\n",
		mac_address2[0], mac_address2[1], mac_address2[2],
		mac_address2[3], mac_address2[4], mac_address2[5]);
	LOG_INFO("LAN2 Speed: 0x%02X\n", lan_speed2);
	
	LOG_INFO("LCM Present: %d\n", lcm_present);
	LOG_INFO("Power1 Status: %d\n", power1_status);
	LOG_INFO("Power2 Status: %d\n", power2_status);
	LOG_INFO("Interface Type Count: %d\n", interface_type_count);
	LOG_INFO("Interface Status: 0x%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
		interface_status[0], interface_status[1], interface_status[2], interface_status[3],
		interface_status[4], interface_status[5], interface_status[6], interface_status[7],
		interface_status[8], interface_status[9], interface_status[10], interface_status[11],
		interface_status[12], interface_status[13], interface_status[14], interface_status[15]);
	LOG_INFO("Lock Status: %d\n", lock_status);
	LOG_INFO("Interface Lock Status: 0x%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
		interface_lock_status[0], interface_lock_status[1], interface_lock_status[2], interface_lock_status[3],
		interface_lock_status[4], interface_lock_status[5], interface_lock_status[6], interface_lock_status[7],
		interface_lock_status[8], interface_lock_status[9], interface_lock_status[10], interface_lock_status[11],
		interface_lock_status[12], interface_lock_status[13], interface_lock_status[14], interface_lock_status[15]);
	LOG_INFO("================================\n");
}

static int pack_device_msg(void)
{
    //LOG_INFO("Action:pack_device_msg\n");
  
    int offset = 0;
    unsigned char sn[8];
       
    semTake(g_config_mutex, WAIT_FOREVER);
    {
        DeviceSettings* dev = &g_system_config.device;
        
        /* ========== 设备基础信息块 ========== */
        
        /* 型号名称 (40字节，不足补0) */
        memset(&msg[offset], 0, 40);
        strncpy((char*)&msg[offset], dev->model_name, MAX_MODEL_NAME_LEN);
        offset += 40;
        
        /* 序列号 (8字节) */
        serial_no_to_bytes(&dev->serial_no, sn);
        memcpy(&msg[offset], sn, 8);
        offset += 8;
        
        /* 设备名称 (40) */
        memset(&msg[offset], 0, 40);
        strncpy((char*)&msg[offset], dev->device_name, MAX_DEVICE_NAME_LEN);
        offset += 40;
        
        /* 固件版本 (4字节) */
        msg[offset++] = dev->firmware_version[0];
        msg[offset++] = dev->firmware_version[1];
        msg[offset++] = dev->firmware_version[2];
        msg[offset++] = dev->firmware_version[3];
        
        /* 设备状态 (1字节) */
        msg[offset++] = dev->status;
        
        /* 网口数量 (1字节) */
        msg[offset++] = dev->lan_count;
        
        /* IP地址1 (4字节，网络字节序) */
        unsigned int ip1 = htonl(dev->ip_address1);
        memcpy(&msg[offset], &ip1, 4);
        offset += 4;
        
        /* MAC地址1 (6字节) */
        memcpy(&msg[offset], dev->mac_address1, 6);
        offset += 6;
        
        /* LAN1速度 (1字节) */
        msg[offset++] = dev->lan_speed1;
        
        /* IP地址2 (4字节，网络字节序) */
        unsigned int ip2 = htonl(dev->ip_address2);
        memcpy(&msg[offset], &ip2, 4);
        offset += 4;
        
        /* MAC地址2 (6字节) */
        memcpy(&msg[offset], dev->mac_address2, 6);
        offset += 6;
        
        /* LAN2速度 (1字节) */
        msg[offset++] = dev->lan_speed2;
        
        /* LCM在位 (1字节) */
        msg[offset++] = dev->lcm_present;
        
        /* 电源1状态 (1字节) */
        msg[offset++] = dev->power1_status;
        
        /* 电源2状态 (1字节) */
        msg[offset++] = dev->power2_status;
        
        /* 接口类型数量 (2字节，网络字节序) */
        unsigned short iface_count = (dev->interface_type_count);
        memcpy(&msg[offset], &iface_count, 2);
        offset += 2;
        
        /* 接口状态 (16字节) */
        memcpy(&msg[offset], dev->interface_status, NUM_PORTS);
        offset += NUM_PORTS;
        
        /* 设备锁定状态 (1字节) */
        msg[offset++] = dev->lock_status;
        
        /* 接口锁定状态 (16字节) */
        memcpy(&msg[offset], dev->interface_lock_status, NUM_PORTS);
        offset += NUM_PORTS;        
    }
    semGive(g_config_mutex);
    return offset;
}
#endif

/* 检查设备是否被锁定 */
int is_device_locked(void)
{
    int locked;
    
    semTake(g_config_mutex, WAIT_FOREVER);
    {
        locked = (g_system_config.device.lock_status == LOCK_STATE_LOCKED);
    }
    semGive(g_config_mutex);
    
    return locked;
}

/**
 * @brief 将每个通道的接口状态和接口锁定状态赋值给DeviceSettings结构体中的数组
 * @details 从每个通道的锁定状态和连接状态更新全局设备设置中的接口状态数组
 */
static void init_device_interface_status(void)
{
    int i;
    
    for (i = 0; i < NUM_PORTS; i++) 
    {
        // 根据通道的锁定状态设置接口锁定状态
        g_system_config.device.interface_lock_status[i] = 
            (g_system_config.channels[i].is_locked == PORT_LOCK_NONE) ? 
            INTERFACE_LOCK_STATE_UNLOCK : INTERFACE_LOCK_STATE_LOCKED;
            
        // 根据通道的连接状态设置接口状态
        g_system_config.device.interface_status[i] = 
            (g_system_config.channels[i].is_connected == PORT_CONNECT_NONE) ? 
            INTERFACE_STATE_CLOSED : INTERFACE_STATE_OPENED;
    }
}

/**
 * @brief 保存网络配置到Flash
 * @details 将两路网络的IP地址和其他网络配置信息保存到Flash中
 * @return int OK on success, ERROR on failure.
 */
int save_config(void)
{
    int result;
    char buffer[256];  // 缓冲区用于存储序列化的网络配置数据
    int offset = 0;
    
    
    // 序列化网络配置数据到缓冲区
    // 第一路网络信息
    memcpy(&buffer[offset], &g_system_config.device.ip_address[0], 4);
    offset += 4;
    
    memcpy(&buffer[offset], &g_system_config.device.netmask[0], 4);
    offset += 4;
    
    memcpy(&buffer[offset], &g_system_config.device.gateway[0], 4);
    offset += 4;
    
    memcpy(&buffer[offset], g_system_config.device.mac_address1, 6);
    offset += 6;
        
    // 第二路网络信息
    memcpy(&buffer[offset], &g_system_config.device.ip_address2, 4);
    offset += 4;
    
    // 注意：在设备结构中，所有网络接口使用相同的子网掩码和网关
    // 这里我们仍然保存它们，保持数据结构的一致性
    memcpy(&buffer[offset], &g_system_config.device.netmask[1], 4);
    offset += 4;
    
    memcpy(&buffer[offset], &g_system_config.device.gateway[1], 4);
    offset += 4;
    
    memcpy(&buffer[offset], g_system_config.device.mac_address2, 6);
    offset += 6;
    
    // 使用安全的Flash写入函数将数据写入Flash
    result = hal_flash_write(FLASH_START_ADDR_NET_INFO, buffer, offset);
    
    if (result >= 0)
    {
        LOG_INFO("Network configuration saved to Flash successfully\n");
        return OK;
    } 
    else 
    {
        LOG_ERROR("Failed to save network configuration to Flash, error code: %d\n", result);
        return ERROR;
    }
}


/**
 * @brief 设置指定网口的 MAC 地址
 * @param lan_index 网口索引 (0=LAN1/eth1, 1=LAN2/eth2)
 * @return 0成功, -1失败
 */
static int set_device_mac_address(int lan_index)
{
	char cmd[128] = {0};
	char eth_name[8] = {0};
	unsigned char mac[6];
	int ret = 0;

	// 根据网口索引选择网卡名
	if (lan_index == 0) {
		strcpy(eth_name, "eth1");
	} else {
		strcpy(eth_name, "eth2");
	}

	// 基于序列号生成 MAC 地址
	generate_mac_from_serial(&g_system_config.device.serial_no, lan_index, mac);

	// 先关闭网卡 (设置MAC地址前必须先关闭网卡)
	snprintf(cmd, sizeof(cmd), "ifconfig %s down", eth_name);
	printf("exec cmd: %s\n", cmd);
	ret = system(cmd);
	if (ret == -1)
	{
		perror("ifconfig down failed!");
		return -1;
	}

	// 设置 MAC 地址
	snprintf(cmd, sizeof(cmd), "ifconfig %s hw ether %02X:%02X:%02X:%02X:%02X:%02X",
	         eth_name, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	printf("exec cmd: %s\n", cmd);
	ret = system(cmd);
	if (ret == -1)
	{
		perror("set MAC address failed!");
		// 尝试恢复网卡
		system("ifconfig eth1 up");
		return -1;
	}
	printf("MAC address set done (LAN%d): %02X:%02X:%02X:%02X:%02X:%02X\n",
	       lan_index + 1, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	// 重新开启网卡
	snprintf(cmd, sizeof(cmd), "ifconfig %s up", eth_name);
	printf("exec cmd: %s\n", cmd);
	ret = system(cmd);
	if (ret == -1)
	{
		perror("ifconfig up failed!");
		return -1;
	}

	return 0;
}

int set_device_ip_address1(uint32_t net_ip) 
{
	char ip_str[16] = {0};
	char cmd[128] = {0};
	char eth_name[] = "eth1";
    struct in_addr ip_addr;
	ip_addr.s_addr = net_ip;
    int ret = 0;

	const char *pRet;
	
	pRet = inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str));
    if (pRet == NULL) 
	{
        fprintf(stderr, "错误:IP 格式非法(%s)!\n", ip_str);
        return -2;
    } 
    printf("  raw ip str: %s\n", ip_str);
    printf("  netbytes(HEX): 0x%08X\n", g_system_config.device.ip_address1);

	snprintf(cmd, sizeof(cmd), "ifconfig %s %s", eth_name, ip_str);
	printf("exec cmd: %s\n", cmd);
	// 执行命令（注意：设置网卡IP需要root权限）
    ret = system(cmd);
    if (ret == -1) 
	{
        perror("exec cmd failed!");
        return -1;
    } 
	else 
	{
        printf("IP address set done!\n");
        // 验证：执行ifconfig eth0查看结果
        system("ifconfig eth1");
    }

    return 0;
}
