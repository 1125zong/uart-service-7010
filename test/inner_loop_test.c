/*
 * =====================================================================================
 *
 * Filename:  inner_loop_test.c
 *
 * Description:  串口内环延迟测试工具。
 * 适用于目标设备已配置为内环(Loopback)模式的场景，
 * 数据写入串口TX后经内环直接从RX返回。
 *
 * 功能:
 *   - 支持可配置的波特率 (9600 ~ 1000000)
 *   - 支持可配置的单次发送字节数
 *   - 支持定次/定时两种测试模式
 *   - 数据校验 (逐字节比对)
 *   - 超时处理和多轮测试统计
 *   - 输出: 最小/最大/平均延迟 + P95/P99 百分位 + 标准差
 *
 * 用法:
 *   ./inner_loop_test [-d /dev/ttyw00] [-b 115200] [-s 64] [-t 1000] ... [-D 0]
 *
 * 编译:
 *   gcc -o inner_loop_test inner_loop_test.c -lm -lpthread
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <pthread.h>

/* ======================== 默认参数 ======================== */
#define DEFAULT_DEVICE     "/dev/ttyw00"
#define DEFAULT_BAUDRATE   115200
#define DEFAULT_PACKET_SIZE 64
#define DEFAULT_TEST_COUNT 1000          /* 定次模式: 默认1000次 */
#define DEFAULT_TEST_TIME  0             /* 定时模式: 0=禁用, 单位秒 */
#define DEFAULT_TIMEOUT_MS 2000          /* 单次接收超时(毫秒) */
#define MAX_PACKET_SIZE    4096
#define MAX_BAUD_VALUES    20

/* ======================== 波特率表 ======================== */
typedef struct {
    int         value;    /* 数值, 如 115200 */
    speed_t     speed;    /* termios 宏, 如 B115200 */
} BaudEntry;

static const BaudEntry s_baud_table[] = {
    {    9600,   B9600 },
    {   19200,  B19200 },
    {   38400,  B38400 },
    {   57600,  B57600 },
    {  115200, B115200 },
    {  230400, B230400 },
    {  460800, B460800 },
    {  500000, B500000 },
    {  576000, B576000 },
    {  921600, B921600 },
    { 1000000, B1000000 },
    {       0, B0      },  /* 结束标记 */
};

/* ======================== 全局状态 ======================== */
static volatile int g_running = 1;   /* Ctrl+C 时置0 */

/* ======================== 信号处理 ======================== */
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ======================== 工具函数 ======================== */

/** 时间差(微秒) */
static long long time_diff_us(const struct timespec *start, const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000000LL
         + (end->tv_nsec - start->tv_nsec) / 1000;
}

/** 时间差(纳秒) */
static long long time_diff_ns(const struct timespec *start, const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000000000LL
         + (end->tv_nsec - start->tv_nsec);
}

/** 查找波特率对应的 speed_t */
static speed_t lookup_baud(int baud_val)
{
    for (int i = 0; s_baud_table[i].value > 0; i++)
    {
        if (s_baud_table[i].value == baud_val)
            return s_baud_table[i].speed;
    }
    return B0;
}

/** 填充测试数据: 递增模式 + 校验和 */
static void fill_test_data(unsigned char *buf, int len)
{
    for (int i = 0; i < len - 1; i++)
        buf[i] = 0x55 + (i & 0x7F);

    /* 最后一字节为校验和(前面所有字节求和) */
    unsigned char sum = 0;
    for (int i = 0; i < len - 1; i++)
        sum += buf[i];
    buf[len - 1] = sum;
}

/** 验证接收数据, 返回 0=正确 / -1=错误 */
static int verify_data(const unsigned char *tx, const unsigned char *rx, int len)
{
    /* 逐字节比对 */
    for (int i = 0; i < len - 1; i++)
    {
        if (rx[i] != tx[i])
            return -1;
    }
    /* 校验和 */
    unsigned char sum = 0;
    for (int i = 0; i < len - 1; i++)
        sum += rx[i];
    if (rx[len - 1] != sum)
        return -2;
    return 0;
}

/* ======================== 串口操作 ======================== */

