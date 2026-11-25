#include "key.h"
#include <stdio.h>

#include <string.h>

// 添加UI系统相关的头文件
#include "ui_menu_system.h"
#include "my_screen_driver.h"
#include "app_data.h"
#include "ui_state_main.h"
#include "app_com.h"
#include "app_dev.h"
#include "hal_log.h"

#include <sys/reboot.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>

// 声明外部函数和变量
ui_manager_t g_ui_manager;
app_config_t g_app_data;

static volatile uint8_t last_key_state = KEY_EVENT_NONE;

// 标志变量，用于通知线程退出
static volatile int key_thread_running = 1;

static pthread_mutex_t s_reboot_page_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_reboot_page_requested = 0; // 重启请求标志
static int s_reboot_page_shown = 0;     // reboot页面绘制完成标志

#ifdef TIMER
ACoreOs_id timer_id;
Timer_ID pTimerId;
#endif

void load_app_data(void);
void timer_start(void);
void timer_init(void);
void sysReboot(void);

/* 只供管理线程调用，不要从 KeyProcessTask 内调用 */
int screen_show_reboot_wait(void)
{
    pthread_mutex_lock(&s_reboot_page_mutex);
    s_reboot_page_requested = 1;
    pthread_mutex_unlock(&s_reboot_page_mutex);

    /* 最多等待约 1 秒，不因屏幕线程异常而一直阻塞重启 */
    for (int i = 0; i < 20; i++)
    {
        pthread_mutex_lock(&s_reboot_page_mutex);
        int shown = s_reboot_page_shown;
        pthread_mutex_unlock(&s_reboot_page_mutex);

        if (shown)
            return 0;

        usleep(50000); // 每50ms检查屏幕线程是否已经画好等待页
    }

    return -1;
}

void screen_cancel_reboot_wait(void)
{
    pthread_mutex_lock(&s_reboot_page_mutex);
    s_reboot_page_requested = 0;
    pthread_mutex_unlock(&s_reboot_page_mutex);
}

/* 仅由 KeyProcessTask 调用；返回 1 时暂不处理普通按键 */
static int process_reboot_wait_page(void)
{
    pthread_mutex_lock(&s_reboot_page_mutex);
    int requested = s_reboot_page_requested;
    int shown = s_reboot_page_shown;
    pthread_mutex_unlock(&s_reboot_page_mutex);

    if (!requested)
    {
        /* 重启失败并取消请求后，恢复原来的界面 */
        if (shown)
        {
            ui_refresh(&g_ui_manager);

            pthread_mutex_lock(&s_reboot_page_mutex);
            s_reboot_page_shown = 0;
            pthread_mutex_unlock(&s_reboot_page_mutex);
        }
        return 0;
    }

    if (!shown)
    {
        const display_driver_t *display = g_ui_manager.display;

        if (!display || !display->clear || !display->draw_text)
            return 1;

        const char *text = "Wait A Moment...";
        int x = (320 - (int)strlen(text) * FONT_WIDTH) / 2;

        display->clear();
        display->draw_text((uint8_t)x, 65, text);

        if (display->refresh)
        {
            display->refresh();
        }

        pthread_mutex_lock(&s_reboot_page_mutex);
        s_reboot_page_shown = 1;
        pthread_mutex_unlock(&s_reboot_page_mutex);
    }

    return 1;
}

/**
 * @brief 从 sysfs 读取 GPIO 电平值
 * @param gpio_num GPIO 编号
 * @return 0=低电平(按下), 1=高电平(未按下), -1=读取失败
 */
static int gpio_read_value(int gpio_num)
{
    char path[64];
    char buf[4] = {0};
    int fd, ret;

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio_num);

    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return -1;
    }

    ret = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (ret <= 0)
    {
        return -1;
    }

    return (buf[0] == '0') ? 0 : 1;
}

/**
 * @brief 初始化 GPIO（export 并设置方向为输入）
 * @param gpio_num GPIO 编号
 * @return 0=成功, -1=失败
 */
