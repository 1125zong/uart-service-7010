/* * app_data.h
 * (这个文件不属于 UI 模块，而是您的主应用程序文件)
 * (UI 模块会包含它，以便知道如何解析 user_data)
 */
#ifndef APP_DATA_H
#define APP_DATA_H

// [新增] IP 配置类型 (DHCP/Static)
typedef enum {
    IP_CONFIG_DHCP,
    IP_CONFIG_STATIC
} ip_config_mode_t;

// [新增] 设备主状态
typedef enum {
    STATUS_NORMAL,
    STATUS_LOCK,
    STATUS_ERROR,
    STATUS_REBOOT_REQUIRED // 用于提示 "reboot"
} device_status_t;


// [新增] 定义一个网口的数据结构
typedef struct {
    char ethernet_status[16]; // "100M/Link", "No Link" 等
    char mac_address[18];     // "00:11:22:33:44:55"
    char ip_address[16];      // "192.168.0.123"
    char network_mask[16];    // "255.255.255.0"
    char gateway[16];         // "192.168.0.1"
    ip_config_mode_t ip_config; // DHCP or Static
    
} lan_port_t;


typedef struct {
    // --- [修改] 通用设置 ---
    char model_name[20]; // e.g., "QPort125S"
    char model[20];      // [新增] e.g., "65809"
    
    // --- [修改] 状态 ---
    device_status_t current_status;
    int error_code; // 仅在 current_status == STATUS_ERROR 时使用
    
    // --- Device Info ---
    char firmware_version[12];
    char serial_number[32];

    // --- Lock Password ---
    char lock_password[8]; 

    // --- 网络设置 ---
    lan_port_t lan_ports[2]; 
    
} app_config_t;


#endif // APP_DATA_H

