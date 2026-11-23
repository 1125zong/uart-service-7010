/*
 * =====================================================================================
 *
 * Filename:  RealTimeSchedulerTask.c
 *
 * Description:  实现 RealTimeSchedulerTask 任务。
 * 这是系统的硬实时核心，由高精度硬件定时器驱动，
 * 负责所有数据通路的高速、非阻塞I/O操作。
 *
 * =====================================================================================
 */
#include "app_com.h"
#include "screen_uart.h"
// #include <timers.h>     // For POSIX timers if used as fallback, or custom timer driver header
// #include <intLib.h>     // For intConnect()
#include <stdint.h> // For uint8_t, uint32_t, etc.
#include <time.h>   // For time() function
#include <stdio.h>

// #include<timer.h>

/* ------------------ Task-Specific Constants ------------------ */
#define MEDIUM_FREQ_INTERVAL (1)     // 中频任务执行间隔 (10 * 100µs = ms)
#define LOW_FREQ_INTERVAL (5 * 1000) // 任务执行间隔 (1000ms)

#define TX_CHUNK_SIZE (UART_HW_FIFO_SIZE) / 2

// 函数声明
extern void NetworkSchedulerTask(void);
extern void check_for_new_connections(void);
extern void run_net_recv(void);
extern void run_net_send(void);

/* ------------------ Private Function Prototypes ------------------ */
// static void high_precision_timer_isr(uint32_t timer_id, void *arg);
static int setup_high_precision_timer(void);

static void handle_serial_tx_rx(void);
static void run_medium_frequency_tasks(void);
// static void run_low_frequency_tasks(void);

/* ------------------ Module-level static variables ------------------ */
static SEM_ID s_timer_sync_sem; // 用于定时器ISR与任务同步的二进制信号量
static volatile uint32_t timer_cnt = 0;

/* ------------------ Global Variable Definitions ------------------ */

void printHex(const char *buffer, size_t size);

void printHex(const char *buffer, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i)
    {
        // 每16个字节换行
        if (i > 0 && i % 16 == 0)
        {
            printf("\r\n");
        }
        printf("%02x ", (uint8_t)buffer[i]);
    }
    printf("\r\n");
}

#if 0
void high_precision_timer_isr(uint32_t timer_id, void *arg)
{
	// ISR中唯一要做的事：释放信号量唤醒实时任务
    if(timer_id == 0)
    {
        LOG_INFO("high_precision_timer_isr\n");
    }
    if(arg != NULL)
    {
        LOG_INFO("high_precision_timer_isr arg != NULL\n");
    }

    //打印定时器时间到
    //LOG_INFO("Timer interrupt %d\n", timer_cnt);
	//logMsg("====high_precision_timer_isr!!!!\r\n",1,2,3,4,5,6);
	timer_cnt++;
	semGive(s_timer_sync_sem);
    //run_high_frequency_tasks();
}
#endif
void uart_test()
{
    uint8_t i = 0;
    uint32_t j = 0;
    const char test_data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (i = 0; i < NUM_PORTS; i++)
    {
        // Initialize UART with default settings
        if (sysUartInit(i, 115200) == 0)
        {
            ChannelState *channel = &g_system_config.channels[i];
            channel->uart_state = UART_STATE_OPENED;
            // send data
            channel->tx_net = 0;
            for (j = 0; j < 115200 / 100; j++)
            {
                ring_buffer_queue_arr(&channel->buffer_net, (const char *)test_data, sizeof(test_data));
                channel->tx_net += sizeof(test_data);
            }
        }
        else
        {
            printf("Failed to initialize UART %d\n", i);
        }
    }
    printf("uart_test end...\n");
}
#include <time.h>

// n毫秒 高精度延时函数（Linux 标准）
void delay_nus(unsigned int nus)
{
    struct timespec ts;
    // 100 微秒 = 0 秒 + 100000 纳秒
    ts.tv_sec = 0;
    ts.tv_nsec = nus * 1000; // 100us = 100000ns

    // 执行延时，被信号中断自动重试（保证延时准确）
    while (nanosleep(&ts, &ts) == -1)
        ;
}

