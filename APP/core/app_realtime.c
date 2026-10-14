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
//#include <timers.h>     // For POSIX timers if used as fallback, or custom timer driver header
//#include <intLib.h>     // For intConnect()
#include <stdint.h>     // For uint8_t, uint32_t, etc.
#include <time.h>       // For time() function
#include<stdio.h>

//#include<timer.h>

/* ------------------ Task-Specific Constants ------------------ */
#define MEDIUM_FREQ_INTERVAL     (1)          // 中频任务执行间隔 (10 * 100µs = ms)
#define LOW_FREQ_INTERVAL        (5*1000)     // 任务执行间隔 (1000ms)

#define TX_CHUNK_SIZE (UART_HW_FIFO_SIZE) / 2

// LED每次触发后点亮的持续时间（单位：秒）
#define LED_ON_DURATION_SECONDS    (2) 

//函数声明
extern void NetworkSchedulerTask(void);
extern void check_for_new_connections(void);
extern void run_net_recv(void);
extern void run_net_send(void);

/* ------------------ Private Function Prototypes ------------------ */
//static void high_precision_timer_isr(uint32_t timer_id, void *arg);
static int setup_high_precision_timer(void);

static void handle_serial_tx_rx(void);
static void handle_led_blinking(void);
static void run_medium_frequency_tasks(void);
//static void run_low_frequency_tasks(void);

/* ------------------ Module-level static variables ------------------ */
static SEM_ID s_timer_sync_sem; // 用于定时器ISR与任务同步的二进制信号量
static volatile uint32_t timer_cnt = 0;

static uint32_t s_last_rx_count[NUM_PORTS] = {0};
static uint32_t s_last_tx_count[NUM_PORTS] = {0};
static time_t s_last_rx_led_time[NUM_PORTS] = {0};  // 存储RX LED最后活动时间戳
static time_t s_last_tx_led_time[NUM_PORTS] = {0};  // 存储TX LED最后活动时间戳

/* ------------------ Global Variable Definitions ------------------ */

void printHex(const char *buffer, size_t size);

