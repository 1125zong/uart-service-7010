#ifndef APP_LAN_CONFIG_H
#define APP_LAN_CONFIG_H

#include <stdint.h>

/* 配置容量保持两路，不改变现有持久化结构。 */
#define NET_NUM 2

/* 当前产品只启用 LAN1；核实 LAN2 硬件后改为 2。 */
#define APP_LAN_COUNT 1
#define MAX_LANS APP_LAN_COUNT

#if APP_LAN_COUNT < 1 || APP_LAN_COUNT > NET_NUM
#error "Invalid APP_LAN_COUNT"
#endif

/* 仅用于参数传递，不直接加入持久化结构。地址均为网络字节序。 */
typedef struct
{
    unsigned char mode;
    uint32_t ip;
    uint32_t mask;
    uint32_t gateway;
} NetPortConfig;

#endif
