/*
 * =====================================================================================
 *
 * Filename:  ConfigTaskManager.c
 *
 * Description:  实现 ConfigTaskManager 任务，包含完整的协议处理逻辑。
 *
 * =====================================================================================
 */
#include "app_net_cfg.h"
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>      // For inactivity timeout check
#include <stdlib.h>    // For atoi, etc.
#include <arpa/inet.h> // For htonl, ntohl etc.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_uart.h"
#include "app_net_proto.h"
#include "app_net.h"
#include "app_net_con.h"

// 内部宏定义
#define INACTIVITY_TIMEOUT_SECONDS 300 // 5分钟无活动则超时

/* ------------------ Private Function Prototypes ------------------ */
static void cleanup_config_connection(int index);
static int handle_config_client(int index);
static int handle_serial_port_command(ClientSession *session);
static void process_command_frame(int session_index, const unsigned char *frame, int len);

/* ------------------ Module-level static variables ------------------ */
ClientSession s_sessions[MAX_CONFIG_CLIENTS];
static int s_num_active_sessions = 0;

/* ------------------ Global Variable Definitions ------------------ */

/**
 * @brief ConfigTaskManager 的主入口函数，用来处理驱动发来的CMD命令
 */
void *ConfigTaskManager(void *arg)
{
    int i;
    int id = *(int *)arg;
    LOG_INFO("ConfigTaskManager: Starting, id=%d\n", id);
    LOG_INFO("ConfigTaskManager: Starting\n");

    int ret = pthread_setname_np(pthread_self(), "tConfigManager");
    if (ret != 0)
    {
        LOG_ERROR("ConfigTaskManager: Failed to set thread name: %s", strerror(errno));
        return NULL;
    }

    // 初始化会话列表
    for (i = 0; i < MAX_CONFIG_CLIENTS; i++)
    {
        s_sessions[i].fd = -1;
        s_sessions[i].rx_bytes = 0;
    }

    while (1)
    {
        /* ------------------ 1. 检查并接管新连接 ------------------ */
        NewConnectionMsg msg;

        // cmd
        while (msgQReceive(g_config_conn_q, (char *)&msg, sizeof(msg), NO_WAIT) == sizeof(NewConnectionMsg))
        {
            if (dev_is_rebooting())
            {
                shutdown(msg.client_fd, SHUT_RDWR);
                close(msg.client_fd);
                continue;
            }

            /* 先检查全局会话表容量，未接纳的连接不增加任何计数 */
            if (s_num_active_sessions >= MAX_CONFIG_CLIENTS)
            {
                LOG_WARN("Config session table full, rejecting fd=%d\n", msg.client_fd);
                close(msg.client_fd);
                continue;
            }

            /* 串口命令连接与数据连接分别限制 */
            if (msg.type == CONN_TYPE_REALCOM_CMD)
            {
                if (msg.channel_index < 0 || msg.channel_index >= NUM_PORTS)
                {
                    close(msg.client_fd);
                    continue;
                }

                if (semTake(g_config_mutex, WAIT_FOREVER) != OK)
                {
                    close(msg.client_fd);
                    continue;
                }

                if (dev_is_rebooting())
                {
                    semGive(g_config_mutex);
                    shutdown(msg.client_fd, SHUT_RDWR);
                    close(msg.client_fd);
                    continue;
                }

                ChannelState *channel = &g_system_config.channels[msg.channel_index];

                int connection_limit = channel->max_connections;
                int client_count = channel->cmd_net_info.num_clients;

                if (connection_limit < 1 || connection_limit > MAX_CLIENTS_PER_CHANNEL || client_count < 0 ||
                    client_count >= connection_limit || client_count >= MAX_CLIENTS_PER_CHANNEL)
                {
                    semGive(g_config_mutex);

                    LOG_WARN("Ch %d CMD connection rejected: count=%d, limit=%d\n", msg.channel_index, client_count,
                             connection_limit);

                    close(msg.client_fd);
                    continue;
                }

                channel->cmd_net_info.client_fds[client_count] = msg.client_fd;
                channel->cmd_net_info.num_clients = client_count + 1;
                channel->cmd_net_info.state = NET_STATE_CONNECTED;

                semGive(g_config_mutex);
            }

            /* 所有检查通过后，才正式加入会话表 */
            int new_index = s_num_active_sessions;

            s_sessions[new_index].fd = msg.client_fd;
            s_sessions[new_index].type = msg.type;
            s_sessions[new_index].channel_index = msg.channel_index;
            s_sessions[new_index].last_activity_time = time(NULL);
            s_sessions[new_index].rx_bytes = 0;
            s_sessions[new_index].uart_command_deferred = 0;

            s_num_active_sessions++;

            LOG_INFO("Accepted config connection fd=%d, type=%d, channel=%d\n", msg.client_fd, msg.type,
                     msg.channel_index);
        }

        if (s_num_active_sessions == 0)
        {
            // taskDelay(sysClkRateGet()); // 没有连接时，休眠1秒
            taskDelay(1); // 没有连接时，休眠1秒
            continue;
        }

        /* ------------------ 2. 构建select监听集合 ------------------ */
        fd_set read_fds;
        int max_fd = 0;
        int has_deferred_command = 0;
        FD_ZERO(&read_fds);

        for (i = 0; i < s_num_active_sessions; i++)
        {
            if (s_sessions[i].uart_command_deferred)
            {
                has_deferred_command = 1;
                if (s_sessions[i].rx_bytes == MAX_COMMAND_LEN)
                {
                    continue; /* 留出空间后再读取这个连接 */
                }
            }
            FD_SET(s_sessions[i].fd, &read_fds);
            if (s_sessions[i].fd > max_fd)
            {
                max_fd = s_sessions[i].fd;
            }
        }

        /* ------------------ 3. 阻塞等待事件 ------------------ */
        struct timeval timeout = {5, 0};
        if (has_deferred_command)
        {
            timeout.tv_sec = 0;
            timeout.tv_usec = 10000;
        }
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ret < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            LOG_ERROR("ConfigTaskManager: select() error");
            taskDelay(1);
            continue;
        }

        /* ------------------ 4. 处理就绪的连接和超时 ------------------ */
        time_t now = time(NULL);
        // 从后往前遍历，方便在循环中安全地移除断开的连接
        for (i = s_num_active_sessions - 1; i >= 0; i--)
        {
            int connection_alive = 1; // 1 for true
            if (FD_ISSET(s_sessions[i].fd, &read_fds))
            {
                // 有数据可读
                s_sessions[i].last_activity_time = now;
                connection_alive = handle_config_client(i);
            }
            else if (s_sessions[i].uart_command_deferred)
            {
                connection_alive = handle_serial_port_command(&s_sessions[i]);
            }
            else if (ret == 0 && s_sessions[i].type != CONN_TYPE_REALCOM_CMD)
            {
                // select超时，检查此连接是否不活动超时
                if ((now - s_sessions[i].last_activity_time) > INACTIVITY_TIMEOUT_SECONDS)
                {
                    LOG_INFO("ConfigTaskManager: fd=%d timed out due to inactivity.\n", s_sessions[i].fd);
                    connection_alive = 0; // 0 for false
                }
            }

            if (connection_alive == 0)
            {
                cleanup_config_connection(i);
            }
        }
    }

    return NULL;
}