// 用来把时间差转成 秒/毫秒/微秒/纳秒
struct timespec diff_timespec(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0)
    {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    }
    else
    {
        temp.tv_sec = end.tv_sec - start.tv_sec;
        temp.tv_nsec = end.tv_nsec - start.tv_nsec;
    }
    return temp;
}
struct timespec start, end, diff;
////////////////////////////////////////////////////////////////////////////////////

/**
 * @brief RealTimeSchedulerTask 的主入口函数
 */
void *RealTimeSchedulerTask(void *arg)
{
    LOG_INFO("RealTimeSchedulerTask: Starting...\n");

    int ret = pthread_setname_np(pthread_self(), "RealTimeTask");
    if (ret != 0)
    {
        LOG_ERROR("RealTimeSchedulerTask: set thread name failed: %s\n", strerror(ret));
        return NULL;
    }

    /* ------------------ 主调度循环 ------------------ */
    while (1)
    {
        /* 先回收已就绪的旧连接，再登记重连，避免旧连接占着名额。 */
        run_net_recv();
        check_for_new_connections();
        /* 串口收发：每通道先 TX 再 RX，一站处理完 */
        handle_serial_tx_rx();
        run_net_send();
    }

    return NULL;
}

/**
 * @brief 串口收发合并处理 (每通道 TX→RX 一站处理)
 * @details 每个通道先发后收。RX 紧跟在 TX 之后，环回数据较大概率在同周期内
 * 被读到（高波特率时），消除跨周期等待。
 */
static unsigned char temp_buffer[2048];