static int open_serial(const char *dev, int baud_val, int packet_size, int timeout_ms)
{
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        fprintf(stderr, "打开串口 %s 失败: %s\n", dev, strerror(errno));
        return -1;
    }

    speed_t baud = lookup_baud(baud_val);
    if (baud == B0)
    {
        fprintf(stderr, "不支持的波特率: %d\n", baud_val);
        close(fd);
        return -1;
    }

    struct termios opt;
    memset(&opt, 0, sizeof(opt));

    if (tcgetattr(fd, &opt) < 0)
    {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    cfsetispeed(&opt, baud);
    cfsetospeed(&opt, baud);

    /* 8N1, 原始模式, 无流控 */
    opt.c_cflag |=  (CLOCAL | CREAD);
    opt.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    opt.c_cflag |=  CS8;
    opt.c_cflag &= ~CRTSCTS;

    opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    opt.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL | IGNCR);
    opt.c_oflag &= ~OPOST;

    /*
     * VMIN / VTIME 配合:
     *   阻塞模式: VMIN=packet_size, VTIME=0 → 读满才返回
     *   超时模式: VMIN=0, VTIME=timeout_ms*10 → 等数据或超时
     */
    // opt.c_cc[VMIN]  = packet_size;
    // opt.c_cc[VTIME] = 0;   /* 不设超时, 靠 select 实现外部超时 */


    opt.c_cc[VMIN]  = 1;
    opt.c_cc[VTIME] = 0;   /* 不设超时, 靠 select 实现外部超时 */

    if (tcsetattr(fd, TCSANOW, &opt) < 0)
    {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

/**
 * 带超时的串口 read (毫秒)
 * VMIN=1 模式下, 有数据就立即返回, 然后循环收满 need_len 字节。
 * select 使用短超时(10ms)轮询, 避免 Delayed ACK 造成的 40ms 阻塞。
 * @return 实际读取的字节数, -1=超时或错误
 */
static int read_with_timeout(int fd, unsigned char *buf, int need_len, int timeout_ms)
{
    fd_set rfds;
    int total_read = 0;
    struct timespec t_start, t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    while (total_read < need_len)
    {
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        long long elapsed_ms = (t_now.tv_sec - t_start.tv_sec) * 1000LL
                             + (t_now.tv_nsec - t_start.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms)
            return -1;  /* 总超时 */

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 10000;  /* 10ms 短超时轮询 */

        int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0)
            return -2;       /* select 错误 */

        if (ret == 0)
            continue;        /* 超时无数据, 继续等 */

        ssize_t n = read(fd, buf + total_read, need_len - total_read);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -3;       /* read 错误 */
        }
        if (n == 0)
            return -4;       /* 设备关闭 */

        total_read += n;
    }
    return total_read;
}

/* ======================== 统计计算 ======================== */

typedef struct {
    long long *values;
    int        count;
    int        capacity;
} LatencyArray;

static void latency_init(LatencyArray *a, int capacity)
{
    a->values   = malloc(capacity * sizeof(long long));
    a->count    = 0;
    a->capacity = capacity;
}

static void latency_add(LatencyArray *a, long long val)
{
    if (a->count >= a->capacity)
    {
        a->capacity += a->capacity / 2;
        a->values = realloc(a->values, a->capacity * sizeof(long long));
    }
    a->values[a->count++] = val;
}

static void latency_free(LatencyArray *a)
{
    free(a->values);
    a->values   = NULL;
    a->count    = 0;
    a->capacity = 0;
}

static int cmp_ll(const void *a, const void *b)
{
    long long va = *(const long long *)a;
    long long vb = *(const long long *)b;
    return (va > vb) - (va < vb);
}