static void set_uart_command_deferred(ClientSession *session, int deferred)
{
    if (session->uart_command_deferred == deferred)
    {
        return;
    }

    semTake(g_config_mutex, WAIT_FOREVER);
    ChannelState *channel = &g_system_config.channels[session->channel_index];
    if (deferred)
    {
        channel->uart_reconfigure_waiters++;
    }
    else if (channel->uart_reconfigure_waiters > 0)
    {
        channel->uart_reconfigure_waiters--;
    }

    session->uart_command_deferred = deferred;
    semGive(g_config_mutex);
}

static int handle_serial_port_command(ClientSession *session)
{
    ChannelState *channel = &g_system_config.channels[session->channel_index];
    int consumed = 0;
    int deferred = 0;

    /* ASPP 帧由命令字、负载长度和负载组成，TCP 可以任意拆分。 */
    while (session->rx_bytes - consumed >= ASPP_HEADER_LEN)
    {
        unsigned char *frame = &session->rx_buffer[consumed];
        int frame_len = ASPP_HEADER_LEN + frame[1];
        if (session->rx_bytes - consumed < frame_len)
        {
            break;
        }

        int result = handle_command(channel, session->fd, (char *)frame, frame_len, session->channel_index);
        if (result == ERROR)
        {
            return 0;
        }
        if (result == ASPP_COMMAND_DEFERRED)
        {
            deferred = 1;
            break;
        }
        consumed += frame_len;
    }

    if (consumed > 0)
    {
        session->rx_bytes -= consumed;
        memmove(session->rx_buffer, session->rx_buffer + consumed, session->rx_bytes);
    }
    set_uart_command_deferred(session, deferred);
    return 1;
}

