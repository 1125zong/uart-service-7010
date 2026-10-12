/**
 * axi16550_test.c
 * 测试Zynq PL端16550 UART，直接内存映射操作寄存器
 * 编译：gcc -o axi16550_test axi16550_test.c
 * 运行：sudo ./axi16550_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>

// 包含你的头文件，假设它位于当前目录
#include "hal_axi16550.h"

// 物理基地址（根据Vivado地址分配修改）
#define PHYS_UART_BASE  0x41200000
// 映射大小（根据实际地址范围，例如单个通道0x2000，多通道可适当扩大）
#define MAP_SIZE        0x10000   // 64KB，覆盖多个通道

// 全局变量：pl_uart_base_addr 在头文件中被使用，需要定义
volatile unsigned char *pl_uart_base_addr = NULL;

// 测试用的通道号（根据实际连接修改）
#define TEST_CHANNEL    0

// 标志位，用于循环退出
volatile int keep_running = 1;


// 示例：向指定通道的寄存器写入32位值
void userAxiCfgWrite(unsigned int channel, unsigned int offset, unsigned int data)
{
    volatile uint32_t *reg = (volatile uint32_t *)(pl_uart_base_addr + 0x2000 * channel + offset);
    *reg = data;
}

unsigned int userAxiCfgRead(unsigned int channel, unsigned int offset)
{
    volatile uint32_t *reg = (volatile uint32_t *)(pl_uart_base_addr + 0x2000 * channel + offset);
    return *reg;
}

// 初始化：设置波特率、8N1、使能FIFO
void axi16550Init(unsigned int channel, unsigned int baud)
{
    // 先设置LCR使能除数锁存器访问（DLAB = 1）
    uint32_t lcr = userAxiCfgRead(channel, AXI_16550_LCR);
    lcr |= 0x80;  // 设置DLAB位（通常为bit7）
    userAxiCfgWrite(channel, AXI_16550_LCR, lcr);

    // 设置波特率除数（假设时钟频率为AXI_16550_CLK = 29491200）
    uint32_t divisor = AXI_16550_CLK / (baud * 16);
    userAxiCfgWrite(channel, AXI_16550_DLL, divisor & 0xFF);
    userAxiCfgWrite(channel, AXI_16550_DLM, (divisor >> 8) & 0xFF);

    // 恢复LCR：8位数据，1停止位，无校验，且DLAB=0
    lcr = 0x03;  // 8位数据，1停止位，无校验
    userAxiCfgWrite(channel, AXI_16550_LCR, lcr);

    // 使能FIFO，并设置触发阈值（可选）
    userAxiCfgWrite(channel, AXI_16550_FCR, 0x07);  // 使能FIFO并清零

    // 禁用中断（轮询模式）
    userAxiCfgWrite(channel, AXI_16550_IER, 0x00);
}

// 发送字符串（阻塞直到全部发送完毕）
int axi16550Send(unsigned int channel, uint8_t *buffer, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        // 等待发送保持寄存器空（THRE）
        while (!(userAxiCfgRead(channel, AXI_16550_LSR) & LSR_THRE));
        userAxiCfgWrite(channel, AXI_16550_THR, buffer[i]);
    }
    return 0;
}

// 接收（非阻塞，返回实际收到的字节数）
int axi16550Recv(unsigned int channel, uint8_t *buffer, uint32_t *len,
                 uint32_t *frame_err, uint32_t *parity_err,
                 uint32_t *overrun_err, uint32_t *break_cnt)
{
    *len = 0;
    if (userAxiCfgRead(channel, AXI_16550_LSR) & LSR_DR) {
        buffer[0] = userAxiCfgRead(channel, AXI_16550_RBR) & 0xFF;
        *len = 1;
    }
    return 0;
}



void int_handler(int dummy) {
    keep_running = 0;
}

int main(int argc, char **argv)
{
    int mem_fd;
    void *map_base;

    // 注册信号处理，用于Ctrl+C退出
    signal(SIGINT, int_handler);

    // 1. 打开 /dev/mem
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("open /dev/mem failed");
        return -1;
    }

    // 2. 映射物理地址到用户空间
    map_base = mmap(NULL,
                    MAP_SIZE,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    mem_fd,
                    PHYS_UART_BASE);
    if (map_base == MAP_FAILED) {
        perror("mmap failed");
        close(mem_fd);
        return -1;
    }
    close(mem_fd);  // 映射后可以关闭文件描述符

    // 3. 设置全局基地址指针（供后续函数使用）
    pl_uart_base_addr = (volatile unsigned char *)map_base;
    printf("UART registers mapped at virtual address %p\n", pl_uart_base_addr);

    // 4. 初始化UART（设置波特率、数据格式等）
    //    假设axi16550Init函数会根据全局pl_uart_base_addr计算通道地址
    axi16550Init(TEST_CHANNEL, 115200);   // 波特率可根据需要修改
    printf("UART channel %d initialized at 115200 baud\n", TEST_CHANNEL);

    // 5. 发送测试字符串
    char *test_str = "Hello from MMIO UART on Zynq!\r\n";
    int len = strlen(test_str);
    int ret = axi16550Send(TEST_CHANNEL, (uint8_t *)test_str, len);
    if (ret != 0) {
        printf("Send error: %d\n", ret);
    } else {
        printf("Sent %d bytes\n", len);
    }

    // 6. 进入回显测试：接收一个字符，立即回传
    printf("Entering echo mode. Type characters, they will be echoed back. Press Ctrl+C to exit.\n");
    while (keep_running) {
        uint8_t rx_buf[1];
        uint32_t recv_len = 0;

        // 尝试接收一个字符（非阻塞检查）
        if (axi16550Recv(TEST_CHANNEL, rx_buf, &recv_len) == 0 && recv_len == 1) {
            // 将收到的字符回显
            axi16550Send(TEST_CHANNEL, rx_buf, 1);
            // 如果是回车换行，可以额外处理
            if (rx_buf[0] == '\r' || rx_buf[0] == '\n') {
                // 可选：发送换行
                // axi16550Send(TEST_CHANNEL, (uint8_t *)"\r\n", 2);
            }
        }
        // 加入短暂延时，避免CPU占用过高
        usleep(1000);
    }

    // 7. 清理映射
    munmap(map_base, MAP_SIZE);
    printf("\nTest finished.\n");
    return 0;
}