static void latency_report(LatencyArray *a, int baud, int pkt_size, int total_tries,
                            int timeout_cnt, int error_cnt)
{
    printf("\n");
    printf("============================================================\n");
    printf("  内环延迟测试报告\n");
    printf("------------------------------------------------------------\n");
    printf("  波特率:        %d bps\n", baud);
    printf("  包大小:        %d 字节\n", pkt_size);
    printf("  总尝试:        %d\n", total_tries);
    printf("  成功:          %d\n", a->count);
    printf("  超时:          %d\n", timeout_cnt);
    printf("  数据错误:      %d\n", error_cnt);
    printf("  成功率:        %.2f%%\n",
           100.0 * a->count / (total_tries > 0 ? total_tries : 1));

    if (a->count == 0)
    {
        printf("------------------------------------------------------------\n");
        printf("  (无有效数据)\n");
        printf("============================================================\n");
        return;
    }

    /* 排序 */
    qsort(a->values, a->count, sizeof(long long), cmp_ll);

    long long min   = a->values[0];
    long long max   = a->values[a->count - 1];
    double    sum   = 0;
    double    sum2  = 0;

    for (int i = 0; i < a->count; i++)
    {
        double v = (double)a->values[i];
        sum  += v;
        sum2 += v * v;
    }

    double avg  = sum / a->count;
    double variance = (sum2 / a->count) - (avg * avg);
    double stddev = sqrt(variance > 0 ? variance : 0);

    /* 百分位 */
    int p50_idx = (int)(a->count * 0.50);
    int p95_idx = (int)(a->count * 0.95);
    int p99_idx = (int)(a->count * 0.99);
    if (p50_idx >= a->count) p50_idx = a->count - 1;
    if (p95_idx >= a->count) p95_idx = a->count - 1;
    if (p99_idx >= a->count) p99_idx = a->count - 1;

    /* 理论最小延迟 = 2 * 包大小 * 10bit / 波特率 (串口双向传输时间) */
    double theory_us = 2.0 * pkt_size * 10.0 * 1000000.0 / baud;

    /* 转换 ns -> us 用于显示 */
    double min_us  = (double)min / 1000.0;
    double max_us  = (double)max / 1000.0;
    double avg_us  = avg / 1000.0;
    double std_us  = stddev / 1000.0;
    double p50_us  = (double)a->values[p50_idx] / 1000.0;
    double p95_us  = (double)a->values[p95_idx] / 1000.0;
    double p99_us  = (double)a->values[p99_idx] / 1000.0;

    printf("------------------------------------------------------------\n");
    printf("  延迟统计\n");
    printf("------------------------------------------------------------\n");
    printf("  最小延迟:      %.2f ms  (%.0f us / %lld ns)\n", min_us / 1000.0, min_us, min);
    printf("  最大延迟:      %.2f ms  (%.0f us / %lld ns)\n", max_us / 1000.0, max_us, max);
    printf("  平均延迟:      %.2f ms  (%.0f us)\n", avg_us / 1000.0, avg_us);
    printf("  中位延迟(P50): %.2f ms  (%.0f us)\n", p50_us / 1000.0, p50_us);
    printf("  P95 延迟:      %.2f ms  (%.0f us)\n", p95_us / 1000.0, p95_us);
    printf("  P99 延迟:      %.2f ms  (%.0f us)\n", p99_us / 1000.0, p99_us);
    printf("  标准差:        %.2f us  (%.2f ms)\n", std_us, std_us / 1000.0);
    printf("------------------------------------------------------------\n");
    printf("  理论最小延迟:  %.2f ms  (%.0f us, 双向串口传输时间)\n", theory_us / 1000.0, theory_us);
    printf("  额外开销:      %.2f us  (实测平均 - 理论最小)\n", avg_us - theory_us);
    printf("============================================================\n");
}

/* ======================== 单波特率测试 ======================== */