/**
 * @brief 处理全局设备配置指令 (CONN_TYPE_SETTING)
 * @details 从字节流中解析出带 A5A5 帧头帧尾的完整协议帧，并交给 process_command_frame 处理。
 */
static void handle_global_setting_frame(int session_index, ClientSession *session)
{
    while (session->rx_bytes >= MIN_FRAME_SIZE)
    {
        int frame_start = -1, frame_end = -1, i;

        // 1. 寻找帧头 (0xA5A5)
        for (i = 0; i <= session->rx_bytes - 2; i++)
        {
            if (session->rx_buffer[i] == HEAD_ID_B1 && session->rx_buffer[i + 1] == HEAD_ID_B2)
            {
                frame_start = i;
                break;
            }
        }

        if (frame_start == -1)
        {
            // 缓冲区中没有帧头，所有数据都可能是无效的，但为了安全，只丢弃部分
            // 这样可以防止因丢失一个字节而丢弃整个缓冲区
            session->rx_bytes = 0;
            break;
        }

        // 如果帧头不在缓冲区开头，丢弃之前的所有垃圾数据
        if (frame_start > 0)
        {
            memmove(session->rx_buffer, session->rx_buffer + frame_start, session->rx_bytes - frame_start);
            session->rx_bytes -= frame_start;
        }

        // 2. 在找到帧头后，寻找帧尾 (0x5A5A)
        // 帧尾至少在帧头后4个字节 (Cmd+Sub+End)
        if (session->rx_bytes >= MIN_FRAME_SIZE)
        {
            for (i = MIN_FRAME_SIZE - 2; i <= session->rx_bytes - 2; i++)
            {
                if (session->rx_buffer[i] == END_ID_B1 && session->rx_buffer[i + 1] == END_ID_B2)
                {
                    frame_end = i;
                    break;
                }
            }
        }

        if (frame_end != -1)
        {
            // 找到了一个完整的帧
            int frame_len = frame_end + 2;
            process_command_frame(session_index, session->rx_buffer, frame_len);
            // 从缓冲区中移除已处理的帧
            memmove(session->rx_buffer, session->rx_buffer + frame_len, session->rx_bytes - frame_len);
            session->rx_bytes -= frame_len;
        }
        else
        {
            break;
        }
    }
}

/**
 * @brief 接收数据并根据连接类型分发到不同的处理函数
 * @return 1 表示连接存活, 0 表示连接断开
 */
static int handle_config_client(int index)
{
    ClientSession *session = &s_sessions[index];

    int n = recv(session->fd, (char *)session->rx_buffer + session->rx_bytes, MAX_COMMAND_LEN - session->rx_bytes, 0);

    // 打印接收到的数据
    // LOG_DEBUG("Received %d bytes on fd %d: %s\n", n, session->fd, session->rx_buffer);

    if (n > 0)
    {
        session->rx_bytes += n;
        if (session->type == CONN_TYPE_REALCOM_CMD)
        {
            return handle_serial_port_command(session);
        }

        else if (session->type == CONN_TYPE_SETTING)
        {
            LOG_DEBUG("CONN_TYPE_SETTING");
            handle_global_setting_frame(index, session);
        }
        return 1;
    }
    else if (n == 0)
    {
        // 链接断开，关闭socket，修改连接状态
        LOG_DEBUG("ConfigTask: Client disconnected on fd %d\n", session->fd);
        //        shutdown(session->fd, SHUT_RDWR);
        //        close(session->fd);
        //        session->fd = -1;

        return 0;
    }
    else
    {
        return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR;
    }

    return 1;
}

/**
 * @brief 用于全局配置指令
 */
static void process_command_frame(int session_index, const unsigned char *frame, int len)
{
    unsigned char cmd_id = frame[2];
    switch (cmd_id)
    {
    case CMD_OVERVIEW:
        // handle_overview_request(session_index);
        break;
    case CMD_BASIC_SETTINGS:
        handle_basic_settings_request(session_index, frame, len);
        break;
    case CMD_NETWORK_SETTINGS:
        // handle_network_settings_request(session_index, frame, len);
        break;
    case CMD_SERIAL_SETTINGS:
        handle_serial_settings_request(session_index, frame, len);
        break;
    case CMD_OPERATING_SETTINGS:
        handle_operating_settings_request(session_index, frame, len);
        break;
    case CMD_MONITOR:
        handle_monitor_request(session_index, frame, len);
        break;
    case CMD_ADMIN:
        handle_change_password_request(session_index, frame, len);
        break;
    default:
        LOG_WARN("Unknown command ID: 0x%02X", cmd_id);
        break;
    }
}

