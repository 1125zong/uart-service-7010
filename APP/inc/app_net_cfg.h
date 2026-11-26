#ifndef APP_NET_CGF_H
#define APP_NET_CGF_H

#include "app_lan_config.h"

/* 文本转换仅用于屏幕和启动阶段，允许十进制前导零。 */
int net_cfg_parse_ipv4(const char *text, uint32_t *address);
/* 参数为网络字节序；普通局域网配置支持 /1 到 /30。 */
int net_cfg_validate_static_settings(uint32_t ip, uint32_t mask, uint32_t gateway);
const char *net_cfg_interface_name(int index);
int net_cfg_set_mac_address(int index, const unsigned char mac[6]);
int net_cfg_apply_settings(int index, const NetPortConfig *config);

#endif
