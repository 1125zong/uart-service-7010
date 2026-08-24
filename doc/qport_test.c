#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <math.h>

// 默认参数
#define DEFAULT_SERIAL_PORT "/dev/ttyw00"
#define DEFAULT_BAUD_RATE 115200
#define DEFAULT_DATA_SIZE 5
#define DEFAULT_TEST_TIMES 2
#define MAX_DATA_SIZE 4096

// 时间差 (微秒)
static long long time_diff_us(const struct timespec *start, const struct timespec *end)
{
    return (end->tv_sec - start->tv_sec) * 1000000LL +
           (end->tv_nsec - start->tv_nsec) / 1000;
}

// 波特率转换
static speed_t get_baud_rate(int baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 500000: return B500000;
        case 576000: return B576000;
        case 921600: return B921600;
        case 1000000: return B1000000;
        default: return B115200;
    }
}

// 配置串口为原始模式，阻塞读取 (VMIN=data_size, VTIME=0)
static int configure_serial(int fd, speed_t baud_rate, int data_size)
{
    struct termios opt;
    if (tcgetattr(fd, &opt) < 0) {
        perror("tcgetattr");
        return -1;
    }

    cfsetispeed(&opt, baud_rate);
    cfsetospeed(&opt, baud_rate);

    opt.c_cflag |= (CLOCAL | CREAD);
    opt.c_cflag &= ~PARENB;      // 无校验
    opt.c_cflag &= ~CSTOPB;      // 1停止位
    opt.c_cflag &= ~CSIZE;
    opt.c_cflag |= CS8;          // 8数据位
    opt.c_cflag &= ~CRTSCTS;     // 关闭硬件流控

    opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    opt.c_iflag &= ~(IXON | IXOFF | IXANY);  // 关闭软件流控
    opt.c_iflag &= ~(INLCR | ICRNL);
    opt.c_oflag &= ~OPOST;

    // 关键修改：阻塞模式，直到读满 data_size 字节
    opt.c_cc[VMIN] = data_size;
    opt.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &opt) < 0) {
        perror("tcsetattr");
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return 0;
}

// 打印帮助信息
static void print_usage(const char *prog)
{
    printf("用法: %s [选项]\n", prog);
    printf("选项:\n");
    printf("  -d <设备>      串口设备 (默认: %s)\n", DEFAULT_SERIAL_PORT);
    printf("  -b <波特率>    波特率 (默认: %d)\n", DEFAULT_BAUD_RATE);
    printf("  -s <字节数>    单次发送字节数 (1-%d, 默认: %d)\n", MAX_DATA_SIZE, DEFAULT_DATA_SIZE);
    printf("  -t <次数>      测试次数 (默认: %d)\n", DEFAULT_TEST_TIMES);
    printf("  -h             显示帮助\n");
}

