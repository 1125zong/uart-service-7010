/*
 * =====================================================================================
 *
 * Filename:  app_report_network.c
 *
 * Description:  网络设置相关处理函数
 *
 * =====================================================================================
 */

#include "app_com.h"
#include "app_report_network.h"
#include "hal_flash.h"
#include "app_dev.h"  // 包含设备配置头文件
#include <string.h>   // 包含memcpy等函数
#include <netinet/in.h> // 包含struct in_addr定义
#include <arpa/inet.h>  // 包含inet_ntoa等网络地址转换函数
#include <arpa/inet.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>

static void handle_read_network_settings(int client_fd, unsigned char *data);
static void handle_write_network_settings(int client_fd, unsigned char *data, int len);

static int get_ip_address(const char *ifname, char *ip_buf, size_t buf_size);
static int dhcp(const char *iface, char *ip_buf, int ip_len, char *mask_buf, int mask_len, char *gw_buf, int gw_len);   
extern void send_report_response(int client_fd, const unsigned char main_id,  unsigned char sub_id, unsigned char status);

extern int set_device_ip_address1(uint32_t net_ip);

/* DHCP 获取到新 IP 后，是否保留旧 IP 立即恢复（不立即生效） */
/* 置 1：网卡继续使用旧的 IP 地址，DHCP 新 IP 仅采集返回给上层保存配置 */
/* 置 0：DHCP 获取到 IP 后立即生效 */
#define KEEP_OLD_IP_AFTER_DHCP   1

/**
 * @brief 处理0x03 - 网络设置操作
 */
void handle_network_settings(int client_fd, unsigned char *data, int len)
{
    unsigned char sub_id = data[3];
    //unsigned char main_id = data[2];
    
    LOG_INFO("  Action: Network Settings, main_id=0x%02X  SubID=0x%02X\n", data[2], sub_id);
    
    if (sub_id == 0x01) 
    { 
    	/* 读取网络设置 */
        handle_read_network_settings(client_fd, data);
    }
    else if (sub_id == 0x02)
    { 
    	/* 设置网络设置 */
        handle_write_network_settings(client_fd, data, len);
    }
}

/**
 * @brief 处理0x03 - 设置网络设置操作
 */