static void handle_serial_tx_rx(void)
{
    int i;
    for (i = 0; i < NUM_PORTS; i++)
    {
        ChannelState *channel = &g_system_config.channels[i];
        if (semTake(g_config_mutex, NO_WAIT) != OK)
            continue;
        if (channel->uart_state != UART_STATE_OPENED)
        {
            semGive(g_config_mutex);
            continue;
        }

        /* --- TX: buffer_net → UART --- */
        uint32_t bytes_count = 0;
        int tx_ok = 0;

        {
            int is_rs485 = channel->interface_type == INTERFACE_TYPE_RS485_2WIRE || channel->interface_type == INTERFACE_TYPE_RS485_4WIRE;

            /*
             * 有待发数据且 UART 可以接收下一批数据时，
             * 再开启发送方向。
             */
            if (!ring_buffer_is_empty(&channel->buffer_net) && axi16550_TxReady(i))
            {
                int tx_direction_ready = 0;

                if (is_rs485)
                {
                    if (!channel->rs485_tx_active)
                    {
                        UART_TYPE tx_mode;

                        if (channel->interface_type == INTERFACE_TYPE_RS485_2WIRE)
                        {
                            tx_mode = UART_MODE_RS485_TX;
                        }
                        else
                        {
                            tx_mode = UART_MODE_RS485;
                        }

                        if (axi16550TypeSet(tx_mode, i))
                        {
                            channel->rs485_tx_active = 1;
                        }
                    }

                    tx_direction_ready = channel->rs485_tx_active;
                }
                else if (channel->interface_type == INTERFACE_TYPE_RS232 || channel->interface_type == INTERFACE_TYPE_RS422)
                {
                    tx_direction_ready = 1;
                }
                /* 方向切换成功后，才取出数据并写入 UART */
                if (tx_direction_ready)
                {
                    bytes_count = ring_buffer_dequeue_arr(&channel->buffer_net, (char *)temp_buffer, TX_CHUNK_SIZE);

                    if (bytes_count > 0)
                    {
                        tx_ok = (axi16550SendNoWait(i, temp_buffer, bytes_count) == 0);
                    }
                }
            }

            /*
             * 本轮未写入新数据、软件队列为空，
             * 且 UART FIFO/移位寄存器全部为空时，恢复 RX。
             */
            if (is_rs485 && channel->rs485_tx_active && !tx_ok && ring_buffer_is_empty(&channel->buffer_net) && axi16550_Tx_IDLE(i))
            {
                /* 两线、四线恢复 RX 都写 0x00 */
                if (axi16550TypeSet(UART_MODE_RS485_RX, i))
                {
                    channel->rs485_tx_active = 0;
                }
            }
        }

        if (tx_ok)
        {
            /*      临时添加      */
            // printf("[UART%d TX] len=%u\n", i + 1, (unsigned int)bytes_count);
            // printHex((const char *)temp_buffer, bytes_count);
            // fflush(stdout);
            /********************** */

            semTake(g_stats_mutex, WAIT_FOREVER);
            channel->tx_count += bytes_count;
            channel->tx_total_count += bytes_count;
            semGive(g_stats_mutex);
        }

        /* --- RX: UART → buffer_uart --- */
        {
            uint32_t bytes_count = 0;
            if (axi16550RecvBounded(i, temp_buffer, sizeof(temp_buffer), &bytes_count,
                                    &channel->frame_error_count,
                                    &channel->parity_error_count,
                                    &channel->overrun_error_count,
                                    &channel->break_count) == 0 &&
                bytes_count > 0)
            {
                /*      临时添加      */
                // printf("[UART%d RX] len=%u\n", i + 1, (unsigned int)bytes_count);
                // printHex((const char *)temp_buffer, bytes_count);
                // fflush(stdout);
                /********************** */

                semTake(g_stats_mutex, WAIT_FOREVER);
                channel->rx_count += bytes_count;
                channel->rx_total_count += bytes_count;
                semGive(g_stats_mutex);
                if (channel->data_net_info.num_clients > 0)
                {
                    ring_buffer_queue_arr(&channel->buffer_uart, (const char *)temp_buffer, bytes_count);
                }
            }
        }

        /*
         * 最后一个数据客户端断开后，继续排空已经进入 buffer_net 的数据。
         * 只有软件队列为空且 UART FIFO/移位寄存器均为空时，才关闭 UART。
         */
        if (channel->uart_close_pending &&
            channel->data_net_info.num_clients == 0 &&
            channel->uart_reconfigure_waiters == 0 &&
            ring_buffer_is_empty(&channel->buffer_net) &&
            axi16550_Tx_IDLE(i) &&
            !channel->rs485_tx_active)
        {
            channel->uart_state = UART_STATE_CLOSED;
            channel->uart_close_pending = 0;
            ring_buffer_reset(&channel->buffer_uart);
            LOG_INFO("RealTimeScheduler: Ch %d UART TX drained. UART closed.\n", i);
        }
        semGive(g_config_mutex);
    }
}

/**
 * @brief 执行所有高频（每个周期）任务
 * @details 负责在环形缓冲区和串口之间高速交换数据。
 * 先 RX 再 TX: 先读回上一轮发出的数据，再发新数据。
 * 减少串口数据从到达至被处理的延迟。
 */
#if 0
static void run_high_frequency_tasks(void)
{      
    handle_serial_rx();   /* 先读回上一轮 TX 的内环数据 */
    handle_serial_tx();   /* 再发新数据(非阻塞) */   
}
#endif

/**
 * @brief 执行所有中频任务
 */
static void run_medium_frequency_tasks(void)
{
    NetworkSchedulerTask();
}

void channel_count_info(uint8_t channel_index)
{
    ChannelState *channel = &g_system_config.channels[channel_index];
    LOG_FATAL("[%d]:rx_count= %d, tx_count= %d", channel_index, channel->rx_count, channel->tx_count);
    LOG_FATAL("[%d]:rx_net  = %d, tx_net  = %d", channel_index, channel->rx_net, channel->tx_net);
}