int main(int argc, char *argv[])
{
    const char *serial_port = DEFAULT_SERIAL_PORT;
    int baud_rate_val = DEFAULT_BAUD_RATE;
    int data_size = DEFAULT_DATA_SIZE;
    int test_times = DEFAULT_TEST_TIMES;
    int opt;

    // 解析参数
    while ((opt = getopt(argc, argv, "d:b:s:t:h")) != -1) {
        switch (opt) {
            case 'd': serial_port = optarg; break;
            case 'b': baud_rate_val = atoi(optarg); break;
            case 's': data_size = atoi(optarg);
                      if (data_size <= 0 || data_size > MAX_DATA_SIZE) {
                          fprintf(stderr, "数据大小范围 1-%d\n", MAX_DATA_SIZE);
                          return 1;
                      }
                      break;
            case 't': test_times = atoi(optarg);
                      if (test_times <= 0) {
                          fprintf(stderr, "测试次数 >0\n");
                          return 1;
                      }
                      break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    printf("=== 串口环回延迟测试 (阻塞模式，适用于网络转发) ===\n");
    printf("设备: %s\n", serial_port);
    printf("波特率: %d\n", baud_rate_val);
    printf("数据大小: %d 字节\n", data_size);
    printf("测试次数: %d\n\n", test_times);

    // 分配缓冲区
    unsigned char *tx_buf = malloc(data_size);
    unsigned char *rx_buf = malloc(data_size);
    if (!tx_buf || !rx_buf) {
        fprintf(stderr, "内存分配失败\n");
        free(tx_buf); free(rx_buf);
        return 1;
    }

    // 填充测试数据 (模式 0x55, 0x56, ...)
    for (int i = 0; i < data_size; i++)
        tx_buf[i] = 0x55 + i;

    // 打开串口 (阻塞模式)
    int fd = open(serial_port, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("打开串口失败");
        free(tx_buf); free(rx_buf);
        return 1;
    }

    // 配置串口（传入 data_size 用于设置 VMIN）
    speed_t baud = get_baud_rate(baud_rate_val);
    if (configure_serial(fd, baud, data_size) < 0) {
        close(fd);
        free(tx_buf); free(rx_buf);
        return 1;
    }

    // 等待串口稳定
    usleep(500000);
    tcflush(fd, TCIOFLUSH);

    long long *delays = malloc(test_times * sizeof(long long));
    int success = 0;
    struct timespec start, end;

    printf("开始测试...\n\n");
    for (int i = 0; i < test_times; i++) {
        // 清空接收缓冲区，丢弃旧数据
        tcflush(fd, TCIFLUSH);

        // 记录起始时间
        clock_gettime(CLOCK_MONOTONIC, &start);

        // 发送数据
        ssize_t written = write(fd, tx_buf, data_size);
        if (written != data_size) {
            printf("[%d] 发送失败 (%zd/%d)\n", i+1, written, data_size);
            continue;
        }

        // 阻塞读取直到收满 data_size 字节
        ssize_t n = read(fd, rx_buf, data_size);
        if (n != data_size) {
            printf("[%d] 接收失败 (%zd/%d), errno=%d\n", i+1, n, data_size, errno);
            continue;
        }

        clock_gettime(CLOCK_MONOTONIC, &end);

        // 校验数据
        int valid = 1;
        for (int j = 0; j < data_size; j++) {
            if (rx_buf[j] != tx_buf[j]) {
                valid = 0;
                break;
            }
        }
        if (!valid) {
            printf("[%d] 数据校验失败\n", i+1);
            continue;
        }

        long long delay_us = time_diff_us(&start, &end);
        delays[success++] = delay_us;
        printf("[%d] 延迟: %lld us\n", i+1, delay_us);

        // 增加测试间隔，避免网络缓冲区累积
        usleep(200000);  // 200ms
    }

    // 统计结果
    printf("\n=== 统计结果 (成功 %d/%d) ===\n", success, test_times);
    if (success > 0) {
        long long min_delay = delays[0], max_delay = delays[0], sum = 0;
        for (int i = 0; i < success; i++) {
            if (delays[i] < min_delay) min_delay = delays[i];
            if (delays[i] > max_delay) max_delay = delays[i];
            sum += delays[i];
        }
        double avg = (double)sum / success;
        double variance = 0;
        for (int i = 0; i < success; i++) {
            double diff = delays[i] - avg;
            variance += diff * diff;
        }
        variance /= success;
        double stddev = sqrt(variance);

        printf("最小延迟: %lld us\n", min_delay);
        printf("最大延迟: %lld us\n", max_delay);
        printf("平均延迟: %.2f us\n", avg);
        printf("标准差: %.2f us\n", stddev);
        printf("\n理论最小值 (115200bps, %d字节): 约 %.0f us\n",
               data_size, (data_size * 10.0 * 1000000 / baud_rate_val));
        printf("注意：实测值包含网络往返延迟及系统调度开销。\n");
    }

    close(fd);
    free(tx_buf);
    free(rx_buf);
    free(delays);
    return 0;
}