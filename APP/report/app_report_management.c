/*
 * =====================================================================================
 *
 * Filename:  app_report_lock.c
 *
 * Description:  设备锁定和解锁功能实现
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/reboot.h>

#include "app_report_lock.h"
#include "app_com.h"
#include "app_report.h"
#include "hal_flash.h"
#include "app_dev.h"
#include "app_ports.h"
#include "app_flash_config.h"

#include "hal_axi16550.h"
#include "app_data.h"

/* 外部变量和函数声明 */
extern void device_locked_led(int action);
extern SEM_ID g_config_mutex;

/* 密码长度定义 */
#define LOCK_PASSWORD_LEN 8

#define ON  1
#define OFF 0

/* 引入看门狗模块 */
#include "app_watchdog.h"

/**
 * @brief 从Flash读取网络配置
 * @details 从FLASH_START_ADDR_NET_INFO处读取存储的网络相关信息
 * @return int OK on success, ERROR on failure.
 */
static int read_config_from_flash(void)
{
    int result;
    char buffer[256];  // 缓冲区用于存储从Flash读取的网络配置数据
    int total_size = 41; // 根据save_config函数计算的总字节数
    int i = 0;
    // 从Flash读取网络配置数据
    result = hal_flash_read(FLASH_START_ADDR_NET_INFO, buffer, total_size);
    
    if (result <= 0)
    {
        LOG_ERROR("Failed to read network configuration from Flash, error code: %d\n", result);
        return ERROR;
    }
    
    // 分别打印两路网络的IP地址、子网掩码、网关、MAC地址的原始数据和转换后的数据
    LOG_INFO("=== Network Configuration Details ===\n");
    
    // 第一路网络配置
    LOG_INFO("First Network Interface:\n");
    LOG_INFO("Raw IP Address bytes: ");
    for (i = 0; i < 4; i++) {
        LOG_INFO("%02X ", (unsigned char)buffer[i]);
    }
    LOG_INFO("\n");
    LOG_INFO("Converted IP Address: %d.%d.%d.%d\n", 
             (unsigned char)buffer[0], (unsigned char)buffer[1], 
             (unsigned char)buffer[2], (unsigned char)buffer[3]);
             
    LOG_INFO("Raw Netmask bytes: ");
    for (i = 4; i < 8; i++) {
        LOG_INFO("%02X ", (unsigned char)buffer[i]);
    }
    LOG_INFO("\n");
    LOG_INFO("  Converted Netmask: %d.%d.%d.%d\n", 
             (unsigned char)buffer[4], (unsigned char)buffer[5], 
             (unsigned char)buffer[6], (unsigned char)buffer[7]);
             
    LOG_INFO("Raw Gateway bytes: ");
    for (i = 8; i < 12; i++) {
        LOG_INFO("%02X ", (unsigned char)buffer[i]);
    }
    LOG_INFO("\n");
    LOG_INFO("Converted Gateway: %d.%d.%d.%d\n", 
             (unsigned char)buffer[8], (unsigned char)buffer[9], 
             (unsigned char)buffer[10], (unsigned char)buffer[11]);
             
    LOG_INFO("Raw MAC Address bytes: ");
    for (i = 12; i < 18; i++) {
        LOG_INFO("%02X ", (unsigned char)buffer[i]);
    }
    LOG_INFO("\n");
    LOG_INFO("Converted MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
             (unsigned char)buffer[12], (unsigned char)buffer[13], (unsigned char)buffer[14],
             (unsigned char)buffer[15], (unsigned char)buffer[16], (unsigned char)buffer[17]);
             
    // 第二路网络配置
    LOG_INFO("Second Network Interface:\n");
    LOG_INFO("  Raw IP Address bytes: ");
    for (i = 18; i < 22; i++) {
        LOG_INFO("%02X ", (unsigned char)buffer[i]);
    }
    LOG_INFO("\n");
    LOG_INFO("Converted IP Address: %d.%d.%d.%d\n", 
             (unsigned char)buffer[18], (unsigned char)buffer[19], 
             (unsigned char)buffer[20], (unsigned char)buffer[21]);
             
    LOG_INFO("Raw Netmask bytes: ");
    for (i = 22; i < 26; i++) {
        LOG_INFO("%02X ", (unsigned char)buffer[i]);
    }
    LOG_INFO("\n");
    LOG_INFO("Converted Netmask: %d.%d.%d.%d\n", 
             (unsigned char)buffer[22], (unsigned char)buffer[23], 
             (unsigned char)buffer[24], (unsigned char)buffer[25]);
             
    LOG_INFO("  Raw Gateway bytes: ");
    for (i = 26; i < 30; i++) {
        LOG_INFO("%02X ", (unsigned char)buffer[i]);
    }
    LOG_INFO("\n");
    LOG_INFO("Converted Gateway: %d.%d.%d.%d\n", 
             (unsigned char)buffer[26], (unsigned char)buffer[27], 
             (unsigned char)buffer[28], (unsigned char)buffer[29]);
             
    LOG_INFO("Raw MAC Address bytes: ");
    for (i = 30; i < 36; i++) {
        LOG_INFO("%02X ", (unsigned char)buffer[i]);
    }
    LOG_INFO("\n");
    LOG_INFO("Converted MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
             (unsigned char)buffer[30], (unsigned char)buffer[31], (unsigned char)buffer[32],
             (unsigned char)buffer[33], (unsigned char)buffer[34], (unsigned char)buffer[35]);          
    // 打印读取到的网络配置信息
    LOG_INFO("Network configuration read from Flash successfully\n");    
    return OK;
}



