#ifndef __APP_CONSTANTS_H__
#define __APP_CONSTANTS_H__

/*
 * =====================================================================================
 *
 * Filename:  app_constants.h
 *
 * Description:  应用程序基本常量定义，避免头文件循环依赖
 *
 * =====================================================================================
 */

/* ------------------ Application-Specific Constants ------------------ */
#define     NUM_PORTS                   16      // 系统支持的串口/通道数量
#define     DEFAULT_REAL_COM_MAX_CONNECTIONS  4   // 默认每个串口的最大连接数
#define     MAX_CLIENTS_PER_CHANNEL     (DEFAULT_REAL_COM_MAX_CONNECTIONS)
#define     RING_BUFFER_SIZE            (64 * 1024) // 64KB 环形缓冲区大小
#define     MAX_CONFIG_CLIENTS          (NUM_PORTS * (MAX_CLIENTS_PER_CHANNEL+1) + 1) // 最大配置客户端数量

#define     MAX_ALIAS_LEN               19
#define     MAX_MODEL_NAME_LEN          39
#define     MAX_DEVICE_NAME_LEN         20
#define     MAX_SERVER_NAME_LEN         39
#define     MAX_PASSWORD_LEN            14
#define     SERIAL_NO_LEN               8

#define     MAX_LANS                    2

// 网络相关常量
#define     NET_NUM                     2       // 网络接口数量

/* ------------------ Basic Enumerations (to avoid circular dependencies) ------------------ */
// Operation modes
typedef enum 
{
    OP_MODE_REAL_COM     = 0x01, // Real COM Mode
    OP_MODE_TCP_SERVER   = 0x02, // TCP Server Mode
    OP_MODE_TCP_CLIENT   = 0x03, // TCP Client Mode
    OP_MODE_UDP          = 0x04, // UDP Mode
    OP_MODE_DISABLED     = 0xFF  // Disabled Mode
} OperationMode;

// Delimiter processing modes
typedef enum 
{
    DELIMITER_PROCESS_NONE          = 0x01, // 不做任何处理 (Do Nothing)
    DELIMITER_PROCESS_APPEND_DELIM1 = 0x02, // 附加分隔符1 (Delimiter+1)
    DELIMITER_PROCESS_APPEND_DELIM2 = 0x03, // 附加分隔符2 (Delimiter+2)
    DELIMITER_PROCESS_STRIP         = 0x04  // 剥离分隔符 (Strip Delimiter)
} DelimiterProcess;

// Network states
typedef enum 
{
    NET_STATE_IDLE,         // 初始状态。任务尚未开始监听此端口。
    NET_STATE_LISTENING,    // 正常状态。监听Socket已成功创建并正在等待连接。此时没有活跃的客户端。
    NET_STATE_CONNECTED,    // 正常状态。至少有一个客户端已连接，正在进行数据交换。
    NET_STATE_ERROR         // 异常状态。创建监听Socket失败 (例如，端口被占用)。
} NetworkChannelState;

// UART physical states
typedef enum 
{
    UART_STATE_CLOSED,      // 初始状态。串口未被HAL层初始化或已关闭。
    UART_STATE_OPENED,      // 正常工作状态。串口已由HAL层成功打开并配置。
    UART_STATE_ERROR        // 异常状态。HAL层在尝试打开或配置串口时发生硬件错误。
} UartPhysicalState;

// Interface types
typedef enum 
{ 
    INTERFACE_TYPE_RS232   = 0x00,
    INTERFACE_TYPE_RS422   = 0x01,
    INTERFACE_TYPE_RS485   = 0x02,
    INTERFACE_TYPE_CAN     = 0x03
} Interface_Mode;

#endif /* APP_CONSTANTS_H */