static int gpio_init_input(int gpio_num)
{
    char path[64];
    char cmd[64];
    int fd;

    /* export */
    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0)
    {
        snprintf(cmd, sizeof(cmd), "%d", gpio_num);
        write(fd, cmd, strlen(cmd));
        close(fd);
    }

    /* 设置方向为输入 */
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio_num);
    fd = open(path, O_WRONLY);
    if (fd >= 0)
    {
        write(fd, "in", 2);
        close(fd);
        return 0;
    }

    return -1;
}

/* 7020_mio13对应GPIO引脚编号为493 对应复位按键 */
#define RESET_GPIO_NUM 493
static int reset_gpio_ready = 0;

/**
 * @brief 读取复位按键状态 (GPIO 919)
 * @return 0=按下, 1=未按下
 */
int read_reset_key(void)
{
    if (!reset_gpio_ready)
    {
        if (gpio_init_input(RESET_GPIO_NUM) == 0)
        {
            reset_gpio_ready = 1;
        }
        else
        {
            return 1; /* 初始化失败，返回未按下 */
        }
    }

    int val = gpio_read_value(RESET_GPIO_NUM);
    return (val < 0) ? 1 : val;
}

// ---获得按键值 ---
key_event_t my_keypad_get_event(void)
{
    int pressed_count = 0;
    key_event_t pressed_key = KEY_EVENT_NONE;

    // 检查各个按键状态，0表示按下，1表示未按下
    if (KEY_MENU_ADDR == 0)
    {
        // printf("KEY_MENU_ADDR!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_MENU;
    }

    if (KEY_UP_ADDR == 0)
    {
        // printf("KEY_UP_ADDR!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_UP;
    }

    if (KEY_DOWN_ADDR == 0)
    {
        // printf("KEY_DOWN_ADDR!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_DOWN;
    }

    if (KEY_CONFIRM_ADDR == 0)
    {
        // printf("KEY_CONFIRM_ADDR!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_CONFIRM;
    }

#define RS422

#ifdef RS422
    if (read_reset_key() == 0)
    {
        // printf("KEY_RESET (GPIO)!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_RESET;
    }

#else

    if (KEY_RESET == 0)
    {
        // printf("KEY_RESET!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_RESET;
    }

#endif

    // 按键去抖和处理单次触发
    if (pressed_key != last_key_state)
    {
        last_key_state = pressed_key;
        if (pressed_key != KEY_EVENT_NONE)
        {
            // 新按键按下，返回按键事件
            return pressed_key;
        }
        else
        {
            // 按键释放，不返回事件
            return KEY_EVENT_NONE;
        }
    }

    // 按键状态未变化，不返回事件(防止重复触发)
    return KEY_EVENT_NONE;
}

// 用于检测按键状态变化的辅助函数
static key_event_t check_key_state_change(void)
{
    static volatile uint8_t prev_key_state = KEY_EVENT_NONE;
    key_event_t event = my_keypad_get_event();

    // 检测按键状态变化
    if (event != prev_key_state)
    {
        prev_key_state = event;
        return event;
    }

    return KEY_EVENT_NONE;
}

// 停止按键处理任务的函数
void stop_key_process_task(void)
{
    key_thread_running = 0;
}

/**
 * @brief 按键处理任务
 * @details 该任务负责检测硬件按键状态并将其传递给UI系统
 */