static void handle_write_network_settings(int client_fd, unsigned char *data, int len)
{
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3];
    
    if(len < 0)
    {
        LOG_ERROR("  ERROR: Invalid data length\n");
    }
 
    unsigned char network_path   = data[4]; // 0x01:第一路, 0x02:第二路, 0x03:两路同时
    unsigned char ip_config_mode = data[5];//动态或静态设置
    if(network_path == 0x01 )
    {
        LOG_INFO("  Network path: 0x%02X\n", network_path);
        LOG_INFO("setting network path 1\n");
    }
    else if (network_path == 0x02 ) 
    {
        LOG_INFO("  Network path: 0x%02X\n", network_path);
        LOG_INFO("setting network path 2\n");
    }
    else if(network_path == 0x03)
    {
        LOG_INFO("setting network path both 1&&2\n");
    }
    
    LOG_INFO("  IP Config Mode: %s\n", ip_config_mode == 0x01 ? "Static" : "Dynamic");
       
    //检查设备是否锁定
    if (is_device_locked()) 
    {
        LOG_ERROR("  ERROR: Device is locked, cannot update network settings\n");
        send_report_response(client_fd, main_id, sub_id, FAILED);
        return;
    }
    
    semTake(g_config_mutex, WAIT_FOREVER);

    //静态设置ip地址
    if(ip_config_mode == 0x01)
    {
        if (network_path == 0x01 || network_path == 0x03) { /* 设置第一路网络 */
            /* 网络模式 */
            g_system_config.device.ip_config_mode[0] = data[5];
            /* IP地址1 */
            unsigned int new_ip1;
            new_ip1 = data[6] << 24 | data[7] << 16 | data[8] << 8 | data[9];

            LOG_INFO("new_ip1: 0x%04x\n", new_ip1);

            g_system_config.device.ip_address[0] = (new_ip1);
            
            g_system_config.device.ip_address1 = (new_ip1);
            
            /* 子网掩码1 */
            unsigned int new_mask1;
            new_mask1 = data[10] << 24 | data[11] << 16 | data[12] << 8 | data[13];
            g_system_config.device.netmask[0] = (new_mask1);
        
            /* 网关1 */
            unsigned int new_gw1;        
            new_gw1 = data[14] << 24 | data[15] << 16 | data[16] << 8 | data[17];
            g_system_config.device.gateway[0] = (new_gw1);
            
            /* 立即生效：配置第一路静态IP */
            {
                //set_device_ip_address1(new_ip1);
            }
                
        }
        
        if (network_path == 0x02 || network_path == 0x03) 
        { 
            /* 设置第二路网络 */
            int base_offset = (network_path == 0x02) ? 6 : 19; // 如果只设置第二路，偏移从5开始；如果同时设置两路，偏移从18开始
            
            /* 网络模式 */
            if (network_path == 0x02) {
                g_system_config.device.ip_config_mode[1] = data[5];
            }
            if (network_path == 0x03) {
                g_system_config.device.ip_config_mode[1] = data[18];
            }
            
            /* IP地址2 */
            unsigned int new_ip2;
            //memcpy(&new_ip2, &data[base_offset], 4);
            new_ip2 = data[base_offset] << 24 | data[base_offset + 1] << 16 | data[base_offset + 2] << 8 | data[base_offset + 3];
            g_system_config.device.ip_address[1] = (new_ip2);
            g_system_config.device.ip_address2 = (new_ip2);
            
            /* 立即生效：配置第二路静态IP */
            {
#if 0
                char cmd[256];
                struct in_addr ip4_addr;
                ip4_addr.s_addr = new_ip2;
                snprintf(cmd, sizeof(cmd), "ifconfig eth1 %s netmask 255.255.255.0",
                         inet_ntoa(ip4_addr));
                system(cmd);
                LOG_INFO("  Static IP applied: eth1 %s\n", inet_ntoa(ip4_addr));
#endif
            }
        }
    }

    //动态设置ip地址
    else if(ip_config_mode == 0x02)
    {
        char ip[INET_ADDRSTRLEN];  // 用来接收获取到的Ip
        char netmask[32];
        char gateway[32];
        if (dhcp("eth1", ip, sizeof(ip), netmask, sizeof(netmask), gateway, sizeof(gateway)) == 0) 
        {
            printf("调用成功,IP = %s\n", ip); // 这里直接用 ip 即可
            printf("掩码: %s\n", netmask);
            printf("网关: %s\n", gateway);
            
            /* 网络模式 */
            g_system_config.device.ip_config_mode[0] = ip_config_mode;
            /* IP地址1 */
            

            g_system_config.device.ip_address[0] = inet_addr(ip);
            
            g_system_config.device.ip_address1 = inet_addr(ip);
            
            /* 子网掩码1 */
            g_system_config.device.netmask[0] = inet_addr(netmask);
        
            /* 网关1 */
            g_system_config.device.gateway[0] = inet_addr(gateway);


        } 
        else 
        {
            LOG_INFO("DHCP failed\n");
            //解锁返回
            send_report_response(client_fd, main_id, sub_id, FAILED);
            semGive(g_config_mutex);
            return;

        }
    }

    //保存配置
    save_config();
    
    semGive(g_config_mutex);
     
    /* 发送ACK */
    send_report_response(client_fd,  main_id,   sub_id, SUCCESSED);
    LOG_INFO("  Network settings updated successfully\n");
}

/**
 * @brief 处理0x01 - 读取网络设置操作
 */
