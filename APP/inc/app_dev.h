#ifndef APP_DEV_H
#define APP_DEV_H

#include "app_dev_cfg.h" 
#include "hal_ringbuffer.h"

#define     MAX_LOCKED_PORT_IPS     (4)  // 最多允许4个客户端IP地址连接，每个端口最多4个IP地址

/* ------------------ Application-Specific Constants ------------------ */
#define     NUM_PORTS               	16      // 系统支持的串口/通道数量
#define 	MAX_CLIENTS_PER_CHANNEL 	(DEFAULT_REAL_COM_MAX_CONNECTIONS)
#define     RING_BUFFER_SIZE        	(64 * 1024) // 64KB 环形缓冲区大小
#define     MAX_CONFIG_CLIENTS      	(NUM_PORTS * (MAX_CLIENTS_PER_CHANNEL+1) + 1) // 最大配置客户端数量

#define     MAX_ALIAS_LEN               19
#define     MAX_MODEL_NAME_LEN          39
#define     MAX_DEVICE_NAME_LEN         20
#define     MAX_SERVER_NAME_LEN         39
#define     MAX_PASSWORD_LEN            14
#define     SERIAL_NO_LEN               8

#define     MAX_LANS                    1

//配置相关
#define CONFIG_MAGIC_NUMBER     0x4B434647  // 配置魔术字（"GFCK"，用于校验有效性）

/*
 *  设备相关信息define
*/

//设备型号
#define DEVICE_MODEL_NAME           "Qport1255"

//设备ming
#define DEVICE_NAME                 "Qport1255"

//设备序列号
#define DEVICE_SERIAL_NO_YEAR        (0x07E9)  //2025
#define DEVICE_SERIAL_NO_MONTH       (0x05)
#define DEVICE_SERIAL_NO_DAY         (0x01)
#define DEVICE_NUMBER                (0x000003)    

//固件版本
#define FW_VERSION_MAJOR             (0x01)        
#define FW_VERSION_MINOR             (0x00)
#define FW_VERSION_PATCH             (0x00)
#define FW_VERSION_BUILD             (0x01)

/* 设备锁定标志 */
//uint8_t device_locked_flag = 0;
   
typedef struct 
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint32_t number; 
} DevSerialNo;   

/* ------------------ Global Configuration Structures ------------------ */
/**
 * @brief 描述物理串口硬件的状态机
 * @details 这个状态机追踪物理硬件的生命周期。
 */
typedef enum 
{
	UART_STATE_CLOSED,      // 初始状态。串口未被HAL层初始化或已关闭。
	UART_STATE_OPENED,      // 正常工作状态。串口已由HAL层成功打开并配置。
	UART_STATE_ERROR        // 异常状态。HAL层在尝试打开或配置串口时发生硬件错误。
							// 这是一个持久性故障，可能需要重启或干预才能恢复。
} UartPhysicalState;

/**
 * @brief 描述网络通道（数据和命令）的状态机
 * @details 这个状态机追踪每个网络服务（如数据端口950）的生命周期。
 */
typedef enum 
{
	NET_STATE_IDLE,         // 初始状态。任务尚未开始监听此端口。
	NET_STATE_LISTENING,    // 正常状态。监听Socket已成功创建并正在等待连接。此时没有活跃的客户端。
	NET_STATE_CONNECTED,    // 正常状态。至少有一个客户端已连接，正在进行数据交换。
	NET_STATE_ERROR         // 异常状态。创建监听Socket失败 (例如，端口被占用)。
							// 这是一个持久性故障，该通道的网络服务将不可用。
} NetworkChannelState;

/**
 * @brief 定义通道的工作模式
 * @details 根据用户需求定义的操作模式枚举。
 */
typedef enum 
{
    OP_MODE_REAL_COM     = 0x01, // Real COM Mode
    OP_MODE_TCP_SERVER   = 0x02, // TCP Server Mode
    OP_MODE_TCP_CLIENT   = 0x03, // TCP Client Mode
    OP_MODE_UDP          = 0x04, // UDP Mode
    OP_MODE_DISABLED     = 0xFF  // Disabled Mode
} OperationMode;

/**
 * @brief 定义数据打包时的分隔符处理方式
 */