void *KeyProcessTask(void *arg)
{
    int id = *(int *)arg;
    LOG_INFO("Key processing task started, id=%d\n", id);

    int ret = pthread_setname_np(pthread_self(), "KeyProcessTask");
    if (ret != 0)
    {
        LOG_ERROR("KeyProcessTask: pthread_setname_np failed: %s\n", strerror(ret));
        return NULL;
    }

    // 初始化UI系统
    load_app_data();

    // 初始化UI管理器
    ui_init(&g_ui_manager,          // UI管理器实例
            get_my_screen_driver(), // 屏幕驱动
            &g_app_data,            // 应用数据
            &g_state_main           // 初始状态
    );

    // 主循环 - 优化的休眠机制
    while (key_thread_running)
    {
        if (process_reboot_wait_page())
        {
            usleep(100000);
            continue;
        }

        // 获取按键事件
        key_event_t event = check_key_state_change();

        // 如果有按键事件，传递给UI系统
        if (event != KEY_EVENT_NONE)
        {
            // printf("event: %d   last_key_state: %d\n", event, last_key_state);
            ui_handle_input(&g_ui_manager, event);
        }
        if (event == KEY_EVENT_RESET)
        {
            // printf("event == KEY_EVENT_RESET!\n");

            // 等待2秒后重新检测按键状态
            usleep(1000 * 2000);

            // 直接读取 KEY_RESET 硬件状态（绕过状态机）
            // 按键仍被按住则执行重启
#ifdef RS422
            if (read_reset_key() == 0)
#else
            if (KEY_RESET == 0)
#endif
            {
                // printf("KEY_RESET still pressed after 2s, rebooting...\n");
                int ret = set_restore_flag();
                if (ret < 0)
                {
                    LOG_ERROR("  ERROR: Failed to write restore flag to flash\n");
                    continue;
                }
                sysReboot();
            }
            else
            {
                // printf("KEY_RESET released, no reboot\n");
            }
        }

        // 使用更长的休眠时间来降低CPU使用率，但保持合理的响应速度
        usleep(100000); // 休眠100毫秒
    }

    return NULL;
}