void channel_count_info_all(void)
{
    int i;
    for (i = 0; i < NUM_PORTS; i++)
    {
        channel_count_info(i);
    }
}

void channel_buffer_cnt(uint8_t channel_index)
{
    // ChannelState* channel = &g_system_config.channels[channel_index];
    // LOG_FATAL("[%d]:buffer_uart  = %d, buffer_net  = %d", channel_index, ring_buffer_num(&channel->buffer_uart), ring_buffer_num(&channel->buffer_net));

    LOG_FATAL("[%d]\n", channel_index);
}

void channel_buffer_cnt_all(void)
{
    int i;
    for (i = 0; i < NUM_PORTS; i++)
    {
        channel_buffer_cnt(i);
    }
}

void channel_count_clr(uint8_t channel_index)
{
    ChannelState *channel = &g_system_config.channels[channel_index];
    semTake(g_stats_mutex, WAIT_FOREVER);
    channel->tx_net = 0;
    channel->rx_net = 0;
    channel->rx_count = 0;
    channel->tx_count = 0;
    semGive(g_stats_mutex);
}

void channel_count_clr_all(void)
{
    int i;
    for (i = 0; i < NUM_PORTS; i++)
    {
        channel_count_clr(i);
    }
}

#if 0
void run_low_frequency_tasks(void) 
{
    //int i;
    // for (i = 0; i < NUM_PORTS; i++) {
    //     

    //     // 只处理有客户端连接且串口已打开的通道
    //     //if (channel->data_net_info.num_clients > 0 || channel->uart_state == UART_STATE_OPENED) 
    //     if(channel->uart_state == UART_STATE_OPENED)
    //     {
    //         LOG_FATAL("[%d]:rx_count= %d, tx_count= %d", i, channel->rx_count, channel->tx_count);
    //         LOG_FATAL("[%d]:rx_net  = %d, tx_net  = %d", i, channel->rx_net,   channel->tx_net);
    //     }
    // }
    channel_count_info(0);
}

#endif

/**
 * @brief 设置并启动高精度硬件定时器
 */
static int setup_high_precision_timer(void)
{
    int ret = 0;
    // app_start_hz(1, 10000);
    // app_register_task(high_precision_timer_isr, NULL);
    ret = OK;
    return ret;
}

void app_realtime_print(void)
{
    LOG_INFO("timer_cnt:%d \r\n", timer_cnt);
}

// 添加性能监控功能
static unsigned long long s_prev_total_rx = 0;
static unsigned long long s_prev_total_tx = 0;
static time_t s_last_print_time = 0;

void app_realtime_performance_monitor(void)
{
    time_t current_time = time(NULL);
    if (current_time - s_last_print_time >= 5)
    { // 每5秒打印一次
        unsigned long long current_total_rx = 0;
        unsigned long long current_total_tx = 0;

        // 计算总收发数据量
        semTake(g_stats_mutex, WAIT_FOREVER);
        for (int i = 0; i < NUM_PORTS; i++)
        {
            current_total_rx += g_system_config.channels[i].rx_total_count;
            current_total_tx += g_system_config.channels[i].tx_total_count;
        }
        semGive(g_stats_mutex);

        // 计算5秒内的数据量
        unsigned long long rx_delta = current_total_rx >= s_prev_total_rx
                                          ? current_total_rx - s_prev_total_rx
                                          : 0;
        unsigned long long tx_delta = current_total_tx >= s_prev_total_tx
                                          ? current_total_tx - s_prev_total_tx
                                          : 0;

        // 计算平均速率
        double rx_rate = (double)rx_delta / 5.0;
        double tx_rate = (double)tx_delta / 5.0;

        LOG_INFO("Performance: RX: %.2f B/s, TX: %.2f B/s, Total Active Ports: %d\r\n",
                 rx_rate, tx_rate, NUM_PORTS);

        s_prev_total_rx = current_total_rx;
        s_prev_total_tx = current_total_tx;
        s_last_print_time = current_time;
    }
}