static int run_single_test(const char *device, int baud, int packet_size,
                           int test_count, int test_time, int timeout_ms)
{
    if (packet_size < 2 || packet_size > MAX_PACKET_SIZE)
    {
        fprintf(stderr, "数据包大小需在 2~%d 之间\n", MAX_PACKET_SIZE);
        return -1;
    }

    unsigned char *tx_buf = malloc(packet_size);
    unsigned char *rx_buf = malloc(packet_size);
    if (!tx_buf || !rx_buf)
    {
        fprintf(stderr, "内存分配失败\n");
        free(tx_buf); free(rx_buf);
        return -1;
    }

    printf("\n打开串口: %s @ %d bps, packet=%d bytes\n", device, baud, packet_size);

    int fd = open_serial(device, baud, packet_size, timeout_ms);
    if (fd < 0)
    {
        free(tx_buf); free(rx_buf);
        return -1;
    }

    usleep(200000);          /* 等待串口稳定 */
    tcflush(fd, TCIOFLUSH);  /* 清空缓冲区 */

    LatencyArray latencies;
    latency_init(&latencies, 1024);

    int total_tries  = 0;
    int timeout_cnt  = 0;
    int error_cnt    = 0;

    struct timespec t_start, t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    printf("测试中... (Ctrl+C 结束)\n");

    while (g_running)
    {
        /* 定时模式检查 */
        if (test_time > 0)
        {
            clock_gettime(CLOCK_MONOTONIC, &t_now);
            long long elapsed_us = time_diff_us(&t_start, &t_now);
            if (elapsed_us >= test_time * 1000000LL)
                break;
        }

        /* 定次模式检查 */
        if (test_count > 0 && total_tries >= test_count)
            break;

        total_tries++;

        /* 填充测试数据 */
        fill_test_data(tx_buf, packet_size);

        /* 发送前清空内核的残留数据，防止旧数据混入当前测试 */
        tcflush(fd, TCIFLUSH);

        struct timespec t_send, t_recv;
        clock_gettime(CLOCK_MONOTONIC, &t_send);

        /* 发送 */
        ssize_t written = write(fd, tx_buf, packet_size);
        if (written != packet_size)
        {
            fprintf(stderr, "[%d] 写入失败: %zd/%d\n", total_tries, written, packet_size);
            error_cnt++;
            usleep(1000);
            continue;
        }

        /* 接收 (带超时) */
        int n = read_with_timeout(fd, rx_buf, packet_size, timeout_ms);
        if (n < 0)
        {
            /* 超时 */
            timeout_cnt++;
            if (timeout_cnt <= 5 || timeout_cnt % 50 == 0)
                printf("[%d] 接收超时 (total=%d, timeout=%d)\n",
                       total_tries, total_tries, timeout_cnt);

            tcflush(fd, TCIFLUSH);
            continue;
        }

        clock_gettime(CLOCK_MONOTONIC, &t_recv);

        /* 计算延迟 (纳秒) */
        long long rtt_ns = time_diff_ns(&t_send, &t_recv);

        /* 数据校验 */
        int vret = verify_data(tx_buf, rx_buf, packet_size);
        if (vret != 0)
        {
            error_cnt++;
            printf("[%d] 数据校验失败 (ret=%d): len=%d\n", total_tries, vret, n);
            continue;
        }

        /* 记录成功 */
        latency_add(&latencies, rtt_ns);

        /* 进度显示 */
        if (latencies.count % 100 == 0)
        {
            printf("  [%d/%d] OK=%d TO=%d ERR=%d last=%.2f ms\n",
                   total_tries,
                   test_count > 0 ? test_count : total_tries,
                   latencies.count, timeout_cnt, error_cnt,
                   (double)rtt_ns / 1000000.0);
        }
    }

    close(fd);

    /* 输出报告 */
    latency_report(&latencies, baud, packet_size, total_tries, timeout_cnt, error_cnt);

    /* 如果成功次数足够, 列出前几次详细延迟 */
    if (latencies.count > 0)
    {
        int show = (latencies.count < 20) ? latencies.count : 10;
        printf("  前 %d 次延迟 (us / ms):\n  ", show);
        for (int i = 0; i < show; i++)
        {
            double us = (double)latencies.values[i] / 1000.0;
            printf(" %.0fus(%.2fms)", us, us / 1000.0);
        }
        printf("\n\n");
    }

    latency_free(&latencies);
    free(tx_buf);
    free(rx_buf);
    return 0;
}

/* ======================== 命令行解析 ======================== */

static void print_usage(const char *prog)
{
    printf("\n");
    printf("用法: %s [选项]\n", prog);
    printf("\n");
    printf("选项:\n");
    printf("  -d <设备>      串口设备 (默认: %s)\n", DEFAULT_DEVICE);
    printf("  -b <波特率>    波特率, 可多次指定以遍历测试 (默认: %d)\n", DEFAULT_BAUDRATE);
    printf("                 支持: 9600 19200 38400 57600 115200 230400\n");
    printf("                       460800 500000 576000 921600 1000000\n");
    printf("  -s <字节数>    单次发送字节数 (默认: %d, 最大: %d)\n", DEFAULT_PACKET_SIZE, MAX_PACKET_SIZE);
    printf("  -t <次数>      测试次数, 0=不限制 (默认: %d)\n", DEFAULT_TEST_COUNT);
    printf("  -T <秒数>      测试时长(定时模式), 0=禁用 (默认: %d)\n", DEFAULT_TEST_TIME);
    printf("  -w <毫秒>      接收超时 (默认: %dms)\n", DEFAULT_TIMEOUT_MS);
    printf("  -h             帮助\n");
    printf("\n");
    printf("示例:\n");
    printf("  # 单次测试: 115200bps, 64字节, 测试1000次\n");
    printf("  %s -b 115200 -s 64 -t 1000\n", prog);
    printf("\n");
    printf("  # 遍历波特率: 测试多个波特率下的延迟\n");
    printf("  %s -b 9600 -b 115200 -b 921600 -s 256 -t 500\n", prog);
    printf("\n");
    printf("  # 定时测试: 定时30秒\n");
    printf("  %s -b 115200 -s 128 -T 30\n", prog);
    printf("\n");
    printf("  # 自定义设备\n");
    printf("  %s -d /dev/ttyS0 -b 230400 -s 512 -t 500\n", prog);
    printf("\n");
}