static void handle_read_network_settings(int client_fd, unsigned char *data)
{
    unsigned char response[1024];
    int offset = 0;
    int i = 0;
    uint32_t temp_ip;
    uint8_t main_id = data[2];
    uint8_t sub_id  = data[3]; 
    
    // 构建响应报文头部
    response[offset++] = 0xA5; 
    response[offset++] = 0xA5;
    response[offset++] = main_id; 
    response[offset++] = sub_id;
    
    semTake(g_config_mutex, WAIT_FOREVER);
    {
        /* Lan num */
        response[offset++] = g_system_config.device.lan_count;

        /* ip1 mode */
        response[offset++] = g_system_config.device.ip_config_mode[0];
    	// 填充第一路网络信息
        {
            /* IP地址1 */
            unsigned int temp_ip = (g_system_config.device.ip_address[0]);
            memcpy(&response[offset], &temp_ip, 4);
            offset += 4;
            
            /* 子网掩码1 */
            temp_ip = (g_system_config.device.netmask[0]);
            memcpy(&response[offset], &temp_ip, 4);
            offset += 4;
            
            /* 网关1 */
            temp_ip = (g_system_config.device.gateway[0]);
            memcpy(&response[offset], &temp_ip, 4);
            offset += 4;
            
            /* MAC地址1 */
            memcpy(&response[offset], g_system_config.device.mac_address1, 6);
            offset += 6;

            /* speed */
            response[offset++] = g_system_config.device.lan_speed1;
        }
        
        // 填充第二路网络信息
        {
            response[offset++] = g_system_config.device.ip_config_mode[1];           
            /* IP地址2 */
            temp_ip = (g_system_config.device.ip_address2);
            memcpy(&response[offset], &temp_ip, 4);
            offset += 4;
            
            /* 子网掩码2 */
            temp_ip = (g_system_config.device.netmask[1]);  // 使用相同的子网掩码
            memcpy(&response[offset], &temp_ip, 4);
            offset += 4;
            
            /* 网关2 */
            temp_ip = (g_system_config.device.gateway[1]);   // 使用相同的网关
            memcpy(&response[offset], &temp_ip, 4);
            offset += 4;
            
            /* MAC地址2 */
            memcpy(&response[offset], g_system_config.device.mac_address2, 6);
            offset += 6;

            response[offset++] = g_system_config.device.lan_speed2;
        }
                
        /* 预留DNS服务器位置，但不实际使用 */
        // DNS服务器1
        memset(&response[offset], 0, 4);
        offset += 4;
        
        // DNS服务器2
        memset(&response[offset], 0, 4);
        offset += 4;
        
        /* 看门狗状态：1=使能, 0=禁用 */
        response[offset++] = g_system_config.device.watchdog_enable;
        LOG_INFO("  Watchdog status: %d\n", g_system_config.device.watchdog_enable);
    }
    semGive(g_config_mutex);
    
    // 添加响应报文尾部
    response[offset++] = 0x5A; 
    response[offset++] = 0x5A;
    
    // 打印将要发送的数据
    LOG_INFO("Sending network settings response:\n");
    for (i = 0; i < offset; i++) {
        if (i % 16 == 0 && i > 0) {
            LOG_INFO("\n");
        }
        LOG_INFO("%02X ", response[i]);
    }
    LOG_INFO("\n");
    
    send(client_fd, (char*)response, offset, 0);
    LOG_INFO("  Sent Network Settings read response\n");
}



// 函数声明：获取指定网卡的IPv4地址，存入ip_buf（需确保足够大）
static int get_ip_address(const char *ifname, char *ip_buf, size_t buf_size) 
{
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) 
    {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
    {
        if (ifa->ifa_addr == NULL) 
            continue;
        if (ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, ifname) == 0) {
            void *addr_ptr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr_ptr, ip_buf, buf_size);
            found = 1;
            break;
        }
    }

    //打印ip地址
    LOG_INFO("ip_buf: %s\n", ip_buf);

    freeifaddrs(ifaddr);
    return found ? 0 : -1;
}