/**
 * @brief 清理一个已断开的配置连接 (全局或单个串口)
 * @details 关闭socket，从会话列表中移除，并根据情况更新通道状态。
 * 特别是，当一个通道的所有网络连接（数据和命令）都断开时，
 * 它会将该通道的物理串口状态（uart_state）更新为 CLOSED。
 * * @param index 要清理的会话在 s_sessions 数组中的索引
 */
static void cleanup_config_connection(int index)
{
    int i = 0;
    if (index < 0 || index >= s_num_active_sessions)
    {
        LOG_WARN("cleanup_config_connection: Invalid index %d.", index);
        return;
    }

    ClientSession *session = &s_sessions[index];
    int fd_to_close = session->fd;

    // --- 步骤 1: 如果是特定通道的命令连接，则更新其状态 ---
    if (session->type == CONN_TYPE_REALCOM_CMD && session->channel_index >= 0)
    {
        int channel_index = session->channel_index;
        set_uart_command_deferred(session, 0);

        semTake(g_config_mutex, WAIT_FOREVER);
        ChannelState *channel = &g_system_config.channels[channel_index];

        // 在该通道的客户端数组中查找并移除 fd
        int client_found = 0;
        for (i = 0; i < channel->cmd_net_info.num_clients; i++)
        {
            if (channel->cmd_net_info.client_fds[i] == fd_to_close)
            {
                // 找到了，用最后一个元素覆盖当前位置，然后缩减数组大小
                int last_client_index = channel->cmd_net_info.num_clients - 1;
                channel->cmd_net_info.client_fds[i] = channel->cmd_net_info.client_fds[last_client_index];
                channel->cmd_net_info.client_fds[last_client_index] = -1; // 清理
                channel->cmd_net_info.num_clients--;
                client_found = 1;
                break;
            }
        }

        // 如果这是最后一个命令客户端，更新网络状态
        if (client_found && channel->cmd_net_info.num_clients == 0)
        {
            channel->cmd_net_info.state = NET_STATE_LISTENING;
            LOG_INFO("ConfigTask: Ch %d has no CMD clients left. State -> LISTENING.\n", channel_index);

            // *** 检查数据通道是否也已没有客户端 ***
            if (channel->data_net_info.num_clients == 0)
            {
                channel->uart_close_pending = (channel->uart_state == UART_STATE_OPENED);
            }
        }
        semGive(g_config_mutex);
    }

    // --- 步骤 2: 关闭 socket 文件描述符 ---
    close(fd_to_close);
    LOG_INFO("ConfigTask: Cleaned up client fd=%d.\n", fd_to_close);

    // --- 步骤 3: 从全局会话数组 s_sessions 中移除 ---
    // 通过将最后一个元素移动到当前位置来高效地删除，避免移动整个数组
    int last_index = s_num_active_sessions - 1;
    if (index != last_index)
    {
        s_sessions[index] = s_sessions[last_index];
    }
    // 清理最后一个元素的数据
    s_sessions[last_index].fd = -1;
    s_sessions[last_index].type = 0;
    s_sessions[last_index].channel_index = -1;
    s_num_active_sessions--;
}

/* 接口名只在此处维护；LAN2 沿用旧 MAC 设置的 eth2，启用前应核实硬件。 */
static const char *const s_lan_interfaces[NET_NUM] = {"eth0", "eth2"};

const char *net_cfg_interface_name(int index)
{
    if (index < 0 || index >= NET_NUM)
    {
        return NULL;
    }
    return s_lan_interfaces[index];
}

int net_cfg_parse_ipv4(const char *text, uint32_t *address)
{
    unsigned char bytes[4];
    const char *p = text;

    if (text == NULL || address == NULL)
    {
        return ERROR;
    }
    for (int i = 0; i < 4; ++i)
    {
        unsigned int value = 0;
        int digits = 0;

        while (*p >= '0' && *p <= '9')
        {
            value = value * 10 + (unsigned int)(*p++ - '0');
            if (++digits > 3 || value > 255)
            {
                return ERROR;
            }
        }
        if (digits == 0 || (i < 3 ? *p != '.' : *p != '\0'))
        {
            return ERROR;
        }
        bytes[i] = (unsigned char)value;
        if (i < 3)
        {
            ++p;
        }
    }
    memcpy(address, bytes, sizeof(bytes));
    return OK;
}