void printHex(const char *buffer, size_t size)
{
	size_t i;
    for (i = 0; i < size; ++i)
    {
        //每16个字节换行
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
    uint8_t i=0;
    uint32_t j=0;
    const char test_data[] = {0,1,2,3,4,5,6,7,8,9};
    for (i = 0; i < NUM_PORTS; i++) {
        // Initialize UART with default settings
        if (sysUartInit(i, 115200) == 0) {
            ChannelState* channel = &g_system_config.channels[i];
            channel->uart_state = UART_STATE_OPENED;
            // send data
            channel->tx_net = 0;
            for(j=0;j<115200/100;j++){
                ring_buffer_queue_arr(&channel->buffer_net,(const char*) test_data,sizeof(test_data));
                channel->tx_net += sizeof(test_data);
            }
        } else {
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
    ts.tv_nsec = nus * 1000;  // 100us = 100000ns

    // 执行延时，被信号中断自动重试（保证延时准确）
    while (nanosleep(&ts, &ts) == -1);
}

// 用来把时间差转成 秒/毫秒/微秒/纳秒
struct timespec diff_timespec(struct timespec start, struct timespec end)
{
    struct timespec temp;
    if ((end.tv_nsec - start.tv_nsec) < 0) {
        temp.tv_sec = end.tv_sec - start.tv_sec - 1;
        temp.tv_nsec = 1000000000 + end.tv_nsec - start.tv_nsec;
    } else {
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
void* RealTimeSchedulerTask(void *arg)
{  
	LOG_INFO("RealTimeSchedulerTask: Starting...\n");

    int ret = pthread_setname_np(pthread_self(), "RealTimeTask");
    if(ret != 0)
    {
        LOG_ERROR("RealTimeSchedulerTask: set thread name failed: %s\n", strerror(ret));
        return NULL;
    }

	/* ------------------ 主调度循环 ------------------ */
	uint32_t loop_cnt = 0;
	while (1) 
	{
        check_for_new_connections();

        run_net_recv();
        /* 串口收发：每通道先 TX 再 RX，一站处理完 */
        handle_serial_tx_rx();
        run_net_send();

        /* LED 每 100 个周期执行一次，减少 time() 系统调用 */
        if (++loop_cnt >= 100) 
        {
            handle_led_blinking();
            loop_cnt = 0;
        }
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
        ChannelState* channel = &g_system_config.channels[i];
        if (channel->data_net_info.num_clients == 0 || channel->uart_state != UART_STATE_OPENED)
            continue;

        /* --- TX: buffer_net → UART --- */
        if (!ring_buffer_is_empty(&channel->buffer_net) && axi16550_TxReady(i))
        {
            uint32_t bytes_count = ring_buffer_dequeue_arr(&channel->buffer_net, (char*)temp_buffer, TX_CHUNK_SIZE);
            if (bytes_count > 0)
            {
                channel->tx_count += bytes_count;
                /* 记录 UART TX 起始时间 */
                // clock_gettime(CLOCK_MONOTONIC, &t_uart_tx);

                
        //         /*  临时添加  */
        //         LOG_DEBUG("[UART TX] Port=%d, len=%u, data=%02X, LSR=%02X, MCR=%02X\n",
        //                                     i + 1, bytes_count, temp_buffer[0],
        //                                     userAxiCfgRead(i, AXI_16550_LSR) & 0xFF,
        //                                     userAxiCfgRead(i, AXI_16550_MCR) & 0xFF);
        //         /**********/

                axi16550SendNoWait(i, temp_buffer, bytes_count);
            }
        }

        /* --- RX: UART → buffer_uart --- */
        {
            uint32_t bytes_count = 0;
            if (axi16550Recv(i, temp_buffer, &bytes_count,
                             &channel->frame_error_count,
                             &channel->parity_error_count,
                             &channel->overrun_error_count,
                             &channel->break_count) == 0 && bytes_count > 0)
            {
                channel->rx_count += bytes_count;
                ring_buffer_queue_arr(&channel->buffer_uart, (const char*)temp_buffer, bytes_count);
            }
        }
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
 * @brief 新增：处理所有通道的LED闪烁逻辑
 * @details 基于可重入单稳态触发器模型。
 * 检测到数据活动时，记录当前时间戳；超过指定时间无活动则关闭LED。
 * 使用真实时间而非周期计数，避免受动态休眠影响。
 * 添加优化：减少不必要的LED状态切换
 */
static void handle_led_blinking(void)
{
    int i;
    static uint8_t s_last_rx_led_state[NUM_PORTS] = {0};  // 记住上次LED状态
    static uint8_t s_last_tx_led_state[NUM_PORTS] = {0};  // 记住上次LED状态
    time_t current_time = time(NULL);

    for (i = 0; i < NUM_PORTS; i++)
    {
        ChannelState* channel = &g_system_config.channels[i];
        uint8_t current_rx_led_state = s_last_rx_led_state[i];
        uint8_t current_tx_led_state = s_last_tx_led_state[i];

        // --- 处理 RX LED ---
        // 1. 检测数据接收活动
        if (channel->rx_count > s_last_rx_count[i])
        {
            s_last_rx_led_time[i] = current_time;  // 记录活动时间戳
            s_last_rx_count[i] = channel->rx_count; // 更新上次的计数值
            current_rx_led_state = 1; // 设置LED为点亮状态
        }
        else if (current_time - s_last_rx_led_time[i] >= LED_ON_DURATION_SECONDS)
        {
            // 超过指定时间无接收活动，关闭LED
            current_rx_led_state = 0;
        }

        // 只在LED状态发生变化时才更新硬件状态
        if (s_last_rx_led_state[i] != current_rx_led_state) 
        {
            // printf("rxport%d:%d\n", i, current_rx_led_state);
            rxled(i, current_rx_led_state);
            s_last_rx_led_state[i] = current_rx_led_state;
        }

        // --- 处理 TX LED ---
        // 1. 检测数据发送活动
        if (channel->tx_count > s_last_tx_count[i])
        {
            s_last_tx_led_time[i] = current_time;  // 记录活动时间戳
            s_last_tx_count[i] = channel->tx_count; // 更新上次的计数值
            current_tx_led_state = 1; // 设置LED为点亮状态
        }
        else if (current_time - s_last_tx_led_time[i] >= LED_ON_DURATION_SECONDS)
        {
            // 超过指定时间无发送活动，关闭LED
            current_tx_led_state = 0;
        }

        // 只在LED状态发生变化时才更新硬件状态
        if (s_last_tx_led_state[i] != current_tx_led_state) 
        {
            // printf("txport%d:%d\n", i, current_tx_led_state);
            txled(i, current_tx_led_state);
            s_last_tx_led_state[i] = current_tx_led_state;
        }
    }
}

/**
 * @brief 执行所有中频任务
 */
static void run_medium_frequency_tasks(void)
{
    NetworkSchedulerTask();
    handle_led_blinking();
}

void channel_count_info(uint8_t channel_index)
{
    ChannelState* channel = &g_system_config.channels[channel_index];
    LOG_FATAL("[%d]:rx_count= %d, tx_count= %d", channel_index, channel->rx_count, channel->tx_count);
    LOG_FATAL("[%d]:rx_net  = %d, tx_net  = %d", channel_index, channel->rx_net,   channel->tx_net);
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
    //ChannelState* channel = &g_system_config.channels[channel_index];
    //LOG_FATAL("[%d]:buffer_uart  = %d, buffer_net  = %d", channel_index, ring_buffer_num(&channel->buffer_uart), ring_buffer_num(&channel->buffer_net));

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
    ChannelState* channel = &g_system_config.channels[channel_index];
    channel->tx_net = 0;
    channel->rx_net = 0;
    channel->rx_count = 0;
    channel->tx_count = 0;
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
	//app_start_hz(1, 10000);
	//app_register_task(high_precision_timer_isr, NULL);
	ret = OK;
	return ret;
}


void app_realtime_print(void)
{
	LOG_INFO("timer_cnt:%d \r\n",timer_cnt);
}

// 添加性能监控功能
static unsigned long long s_prev_total_rx = 0;
static unsigned long long s_prev_total_tx = 0;
static time_t s_last_print_time = 0;

void app_realtime_performance_monitor(void)
{
    time_t current_time = time(NULL);
    if (current_time - s_last_print_time >= 5) { // 每5秒打印一次
        unsigned long long current_total_rx = 0;
        unsigned long long current_total_tx = 0;
        
        // 计算总收发数据量
        for (int i = 0; i < NUM_PORTS; i++) {
            current_total_rx += g_system_config.channels[i].rx_count;
            current_total_tx += g_system_config.channels[i].tx_count;
        }
        
        // 计算5秒内的数据量
        unsigned long long rx_delta = current_total_rx - s_prev_total_rx;
        unsigned long long tx_delta = current_total_tx - s_prev_total_tx;
        
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