typedef enum 
{
    DELIMITER_PROCESS_NONE          = 0x01, // 不做任何处理 (Do Nothing)
    DELIMITER_PROCESS_APPEND_DELIM1 = 0x02, // 附加分隔符1 (Delimiter+1)
    DELIMITER_PROCESS_APPEND_DELIM2 = 0x03, // 附加分隔符2 (Delimiter+2)
    DELIMITER_PROCESS_STRIP         = 0x04  // 剥离分隔符 (Strip Delimiter)
} DelimiterProcess;

/**
 * @brief 设备状态
 */

typedef enum
{    
    DEV_STATE_FACTORY_MODE  = 0x01,  // 出厂模式
    DEV_STATE_WORKING_MODE  = 0x02,  // 工作模式
    DEV_STATE_NETWORK_FAULT = 0x03, // 网络故障
    DEV_STATE_SERIAL_FAULT  = 0x04, // 串口故障
    DEV_STATE_SYSTEM_FAULT  = 0x05  // 系统故障
} DevStates;

/*
 * @brief 网口LINK状态
*/
typedef enum 
{
    LINK    = 0x01, //
    NO_LINK = 0x02  //
}NetStatus;

typedef enum 
{
    SPEED_100M    = 0x01, //
	SPEED_1000M   = 0x02  //
}NetSpeed;


/*
 * @brief LCM是否已安装
*/
typedef enum
{
    LCM_NOT_PRESENT = 0x00,
    LCM_PRESENT     = 0x01
} LCM_Presence;

/*
 * @brief 电源状态
*/
 typedef enum 
 {
    POWER_STATE_OFF = 0x00,
    POWER_STATE_ON  = 0x01
} PowerState;

/*
 * @brief 接口状态
*/
typedef enum 
{
    INTERFACE_STATE_CLOSED = 0x00,
    INTERFACE_STATE_OPENED = 0x01
} InterfacePhysicalState;

/*
 * @brief 接口类型
*/
typedef enum 
{ 
    INTERFACE_TYPE_RS232   = 0x00,
    INTERFACE_TYPE_RS422   = 0x01,
    INTERFACE_TYPE_RS485   = 0x02,
    INTERFACE_TYPE_CAN     = 0x03
} Interface_Mode;

//接口数量
#define INTERFACE_NUMBER_8  	(8)
#define INTERFACE_NUMBER_16 	(16)
#define INTERFACE_NUMBER_32 	(32)

/*
 * @brief 设备锁定状态
*/
typedef enum 
{
    LOCK_STATE_UNLOCK = 0x00,
    LOCK_STATE_LOCKED = 0x01,    
} LockState;

/*
 * @brief 接口锁定状态
*/
typedef enum 
{
    INTERFACE_LOCK_STATE_UNLOCK = 0x00,
    INTERFACE_LOCK_STATE_LOCKED = 0x01
} InterfaceLockState;

/*
 * @brief 网络ip地址静态或动态
*/
typedef enum
{
    IP_MODE_STATIC = 0x01,
    IP_MODE_DHCP
} IPMode;


/*
 * @brief 串口相关参数
*/

/**
 * @brief 串口奇偶校验设置
 */
 typedef enum 
 {
    PARITY_NONE = 0x00,
    PARITY_EVEN = 0x08,
    PARITY_ODD  = 0x10,
    PARITY_MARK = 0x18,
    PARITY_SPACE = 0x20
} ParitySetting;

/**
 * @brief 串口数据位设置
 */
typedef enum 
{
    DATA_BITS_5 = 0x05,
    DATA_BITS_6 = 0x06,
    DATA_BITS_7 = 0x07,
    DATA_BITS_8 = 0x08,
} DataBitsSetting;

/**
 * @brief 串口停止位设置
 */

typedef enum 
{
    STOP_BITS_1 = 0x01,
    STOP_BITS_2 = 0x02,
} StopBitsSetting;



/**
 * @brief TCP远程目标端点定义
 * @details 包含目标IP、目标端口和本地源端口。
 */
typedef struct 
{
    unsigned int   destination_ip;           // 目标IP地址 (4 bytes)
    unsigned short destination_port;         // 目标端口 (2 bytes)
    unsigned short designated_local_port;    // 指定的本地源端口 (0表示自动分配) (2 bytes)
} TCP_Client_Mode_Settings;