// 判断一个 IPv4 地址是否属于可用的单播地址
static int net_cfg_is_unicast(uint32_t host_ip)
{
    unsigned int first = host_ip >> 24;
    return first != 0 && first != 127 && first < 224;
}

int net_cfg_validate_static_settings(uint32_t ip, uint32_t mask, uint32_t gateway)
{
    uint32_t host_ip = ntohl(ip);
    uint32_t host_mask = ntohl(mask);
    uint32_t host_gateway = ntohl(gateway);
    uint32_t host_bits = ~host_mask;

    if (host_mask == 0 || host_bits < 3 || (host_bits & (host_bits + 1U)) != 0)
    {
        return ERROR;
    }
    if (!net_cfg_is_unicast(host_ip) || (host_ip & host_bits) == 0 || (host_ip & host_bits) == host_bits)
    {
        return ERROR;
    }
    if (host_gateway != 0 && (!net_cfg_is_unicast(host_gateway) || host_gateway == host_ip || (host_gateway & host_mask) != (host_ip & host_mask) ||
         (host_gateway & host_bits) == 0 || (host_gateway & host_bits) == host_bits))
    {
        return ERROR;
    }
    return OK;
}

/* 只在启动应用配置时执行程序，不通过 shell 拼接命令。 */
static int net_cfg_run_program(char *const arguments[])
{
    pid_t child = fork();
    int status;

    if (child < 0)
    {
        return ERROR;
    }
    if (child == 0)
    {
        execvp(arguments[0], arguments);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            return ERROR;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        LOG_ERROR("Network command failed: %s, status=%d\n", arguments[0], status);
        return ERROR;
    }
    return OK;
}

int net_cfg_set_mac_address(int index, const unsigned char mac[6])
{
    const char *interface_name = net_cfg_interface_name(index);
    char mac_text[18];
    int result;
    int up_result;

    if (index < 0 || index >= APP_LAN_COUNT || interface_name == NULL || mac == NULL)
    {
        return ERROR;
    }
    snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char *down_args[] = {"ifconfig", (char *)interface_name, "down", NULL};
    char *mac_args[] = {"ifconfig", (char *)interface_name, "hw", "ether", mac_text, NULL};
    char *up_args[] = {"ifconfig", (char *)interface_name, "up", NULL};

    if (net_cfg_run_program(down_args) != OK)
    {
        return ERROR;
    }
    result = net_cfg_run_program(mac_args);
    up_result = net_cfg_run_program(up_args);
    return result == OK && up_result == OK ? OK : ERROR;
}

int net_cfg_apply_settings(int index, const NetPortConfig *config)
{
    const char *interface_name = net_cfg_interface_name(index);
    char ip_text[INET_ADDRSTRLEN];
    char mask_text[INET_ADDRSTRLEN];
    char gateway_text[INET_ADDRSTRLEN];
    char metric_text[12];

    if (index < 0 || index >= APP_LAN_COUNT || interface_name == NULL || config == NULL ||
        config->mode != IP_MODE_STATIC ||
        net_cfg_validate_static_settings(config->ip, config->mask, config->gateway) != OK)
    {
        return ERROR;
    }
    if (inet_ntop(AF_INET, &config->ip, ip_text, sizeof(ip_text)) == NULL ||
        inet_ntop(AF_INET, &config->mask, mask_text, sizeof(mask_text)) == NULL ||
        inet_ntop(AF_INET, &config->gateway, gateway_text, sizeof(gateway_text)) == NULL)
    {
        return ERROR;
    }
    snprintf(metric_text, sizeof(metric_text), "%u", 100U + (unsigned int)index * 100U);
    char *address_args[] = {"ifconfig", (char *)interface_name, ip_text, "netmask", mask_text, "up", NULL};
    char *clear_args[] = {"ip", "route", "flush", "default", "dev", (char *)interface_name, NULL};
    char *gateway_args[] = {"ip", "route", "add", "default", "via", gateway_text, "dev", (char *)interface_name, "metric", metric_text, NULL};

    if (net_cfg_run_program(address_args) != OK || net_cfg_run_program(clear_args) != OK)
    {
        return ERROR;
    }
    if (config->gateway != 0 && net_cfg_run_program(gateway_args) != OK)
    {
        return ERROR;
    }
    LOG_INFO("LAN%d applied: interface=%s IP=%s mask=%s gateway=%s metric=%s\n",
             index + 1, interface_name, ip_text, mask_text, gateway_text, metric_text);
    return OK;
}