/**
 * @brief 处理设备重启命令
 * @details 功能ID: 0x08, 子功能ID: 0x0C
 */
void handle_device_reboot(int client_fd, unsigned char *data, int len)
{
    int ret;
    unsigned char sub_id  = data[3];  
    unsigned char main_id = data[2];  
    
    LOG_INFO("  Action: Device Reboot,main_id=0x%02X  SubID=0x%02X\n", main_id, sub_id);
    if(len < 0)
    {
        LOG_ERROR("  ERROR: Invalid data length\n");
    }
    
    // 验证子功能ID
    if (sub_id != 0x0C)
    {
        LOG_ERROR("  ERROR: Invalid sub ID for reboot command: 0x%02X\n", sub_id);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    //保存全局配置
    
	if (dev_config_save() != OK)
	{
		LOG_ERROR("ERROR[reboot]: Failed to save  configuration to flash.\n");
		send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
	}
	
	//保存网络相关信息
	ret = save_config();
    if(ret == OK)
    {
        LOG_INFO("  Network configuration saved to Flash successfully\n");

        read_config_from_flash();

    }  
    // 发送确认响应
    send_report_response(client_fd, main_id,  sub_id, SUCCESSED);
    
    // 调用设备重启函数
    LOG_INFO("  Rebooting device...\n");
    usleep(1000000); // 1000ms
    dev_reboot();
}

/**
 * @brief   生成随机密码
 * @details 生成一个8位随机密码
 */
static void generate_random_password(char *password, int len)
{
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    int charset_len = sizeof(charset) - 1;
    int i = 0;
    
    srand(time(NULL));  // 初始化随机数种子
    
    for (i = 0; i < len - 1; i++)
    {
        int key = rand() % charset_len;
        password[i] = charset[key];
    }
    password[len - 1] = '\0';  // 字符串结束符
}

/**
 * @brief 处理设备锁定命令
 * @details 功能ID: 0x08, 子功能ID: 0x06
 * 请求格式：A5 A5 08 06 6字节MAC地址 5A 5A
 * 响应格式：A5 A5 08 06 01(成功) 14字节密码 5A 5A
 */
extern app_config_t* get_app_data(void);
void handle_device_lock(int client_fd, unsigned char *data, int len)
{
    extern void refresh_password_locked_interface(void);
    extern void refresh_main_interface(void);
    
    unsigned char response[1024];
    int offset = 0;
    int ret = 0;
    char lock_password[LOCK_PASSWORD_LEN + 1];  // +1 for null terminator
    unsigned char client_mac[6];

    uint8_t main_id = data[2];
    uint8_t sub_id = data[3];

    LOG_INFO("  Action: Device Lock, SubID=0x06\n");

    // 检查数据长度：帧头(2) + 主功能ID(1) + 子功能ID(1) + MAC地址(6) + 帧尾(2)
    if (len < 2 + 1 + 1 + 6 + 2)
    {
        LOG_ERROR("  ERROR: Invalid data length, expected at least %d, got %d\n", 2 + 1 + 1 + 6 + 2, len);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }

    // 从请求数据中提取MAC地址（紧跟在命令后面）
    memcpy(client_mac, &data[4], 6);

    LOG_INFO("  Client MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
             client_mac[0], client_mac[1], client_mac[2],
             client_mac[3], client_mac[4], client_mac[5]);

    //获取锁定的当前pc机ip地址
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len) != 0)
    {
        LOG_ERROR("  ERROR: Failed to get client address, errno: %d", errno);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    else
    {
        //打印客户端IP地址
        LOG_INFO("  Client IP address: %s\n", inet_ntoa(client_addr.sin_addr));
        LOG_INFO("  Client IP address: 0x%x\n", client_addr.sin_addr.s_addr);
        LOG_INFO("  Client IP address: 0x%x\n", inet_addr(inet_ntoa(client_addr.sin_addr)));

    }

    //从全局变量g_config_mutex中获取当前状态是否锁定
    uint8_t lock_status = g_system_config.device.lock_status;
    LOG_INFO("  Current lock status: %d\n", lock_status);
    if (lock_status == LOCK_STATE_LOCKED)
    {
        LOG_ERROR("  ERROR: Device is already locked\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    // 生成随机密码
    generate_random_password(lock_password, LOCK_PASSWORD_LEN + 1);

    // 获取配置锁
    semTake(g_config_mutex, WAIT_FOREVER);

    g_system_config.device.locked_client_ip = client_addr.sin_addr.s_addr;
    memcpy(g_system_config.device.locked_client_mac, client_mac, 6);

    LOG_INFO("Locked Client IP: %s", inet_ntoa(*(struct in_addr*)&g_system_config.device.locked_client_ip));
    LOG_INFO("Locked Client IP: 0x%x", g_system_config.device.locked_client_ip);
    LOG_INFO("Locked Client MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
             g_system_config.device.locked_client_mac[0],
             g_system_config.device.locked_client_mac[1],
             g_system_config.device.locked_client_mac[2],
             g_system_config.device.locked_client_mac[3],
             g_system_config.device.locked_client_mac[4],
             g_system_config.device.locked_client_mac[5]);

    // 保存生成的密码到设备配置中
    strncpy(g_system_config.device.lock_password, lock_password, LOCK_PASSWORD_LEN);
    g_system_config.device.lock_password[LOCK_PASSWORD_LEN] = '\0';

    //打印密码
    LOG_INFO("  Lock password: %s\n", g_system_config.device.lock_password);

    // 设置锁定状态
    g_system_config.device.lock_status = LOCK_STATE_LOCKED;
    
    
    // 发送包含密码的响应
    response[offset++] = 0xA5; response[offset++] = 0xA5;
    response[offset++] = 0x08; response[offset++] = 0x06;
    response[offset++] = 0x01; /* 成功 */
    
    // 添加密码到响应
    memcpy(&response[offset], lock_password, LOCK_PASSWORD_LEN);
    offset += LOCK_PASSWORD_LEN;
    
    response[offset++] = 0x5A; response[offset++] = 0x5A;
    ret = send(client_fd, (char*)response, offset, 0);
    if (ret <= 0) 
    {
        LOG_ERROR("  ERROR: Failed to send response\n");
       // ret = send(client_fd, (char*)response, offset, 0);
    }

    //更新g_app_data中当前状态
    app_config_t *app_data = get_app_data();
    app_data->current_status = STATUS_LOCK;

    //写配置
    if (dev_config_save() != OK)
    {
        LOG_ERROR("ERROR[lock]: Failed to save configuration to flash.\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    //点亮锁定状态LED
    device_locked_led(ON);
    refresh_password_locked_interface();
    refresh_main_interface();
      // 释放配置锁
    semGive(g_config_mutex);  
    LOG_INFO("  Device locked successfully, password: %s\n", lock_password);
}

/**
 * @brief 处理设备解锁命令
 * @details 功能ID: 0x08, 子功能ID: 0x07
 * 请求格式：A5 A5 08 07 14字节密码 6字节MAC地址 5A 5A
 * 响应格式：A5 A5 08 07 01(成功)/0xE1(失败) 5A 5A
 */
void handle_device_unlock(int client_fd, unsigned char *data, int len)
{
    extern void refresh_password_locked_interface(void);
    extern void refresh_main_interface(void);
  
    unsigned char unlock_password[LOCK_PASSWORD_LEN];
    unsigned char client_mac[6];
    unsigned char sub_id  = data[3];
    unsigned char main_id = data[2];


    LOG_INFO("  Action: Device Unlock, SubID=0x07\n");

    // 检查数据长度：帧头(2) + 主功能ID(1) + 子功能ID(1) + 密码(14) + MAC地址(6) + 帧尾(2)
    if (len < 2 + 1 + 1 + LOCK_PASSWORD_LEN + 6 + 2) {
        LOG_ERROR("  ERROR: Unlock command data length too short, expected at least %d, got %d\n",
                  2 + 1 + 1 + LOCK_PASSWORD_LEN + 6 + 2, len);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }


    // 从数据中提取解锁密码
    memcpy(unlock_password, &data[4], LOCK_PASSWORD_LEN);

    // 从数据中提取MAC地址（紧跟在密码后面）
    memcpy(client_mac, &data[4 + LOCK_PASSWORD_LEN], 6);

    LOG_INFO("  Client MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
             client_mac[0], client_mac[1], client_mac[2],
             client_mac[3], client_mac[4], client_mac[5]);

    // 获取客户端IP地址
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len) != 0)
    {
        LOG_ERROR("  ERROR: Failed to get client address, errno: %d", errno);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    else
    {
        //打印客户端IP地址
        LOG_INFO("  Client IP address: %s\n", inet_ntoa(client_addr.sin_addr));
        LOG_INFO("  Client IP address: 0x%x\n", client_addr.sin_addr.s_addr);
        LOG_INFO("  Client IP address: 0x%x\n", inet_addr(inet_ntoa(client_addr.sin_addr)));
    }

    // 获取配置锁
    semTake(g_config_mutex, WAIT_FOREVER);

    // 验证密码是否正确
    if (memcmp(g_system_config.device.lock_password, unlock_password, LOCK_PASSWORD_LEN) == 0)
    {
        // 密码正确，设置解锁状态
        g_system_config.device.lock_status = LOCK_STATE_UNLOCK;
        LOG_INFO("  Device unlocked successfully with correct password\n");

        // 清空已使用的锁定密码
        memset(g_system_config.device.lock_password, 0, sizeof(g_system_config.device.lock_password));
        strcpy(g_system_config.device.lock_password, "NULL"); // 使用strcpy设置密码为"NULL"，确保密码为空
        g_system_config.device.locked_client_ip = 0;
        memset(g_system_config.device.locked_client_mac, 0, sizeof(g_system_config.device.locked_client_mac));

        // 发送成功响应
        send_report_response(client_fd, main_id, sub_id, SUCCESSED);

        device_locked_led(OFF);

        //更新g_app_data中当前状态
        app_config_t *app_data = get_app_data();
        app_data->current_status = STATUS_NORMAL;

        if (dev_config_save() != OK)
        {
            LOG_ERROR("ERROR[unlock]: Failed to save configuration to flash.\n");
            send_report_response(client_fd, main_id, sub_id, FAILED);
            semGive(g_config_mutex);
            return;
        }

        //刷新密码锁定界面
        LOG_INFO("  Lock password: %s\n", g_system_config.device.lock_password);
        refresh_password_locked_interface();
        refresh_main_interface();
    } 
    else 
    {
        // 密码错误，保持锁定状态
        LOG_ERROR("  ERROR: Incorrect unlock password\n");
        
        // 发送失败响应
        send_report_response(client_fd, main_id, sub_id, FAILED);
        semGive(g_config_mutex);
        return;
    }
    
    // 释放配置锁
    semGive(g_config_mutex);
}

  
/**
 * @brief 处理恢复出厂设置命令
 * @details 功能ID: 0x08, 子功能ID: 0x0A
 */

static void handle_factory_reset(int client_fd, unsigned char *data, int len)
{    
    int ret;    
    unsigned char sub_id  = data[3];  
    unsigned char main_id = data[2];  
    
    if(len < 0)
    {
        LOG_ERROR("  ERROR: Invalid data length\n");
    }
    LOG_INFO("  Action: Factory Reset, SubID=0x0A\n");

    //在flash的FLASH_START_ADDR_RESTORE_FLAG处写入恢复出厂设置标志位1
    ret = set_restore_flag();
    if (ret < 0) 
    {
        LOG_ERROR("  ERROR: Failed to write restore flag to flash\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    // 发送确认响应
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
    
    //重启
    LOG_INFO("  Rebooting device...\n");
    usleep(1000000); // 1000ms
    dev_reboot();
}

/**
 * @brief 处理配置看门狗命令
 * @details 功能ID: 0x08, 子功能ID: 0x09
 * @note 接收数据格式:
 *       0xA5 0xA5 | main_id | sub_id | enable | 0x5A 0x5A
 *       - enable: 1=使能, 0=禁用
 * @note 返回数据格式:
 *       0xA5 0xA5 | main_id | sub_id | status | 0x5A 0x5A
 *       - status: 0=成功, 1=失败
 */
static void handle_watchdog_config(int client_fd, unsigned char *data, int len)
{
    unsigned char sub_id  = data[3];  
    unsigned char main_id = data[2];  
    
    LOG_INFO("  Action: Watchdog Config, SubID=0x09\n");
    
    // 检查数据长度 (帧头2 + main_id 1 + sub_id 1 + enable 1 + 帧尾2 = 7)
    if(len < 7)
    {
        LOG_ERROR("  ERROR: Invalid data length: %d (expected >= 7)\n", len);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    //设备被锁定，直接返回
    if (is_device_locked())        
    {
        LOG_ERROR("  ERROR: Device is locked, cannot configure watchdog\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    // 解析配置参数
    unsigned char enable = data[4];
    
    LOG_INFO("  Watchdog Config: enable=%d\n", enable);
    
    // 参数校验
    if(enable > 1)
    {
        LOG_ERROR("  ERROR: Invalid watchdog enable value: %d (expected 0 or 1)\n", enable);
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    // 调用看门狗配置函数
    if(watchdog_config(enable) < 0)
    {
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    // 发送确认响应
    send_report_response(client_fd, main_id, sub_id, SUCCESSED);
}

/**
 * @brief 处理0x08 - 设备管理操作
 */
void handle_device_management(int client_fd, unsigned char *data, int len)
{
    unsigned char sub_id = data[3];
    
    LOG_INFO("  Action: Device Management, SubID=0x%02X\n", sub_id);

    switch (sub_id)
    {
        // 重置端口
        case 0x01:
        {
            handle_port_reset(client_fd, data, len);
            break;
        }
        //锁定端口
        case 0x02:
        {
            handle_port_lock(client_fd, data, len);
            break;
        }
        // 解锁端口
        case 0x03:
        {
            handle_port_unlock(client_fd, data, len);
            break;
        }
        // 查询端口锁定
        case 0x04:
        {
            handle_port_lock_query(client_fd, data, len);
            break;
        }
        // 配置端口镜像
        case 0x05:
        {
            handle_port_mirror_config(client_fd, data, len);
            break;
        }

        //串口自检
        case 0x08:
        {
            handle_port_self_check(client_fd, data);
            break;
        }
        
        // 设备锁定
        case 0x06:
        {
            handle_device_lock(client_fd, data, len);
            break;
        }

        // 设备解锁
        case 0x07:
        {
            handle_device_unlock(client_fd, data, len);
            break;
        }
       
        //配置看门狗
        case 0x09:
        {
            handle_watchdog_config(client_fd, data, len);
            break;
        }

        //恢复出厂设置
        case 0x0A:
        {
            handle_factory_reset(client_fd, data, len);
            break;
        }
                    
        // 设备重启
        case 0x0C:
        {
            handle_device_reboot(client_fd, data, len);
            break;
        }

        {
            default:
            LOG_ERROR("  ERROR: Invalid sub ID for device management: 0x%02X\n", sub_id);
            break;
        }
        
    }  
}