/**
 * @brief UDP远程目标端点定义
 * @details 包含目标IP、目标端口和本地源端口。
 */
typedef struct 
{
    unsigned int   begin_ip;           // 起始IP地址
    unsigned int   end_ip;             // 结束IP地址
    unsigned short port;               // 目标端口
} UDP_Mode_Settings;

/**
 * @brief 数据打包配置
 * @details 适用于所有网络转发模式 (Real COM, TCP Server/Client, UDP)
 */
typedef struct 
{
    unsigned short packing_length;           // 打包长度 (0-1024 bytes)
    unsigned short force_transmit_time_ms;   // 强制发送超时时间 (0-65535 ms)
    unsigned char  delimiter1;               // 分隔符1 (0x00-0xFF)
    unsigned char  delimiter2;               // 分隔符2 (0x00-0xFF)
    DelimiterProcess delimiter_process;      // 分隔符处理方式
} DataPackingSettings;


/**
 * @brief 描述数据通道的网络状态和连接信息
 */
typedef struct {
	NetworkChannelState state;
	int client_fds[MAX_CLIENTS_PER_CHANNEL];
	int num_clients;
} DataChannelInfo;

/**
 * @brief 描述命令通道的网络状态和连接信息
 */
typedef struct {
	NetworkChannelState state;
	int client_fds[MAX_CLIENTS_PER_CHANNEL];
	int num_clients;
} CmdChannelInfo;

/**
 * @brief 描述命令通道的网络状态和连接信息
 */
typedef struct {
	NetworkChannelState state;
	int client_fd;
	int num_clients;
} LocalChannelInfo;

/**
 * @brief 全局设备配置结构体
 */
typedef struct 
{
    /* -- Overview (0x01) -- */
    char                model_name[MAX_MODEL_NAME_LEN+1];   //设备型号
    DevSerialNo         serial_no;                          //设备序列号
    char                device_name[MAX_DEVICE_NAME_LEN];   //设备名称
    unsigned char       firmware_version[8];                //固件版本
    unsigned char       status;                             //设备状态
    unsigned char       lan_count;                         //网口有效数量:最大2
    
    unsigned char 		mac_address[6];
    
    unsigned int        ip_address1;                        //IP地址1
   
    unsigned char       mac_address1[6];                    //MAC地址1
    
    unsigned char       lan_speed1;                         //LAN1 speed
    
    unsigned char       lan_state1;                         //LAN1 state

    
    unsigned int        ip_address2;                        //IP地址2
    
    unsigned char       mac_address2[6];                    //MAC地址2
    
    unsigned char       lan_speed2;                         //LAN2 speed
    unsigned char       lan_state2;                         //LAN2 state
    
    
    unsigned char       lcm_present;                        //LCM是否已安装

    
    unsigned char       power1_status;                      //电源1状态
   
    unsigned char       power2_status;                      //电源2状态
    
    unsigned int        interface_type_count;               //接口类型数量

    
    unsigned char       interface_status[NUM_PORTS];//接口状态

    
    unsigned char       lock_status;                        //设备锁定状态

    
    unsigned char       interface_lock_status[NUM_PORTS];//接口锁定状态
       
    
    /* -- Network Settings (0x03) -- */
    unsigned int        ip_address[NET_NUM];
    unsigned int        netmask[NET_NUM];
    unsigned int        gateway[NET_NUM];
    unsigned char       ip_config_mode[NET_NUM]; // 0=Static, 1=DHCP, etc.
    unsigned int        dns_server1[NET_NUM];
    unsigned int        dns_server2[NET_NUM];
    
    /* -- Device Lock Settings -- */
    char                lock_password[MAX_PASSWORD_LEN+1];  // 设备锁定密码

    /* 锁定设备的当前pc的ip地址 */
    unsigned int        locked_client_ip;

    /* 锁定设备的当前pc的mac地址 */
    unsigned char       locked_client_mac[6];

    /* -- Watchdog Settings (0x09) -- */
    unsigned char       watchdog_enable;        // 看门狗使能: 0=禁用, 1=使能

} DeviceSettings;


/**
 * @brief 每个通道（串口）的状态和数据结构
 */