// 辅助函数：获取 子网掩码
static int get_netmask(const char *iface, char *mask_buf, int buf_len)
{
    if (!iface || !mask_buf || buf_len <= 0) return -1;
    
    FILE *fp;
    char cmd[128];
    char mask[64];

    snprintf(cmd, sizeof(cmd), "ifconfig %s 2>/dev/null | grep 'Mask' | awk '{print $4}' | cut -d: -f2", iface);
    fp = popen(cmd, "r");
    if (!fp) return -1;

    if (fgets(mask, sizeof(mask), fp) != NULL) 
    {
        mask[strcspn(mask, "\n")] = 0;
        strncpy(mask_buf, mask, buf_len - 1);
        mask_buf[buf_len - 1] = 0;
    } 
    else 
    {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    return 0;
}

// 辅助函数：获取 网关（udhcpc 获得的网关）
static int get_gateway(const char *iface, char *gw_buf, int buf_len)
{
    if (!iface || !gw_buf || buf_len <= 0) return -1;
    
    FILE *fp;
    char cmd[128];
    char gw[64];

    snprintf(cmd, sizeof(cmd), "ip route show 2>/dev/null | grep default | awk '{print $3}'");
    fp = popen(cmd, "r");
    if (!fp) return -1;

    if (fgets(gw, sizeof(gw), fp) != NULL) 
    {
        gw[strcspn(gw, "\n")] = 0;
        strncpy(gw_buf, gw, buf_len - 1);
        gw_buf[buf_len - 1] = 0;
    } 
    else 
    {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    return 0;
}

// =======================================================================
//  函数名：dhcp
//  入参：iface    网卡名（eth0/eth1/ens33/ens37）
//  出参：ip_buf   获取到的IP地址
//  出参：mask_buf 获取到的子网掩码
//  出参：gw_buf   获取到的网关
//  返回：0 成功，1 失败
// =======================================================================
static int dhcp(const char *iface, char *ip_buf, int ip_len, char *mask_buf, int mask_len, char *gw_buf, int gw_len)
{
    int ret;

    // 参数安全检查
    if (!iface || !ip_buf || !mask_buf || !gw_buf) {
        fprintf(stderr, "参数错误\n");
        return 1;
    }

    // 启用网卡
    char up_cmd[256];
    snprintf(up_cmd, sizeof(up_cmd), "ifconfig %s up", iface);
    system(up_cmd);

    // 备份旧IP（失败恢复）
    char old_ip[64] = {0};
    char backup_cmd[256];
    snprintf(backup_cmd, sizeof(backup_cmd), "ip addr show %s | grep -w inet | awk '{print $2}' | head -n1", iface);
    FILE *fp = popen(backup_cmd, "r");
    if (fp) {
        fgets(old_ip, sizeof(old_ip)-1, fp);
        pclose(fp);
        old_ip[strcspn(old_ip, "\n")] = 0;
    }

    // 执行 DHCP
    char dhcp_cmd[256];
    snprintf(dhcp_cmd, sizeof(dhcp_cmd), "udhcpc -i %s -q -t 3 -T 1 -n", iface);
    LOG_INFO("exec DHCP: %s\n", dhcp_cmd);
    ret = system(dhcp_cmd);

    // DHCP 失败 → 恢复旧IP
    if (ret != 0) {
        fprintf(stderr, "DHCP get ip failed!\n");
        if (strlen(old_ip) > 0) {
            char recover_cmd[256];
            snprintf(recover_cmd, sizeof(recover_cmd), "ip addr add %s dev %s", old_ip, iface);
            system(recover_cmd);
            LOG_INFO("reback IP: %s\n", old_ip);
        }
        return 1;
    }

#if KEEP_OLD_IP_AFTER_DHCP
    /* 需求：DHCP 获取到新 IP 后不立即生效，网卡继续使用旧 IP。
     * 先采集新 IP/掩码/网关到出参（供上层保存配置），
     * 再把旧 IP 重新绑定回网卡，使当前通信不受影响。 */
    if (get_ip_address(iface, ip_buf, ip_len) != 0 ||
        get_netmask(iface, mask_buf, mask_len) != 0 ||
        get_gateway(iface, gw_buf, gw_len) != 0)
    {
        fprintf(stderr, "get net info failed!\n");
        return 1;
    }

    /* 恢复旧 IP（仅当存在旧 IP 且与 DHCP 新 IP 不同时） */
    if (strlen(old_ip) > 0 && strcmp(old_ip, ip_buf) != 0) {
        /* 先清除网卡上的新地址，再绑定回旧地址 */
        char reset_cmd[256];
        snprintf(reset_cmd, sizeof(reset_cmd),
                 "ip addr flush dev %s; ip addr add %s dev %s",
                 iface, old_ip, iface);
        system(reset_cmd);
        LOG_INFO("DHCP IP kept not applied, keep old IP: %s\n", old_ip);
    }

    LOG_INFO("old IP preserved: %s (DHCP got: %s)\n", old_ip, ip_buf);
#else
    /* 原逻辑：DHCP 获取到 IP 后立即生效 */
    if (get_ip_address(iface, ip_buf, ip_len) != 0 ||
        get_netmask(iface, mask_buf, mask_len) != 0 ||
        get_gateway(iface, gw_buf, gw_len) != 0)
    {
        fprintf(stderr, "get net info failed!\n");
        return 1;
    }
#endif

    LOG_INFO("DHCP succeed\n");
    LOG_INFO("IP: %s\n netmask: %s\n gateway: %s\n", ip_buf, mask_buf, gw_buf);
    return 0;
}