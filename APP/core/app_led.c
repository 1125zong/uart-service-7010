#include "app_led.h"
#include "app_com.h"
#include "hal_axi16550.h"

#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#define LED_CHECK_INTERVAL_US 20000U
#define LED_HOLD_MS 2000ULL

enum
{
    LED_RX = 0,             // 接收灯对应下标0
    LED_TX = 1,             // 发送灯对应下标1
    LED_DIRECTION_COUNT = 2 // 一共有两个方向
};

static pthread_t s_led_thread;
static int s_led_started = 0;

/* 只保护停止标志、端口锁定灯期望状态。
 * 持锁期间不操作硬件、不休眠。
 */
static pthread_mutex_t s_led_mutex = PTHREAD_MUTEX_INITIALIZER; // 静态初始化宏，初始化互斥锁
static int s_led_stop = 0;

static unsigned char s_port_lock_valid[NUM_PORTS];
static unsigned char s_port_lock_wanted[NUM_PORTS];

static int led_stop_requested(void)
{
    int stop;

    pthread_mutex_lock(&s_led_mutex);
    stop = s_led_stop;
    pthread_mutex_unlock(&s_led_mutex);

    return stop;
}

// 获取 Linux 系统的单调时钟时间（Monotonic Clock），并将时间转换成毫秒，通过指针 time_ms 返回给调用者。
static int led_get_time_ms(uint64_t *time_ms)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return -1;

    *time_ms = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    return 0;
}

void app_led_set_port_lock(int port, int locked)
{
    if (port < 0 || port >= NUM_PORTS)
        return;

    pthread_mutex_lock(&s_led_mutex);
    s_port_lock_wanted[port] = locked ? 1U : 0U;
    s_port_lock_valid[port] = 1U;
    pthread_mutex_unlock(&s_led_mutex);
}

static void *led_task(void *arg)
{
    uint64_t last_count[NUM_PORTS][LED_DIRECTION_COUNT] = {{0}};
    uint64_t last_activity_ms[NUM_PORTS][LED_DIRECTION_COUNT] = {{0}};
    unsigned char activity_on[NUM_PORTS][LED_DIRECTION_COUNT] = {{0}};

    int activity_applied[NUM_PORTS][LED_DIRECTION_COUNT];
    int port_lock_applied[NUM_PORTS];

    int i;
    int direction;

    (void)arg;

    int name_ret = pthread_setname_np(pthread_self(), "LedTask");
    if (name_ret != 0)
    {
        LOG_WARN("Failed to name LED thread: %s\n", strerror(name_ret));
    }

    for (i = 0; i < NUM_PORTS; ++i)
    {
        activity_applied[i][LED_RX] = -1;
        activity_applied[i][LED_TX] = -1;
        port_lock_applied[i] = -1;
    }

    while (!led_stop_requested())
    {
        uint64_t counts[NUM_PORTS][LED_DIRECTION_COUNT];
        uint64_t now_ms;

        unsigned char lock_valid[NUM_PORTS];
        unsigned char lock_wanted[NUM_PORTS];

        /*
         * 只在锁内复制统计。
         * 后面的 rxled/txled/Portled 全部在释放锁后执行。
         */
        if (semTake(g_stats_mutex, WAIT_FOREVER) != OK)
        {
            usleep(LED_CHECK_INTERVAL_US);
            continue;
        }

        for (i = 0; i < NUM_PORTS; ++i)
        {
            counts[i][LED_RX] = g_system_config.channels[i].rx_total_count;
            counts[i][LED_TX] = g_system_config.channels[i].tx_total_count;
        }

        semGive(g_stats_mutex);

        if (led_get_time_ms(&now_ms) != 0)
        {
            usleep(LED_CHECK_INTERVAL_US);
            continue;
        }

        pthread_mutex_lock(&s_led_mutex);

        for (i = 0; i < NUM_PORTS; ++i)
        {
            lock_valid[i] = s_port_lock_valid[i];
            lock_wanted[i] = s_port_lock_wanted[i];
        }

        pthread_mutex_unlock(&s_led_mutex);

        for (i = 0; i < NUM_PORTS; ++i)
        {
            for (direction = 0; direction < LED_DIRECTION_COUNT; ++direction)
            {
                /* 累计计数增加，表示发生收发活动。 */
                if (counts[i][direction] > last_count[i][direction])
                {
                    last_activity_ms[i][direction] = now_ms;
                    activity_on[i][direction] = 1U;
                }

                /*
                 * 计数下降时只更新基准。
                 * 避免统计清零被误判为新的收发活动。
                 */
                last_count[i][direction] = counts[i][direction];

                if (activity_on[i][direction] && now_ms - last_activity_ms[i][direction] >= LED_HOLD_MS)
                {
                    activity_on[i][direction] = 0U;
                }

                if (activity_applied[i][direction] != (int)activity_on[i][direction])
                {
                    /*
                     * 内部包含3ms等待。
                     * 此时没有持有统计锁、配置锁或LED状态锁。
                     */
                    if (direction == LED_RX)
                    {
                        rxled(i, activity_on[i][direction]);
                    }
                    else
                    {
                        txled(i, activity_on[i][direction]);
                    }
                    activity_applied[i][direction] = activity_on[i][direction];
                }
            }

            /*
             * 与 RX/TX 灯共用触发寄存器的端口锁定灯，
             * 也由本线程串行操作。
             */
            if (lock_valid[i] && port_lock_applied[i] != (int)lock_wanted[i])
            {
                Portled(i, lock_wanted[i]);
                port_lock_applied[i] = lock_wanted[i];
            }
        }

        usleep(LED_CHECK_INTERVAL_US);
    }

    return NULL;
}

int app_led_start(void)
{
    int ret;

    /* start/stop 只允许主线程调用。 */
    if (s_led_started)
        return 0;

    pthread_mutex_lock(&s_led_mutex);
    s_led_stop = 0;
    pthread_mutex_unlock(&s_led_mutex);

    ret = pthread_create(&s_led_thread, NULL, led_task, NULL);
    if (ret != 0)
        return ret;

    s_led_started = 1;
    return 0;
}

int app_led_stop(void)
{
    int ret;

    if (!s_led_started)
    {
        return 0;
    }

    pthread_mutex_lock(&s_led_mutex);
    s_led_stop = 1;
    pthread_mutex_unlock(&s_led_mutex);

    /*
     * 不使用 pthread_cancel：
     * 让当前硬件触发操作执行完整，再退出。
     */
    ret = pthread_join(s_led_thread, NULL);
    if (ret == 0)
    {
        s_led_started = 0;
    }

    return ret;
}