typedef struct 
{
    /* -- 运行时状态 -- */
	UartPhysicalState   uart_state;       
    DataChannelInfo     data_net_info;
    CmdChannelInfo      cmd_net_info;
    LocalChannelInfo    local_net_info;
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
	unsigned char IX_off; //XonXoff m

    struct 
	{
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

    /* -- 实时数据缓冲区 -- */
    ring_buffer_t buffer_net;
    ring_buffer_t buffer_uart;
    unsigned char net_buffer_mem[RING_BUFFER_SIZE];
    unsigned char uart_buffer_mem[RING_BUFFER_SIZE];

    /* -- 运行时监控统计 (0x06) -- */
    unsigned int tx_count;
    unsigned int rx_count;
    unsigned int tx_net;
    unsigned int rx_net;
    unsigned long long tx_total_count;
    unsigned long long rx_total_count;
    // 可删除
    
unsigned long long tcp_send_requested_bytes;
unsigned long long tcp_send_actual_bytes;
unsigned long long tcp_short_write_count;
unsigned long long tcp_unsent_bytes;
unsigned long long tcp_send_eagain_count;
unsigned long long tcp_send_error_count;
    /**/


    /* -- 串口错误统计 -- */
    unsigned int frame_error_count;    /* 帧错误计数 */
    unsigned int parity_error_count;   /* 奇偶校验错误计数 */
    unsigned int overrun_error_count;  /* 溢出错误计数 */
    unsigned int break_count;          /* Break事件计数 */

    unsigned char dsr_status;
    unsigned char cts_status;
    unsigned char dcd_status;

    /* 锁定状态 */
    unsigned char is_locked;

    /* 锁定的客户端的ip地址 */
    unsigned int locked_client_ip[MAX_LOCKED_PORT_IPS];

    /* 锁定的客户端的mac地址 */
    unsigned char locked_client_mac[6];

    /* 工作状态 */
    unsigned char is_connected;

    /* 端口镜像使能 */
    unsigned char mirror_enable;           // 镜像端口: 0=禁用, 1=使能
} ChannelState;

/**
 * @brief 包含整个系统所有配置的顶层结构体
 */
typedef struct
{
	DeviceSettings device;
	ChannelState channels[NUM_PORTS];

    /* 全局端口镜像配置，保存上位机端口号 1 ~ 16 */
    unsigned char mirror_rx_port;
    unsigned char mirror_tx_port;
} SystemConfiguration;


/* ------------------ Global Variable Declaration (extern) ------------------ */

/* ------------------ Public API Functions ------------------ */

/**
 * @brief 初始化网络
 * @details 填充g_port_mappings结构体。
 * @return no.
 */
void dev_net_init(void);

/**
 * @brief 初始化设备配置模块
 * @details 从持久化存储（如Flash）中加载配置。如果加载失败，则加载出厂默认设置。
 * @return int OK on success, ERROR on failure.
 */
int dev_config_init(void);

/**
 * @brief 将当前系统配置保存到持久化存储
 * @return int OK on success, ERROR on failure.
 */
int dev_config_save(void);

/**
 * @brief 加载出厂默认配置到 g_system_config
 * @details 这只会修改内存中的配置，不会自动保存。
 */
void dev_config_load_defaults(void);

/**
 * @brief 重启设备
 */
void dev_reboot(void);

/**
 * @brief 打印当前系统所有配置信息以供调试
 */
void dev_config_print(void);

/**
 * @brief 应用新的网络配置，更新全局配置结构体，并将其保存到Flash。
 *
 * @param ip_str 新的IP地址字符串 (e.g., "192.168.1.10")。
 * @param netmask_str 新的子网掩码字符串 (e.g., "255.255.255.0")。
 * @param gateway_str 新的网关字符串 (e.g., "192.168.1.1")。
 * @return int OK 成功, ERROR 失败。
 */
int dev_network_settings_apply(const char *ip_str, const char *netmask_str, const char *gateway_str, const unsigned char index);

int is_device_locked(void);

void dev_config_dump_all(void);

int save_config(void);


/*********************重启标志位相关****************************/
int set_restore_flag(void);
int clear_restore_flag(void);
int8_t read_restore_flag(void);


#endif /* APP_DEV_H */