// 3. 填充您的应用数据 (示例)
void load_app_data(void)
{
    // printf("MAIN: App data loaded.\n");

    LOG_INFO("Model Name: %s", g_system_config.device.model_name);
    LOG_INFO("Device Name: %s", g_system_config.device.device_name);

    // --- 通用设置 ---
    strcpy(g_app_data.model_name, g_system_config.device.model_name);
    strcpy(g_app_data.model, g_system_config.device.device_name); //

    // --- 状态 ---
    uint8_t current_status = g_system_config.device.lock_status;
    g_app_data.current_status = current_status; // e.g., Start in Lock state
    g_app_data.error_code = 1;                  // 仅在 STATUS_ERROR 时显示

    // printf("[current_status]: %d\n", current_status);

    // [Device Info]
    snprintf(g_app_data.firmware_version, sizeof(g_app_data.firmware_version), "V%d.%d.%d.%d",
             g_system_config.device.firmware_version[0], g_system_config.device.firmware_version[1],
             g_system_config.device.firmware_version[2], g_system_config.device.firmware_version[3]);
    snprintf(g_app_data.serial_number, sizeof(g_app_data.serial_number), "%04d%02d%02d%06d",
             g_system_config.device.serial_no.year, g_system_config.device.serial_no.month,
             g_system_config.device.serial_no.day, g_system_config.device.serial_no.number);

    // [Lock Password]
    strcpy(g_app_data.lock_password, g_system_config.device.lock_password);

    // --- 网络数据 ---
    // --- LAN 1 (索引 0) ---
    strcpy(g_app_data.lan_ports[0].ethernet_status, (g_system_config.device.lan_state1 == 1) ? "100M/Link" : "No Link");
    snprintf(g_app_data.lan_ports[0].mac_address, sizeof(g_app_data.lan_ports[0].mac_address),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             g_system_config.device.mac_address1[0], g_system_config.device.mac_address1[1],
             g_system_config.device.mac_address1[2], g_system_config.device.mac_address1[3],
             g_system_config.device.mac_address1[4], g_system_config.device.mac_address1[5]);
    snprintf(g_app_data.lan_ports[0].ip_address, sizeof(g_app_data.lan_ports[0].ip_address),
             "%d.%d.%d.%d",
             g_system_config.device.ip_address1 & 0xFF,
             (g_system_config.device.ip_address1 >> 8) & 0xFF,
             (g_system_config.device.ip_address1 >> 16) & 0xFF,
             (g_system_config.device.ip_address1 >> 24) & 0xFF);

    snprintf(g_app_data.lan_ports[0].network_mask, sizeof(g_app_data.lan_ports[0].network_mask),
             "%d.%d.%d.%d",
             g_system_config.device.netmask[0] & 0xFF,
             (g_system_config.device.netmask[0] >> 8) & 0xFF,
             (g_system_config.device.netmask[0] >> 16) & 0xFF,
             (g_system_config.device.netmask[0] >> 24) & 0xFF);

    snprintf(g_app_data.lan_ports[0].gateway, sizeof(g_app_data.lan_ports[0].gateway),
             "%d.%d.%d.%d",
             g_system_config.device.gateway[0] & 0xFF,
             (g_system_config.device.gateway[0] >> 8) & 0xFF,
             (g_system_config.device.gateway[0] >> 16) & 0xFF,
             (g_system_config.device.gateway[0] >> 24) & 0xFF);
    g_app_data.lan_ports[0].ip_config = (g_system_config.device.ip_config_mode[0] == 0x01) ? IP_CONFIG_STATIC : IP_CONFIG_DHCP; // [修改]

    // --- LAN 2 (索引 1) ---
    strcpy(g_app_data.lan_ports[1].ethernet_status, (g_system_config.device.lan_state2 == 1) ? "100M/Link" : "No Link");
    snprintf(g_app_data.lan_ports[1].mac_address, sizeof(g_app_data.lan_ports[1].mac_address),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             g_system_config.device.mac_address2[0], g_system_config.device.mac_address2[1],
             g_system_config.device.mac_address2[2], g_system_config.device.mac_address2[3],
             g_system_config.device.mac_address2[4], g_system_config.device.mac_address2[5]);
    snprintf(g_app_data.lan_ports[1].ip_address, sizeof(g_app_data.lan_ports[1].ip_address),
             "%d.%d.%d.%d",
             g_system_config.device.ip_address2 & 0xFF,
             (g_system_config.device.ip_address2 >> 8) & 0xFF,
             (g_system_config.device.ip_address2 >> 16) & 0xFF,
             (g_system_config.device.ip_address2 >> 24) & 0xFF);

    snprintf(g_app_data.lan_ports[1].network_mask, sizeof(g_app_data.lan_ports[1].network_mask),
             "%d.%d.%d.%d",
             g_system_config.device.netmask[1] & 0xFF,
             (g_system_config.device.netmask[1] >> 8) & 0xFF,
             (g_system_config.device.netmask[1] >> 16) & 0xFF,
             (g_system_config.device.netmask[1] >> 24) & 0xFF);
    snprintf(g_app_data.lan_ports[1].gateway, sizeof(g_app_data.lan_ports[1].gateway),
             "%d.%d.%d.%d",
             g_system_config.device.gateway[1] & 0xFF,
             (g_system_config.device.gateway[1] >> 8) & 0xFF,
             (g_system_config.device.gateway[1] >> 16) & 0xFF,
             (g_system_config.device.gateway[1] >> 24) & 0xFF);
    g_app_data.lan_ports[1].ip_config = (g_system_config.device.ip_config_mode[1] == 0x01) ? IP_CONFIG_STATIC : IP_CONFIG_DHCP; // [修改]
}

void sysReboot(void)
{
    dev_reboot_from_key();
}

/**********************************TIMER*******************************************************/

#ifdef TIMER

void timer_service_func(ACoreOs_id timer_id, void *arg)
{
    ACoreOs_timer_cancel(pTimerId);
}
// 消抖

void timer_init(void)
{
    ACoreOs_timer_create((ACoreOs_name) "timer1", &timer_id);
    printf("timer id: %x\n", *((UINT32 *)timer_id));

    ACoreOs_timer_get_id((ACoreOs_name) "timer1", &pTimerId);

    printf("pTimerId id: %x\n", *((UINT32 *)pTimerId));
}

void timer_start(void)
{
    ACoreOs_timer_fire_after(pTimerId, 100 * 50, timer_service_func, NULL, ACOREOS_TIMER_FOREVER, 1);
}

void timer_test(void)

{
    timer_init();
}

#endif
/**********************************TIMER*******************************************************/