/* ======================== 主函数 ======================== */

int main(int argc, char *argv[])
{
    const char *device = DEFAULT_DEVICE;
    int baud_list[MAX_BAUD_VALUES];
    int baud_count = 0;
    int packet_size = DEFAULT_PACKET_SIZE;
    int test_count  = DEFAULT_TEST_COUNT;
    int test_time   = DEFAULT_TEST_TIME;
    int timeout_ms  = DEFAULT_TIMEOUT_MS;
    int opt;

    /* 默认波特率 */
    baud_list[baud_count++] = DEFAULT_BAUDRATE;

    while ((opt = getopt(argc, argv, "d:b:s:t:T:w:h")) != -1)
    {
        switch (opt)
        {
        case 'd':
            device = optarg;
            break;
        case 'b': {
            int v = atoi(optarg);
            if (lookup_baud(v) == B0)
            {
                fprintf(stderr, "不支持的波特率: %d\n", v);
                return 1;
            }
            if (baud_count == 1 && baud_list[0] == DEFAULT_BAUDRATE)
                baud_count = 0;  /* 覆盖默认 */
            if (baud_count < MAX_BAUD_VALUES)
                baud_list[baud_count++] = v;
            break;
        }
        case 's':
            packet_size = atoi(optarg);
            if (packet_size < 2 || packet_size > MAX_PACKET_SIZE)
            {
                fprintf(stderr, "数据包大小需在 2~%d 之间\n", MAX_PACKET_SIZE);
                return 1;
            }
            break;
        case 't':
            test_count = atoi(optarg);
            if (test_count < 0) test_count = 0;
            break;
        case 'T':
            test_time = atoi(optarg);
            if (test_time < 0) test_time = 0;
            break;
        case 'w':
            timeout_ms = atoi(optarg);
            if (timeout_ms < 1) timeout_ms = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* 注册 Ctrl+C 信号 */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("============================================================\n");
    printf("  串口内环延迟测试工具\n");
    printf("============================================================\n");
    printf("  设备:        %s\n", device);
    printf("  包大小:      %d 字节\n", packet_size);
    printf("  超时:        %d ms\n", timeout_ms);
    if (test_time > 0)
        printf("  模式:        定时 %.0f 秒\n", (double)test_time);
    else if (test_count > 0)
        printf("  模式:        定次 %d 次\n", test_count);
    else
        printf("  模式:        无限 (Ctrl+C 停止)\n");

    printf("  波特率列表:");
    for (int i = 0; i < baud_count; i++)
        printf(" %d", baud_list[i]);
    printf("\n");
    printf("============================================================\n");

    /* 遍历每个波特率执行测试 */
    int ret = 0;
    for (int i = 0; i < baud_count && g_running; i++)
    {
        printf("\n\n");
        printf(">>>> 测试 [%d/%d]: %d bps <<<<\n", i + 1, baud_count, baud_list[i]);
        if (run_single_test(device, baud_list[i], packet_size,
                            test_count, test_time, timeout_ms) != 0)
        {
            ret = 1;
        }
    }

    printf("\n所有测试完成.\n");
    return ret;
}

/*

# 编译
gcc -o inner_loop_test test/inner_loop_test.c -lm -lpthread

# 单次测试: 115200bps, 64字节, 测试1000次
./inner_loop_test -b 115200 -s 64 -t 1000

# 遍历多个波特率
./inner_loop_test -b 9600 -b 115200 -b 921600 -s 128 -t 500

# 定时测试30秒
./inner_loop_test -b 115200 -s 64 -T 30

*/