#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <net/if.h>
#include <sys/ioctl.h>

// 函数声明：获取指定网卡的IPv4地址，存入ip_buf（需确保足够大）
int get_ip_address(const char *ifname, char *ip_buf, size_t buf_size) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family == AF_INET &&
            strcmp(ifa->ifa_name, ifname) == 0) {
            void *addr_ptr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr_ptr, ip_buf, buf_size);
            found = 1;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return found ? 0 : -1;
}

int dhcp() 
{
    const char *iface = "eth1";  // 注意：用户写的enss33可能是笔误，根据实际修改
    char ip[INET_ADDRSTRLEN];
    int ret;

    // 1. 确保网卡已启用（可选，但推荐）
    char up_cmd[256];
    snprintf(up_cmd, sizeof(up_cmd), "ifconfig %s up", iface);
    system(up_cmd);

    // 2. 执行 udhcpc，获取IP后自动退出
    char dhcp_cmd[256];               //udhcpc -i eth1 -q -t 3 -T 1 -n
    snprintf(dhcp_cmd, sizeof(dhcp_cmd), "udhcpc -i %s -q -q -t 3 -T 1 -n", iface);
    printf("执行命令: %s\n", dhcp_cmd);
    ret = system(dhcp_cmd);
    if (ret != 0) {
        fprintf(stderr, "udhcpc 执行失败，返回码: %d\n", ret);
        // 可以进一步检查错误，比如网线未插、DHCP服务器不可达等
        return 1;
    }

    // 3. 等待一瞬间，确保内核完全更新（可选，通常不需要）
   // usleep(100000);  // 100ms

    // 4. 获取IP地址
    if (get_ip_address(iface, ip, sizeof(ip)) == 0) {
        printf("动态获取的 IP 地址: %s\n", ip);
    } else {
        fprintf(stderr, "未能获取到 %s 的 IP 地址\n", iface);
        return 1;
    }

    return 0;
}
