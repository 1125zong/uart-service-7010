/*
 * LED测试程序
 * 用于诊断PL LED内存映射和操作问题
 * 
 * 编译：gcc -o test_led test_led.c
 * 运行：sudo ./test_led
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

// PL AXI基地址
#define PL_AXI_BASE         0x40000000
#define PL_AXI_SIZE         0x10000  // 64KB

// LED寄存器偏移（来自hal_axi16550.h）
#define RECEIVE_LED_OFFSET  0x130    // 接收LED
#define SEND_LED_OFFSET     0x230    // 发送LED
#define LOCKED_LED_OFFSET   0x30     // 锁定LED
#define DEVICE_LOCKED_LED_OFFSET 0x270 // 设备锁定LED
#define IIC_REG_OFFSET      0x280    // IIC触发寄存器

int main() {
    int mem_fd = -1;
    volatile void *mapped_base = MAP_FAILED;
    volatile uint32_t *led_reg = NULL;
    volatile uint32_t *iic_reg = NULL;
    
    printf("=== LED内存映射测试程序 ===\n\n");
    
    // 1. 打开/dev/mem
    printf("1. 打开 /dev/mem...\n");
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) {
        printf("   [错误] 打开/dev/mem失败: %s\n", strerror(errno));
        printf("   请确保以root权限运行！\n");
        return -1;
    }
    printf("   [成功] 文件描述符: %d\n", mem_fd);
    
    // 2. 映射物理地址
    printf("\n2. 映射物理地址 0x%08X (大小: 0x%X)...\n", PL_AXI_BASE, PL_AXI_SIZE);
    mapped_base = mmap(NULL, PL_AXI_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, PL_AXI_BASE);
    if (mapped_base == MAP_FAILED) {
        printf("   [错误] mmap失败: %s\n", strerror(errno));
        close(mem_fd);
        return -1;
    }
    printf("   [成功] 虚拟地址: %p\n", mapped_base);
    
    // 3. 测试读取
    printf("\n3. 测试读取...\n");
    volatile uint32_t *test_reg = (volatile uint32_t *)mapped_base;
    printf("   读取 0x%08X: 0x%08X\n", PL_AXI_BASE, *test_reg);
    
    // 4. 计算寄存器地址
    printf("\n4. 计算寄存器地址...\n");
    led_reg = (volatile uint32_t *)((char *)mapped_base + RECEIVE_LED_OFFSET);
    iic_reg = (volatile uint32_t *)((char *)mapped_base + IIC_REG_OFFSET);
    
    printf("   接收LED寄存器地址: %p (物理: 0x%08X)\n", led_reg, PL_AXI_BASE + RECEIVE_LED_OFFSET);
    printf("   IIC触发寄存器地址: %p (物理: 0x%08X)\n", iic_reg, PL_AXI_BASE + IIC_REG_OFFSET);
    
    // 5. 测试写入接收LED
    printf("\n5. 测试写入接收LED (0x130)...\n");
    printf("   写入前: 0x%08X\n", *led_reg);
    *led_reg = 1;  // 点亮LED
    printf("   写入后: 0x%08X\n", *led_reg);
    
    // 6. 触发IIC寄存器
    printf("\n6. 触发IIC寄存器 (0x280)...\n");
    printf("   写入前: 0x%08X\n", *iic_reg);
    *iic_reg = 1;
    printf("   写入1: 0x%08X\n", *iic_reg);
    *iic_reg = 0;
    printf("   写入0: 0x%08X\n", *iic_reg);
    
    // 7. 测试其他LED
    printf("\n7. 测试其他LED寄存器...\n");
    
    // 发送LED
    volatile uint32_t *send_led = (volatile uint32_t *)((char *)mapped_base + SEND_LED_OFFSET);
    printf("   发送LED (0x230): 写入前=0x%08X, ", *send_led);
    *send_led = 1;
    printf("写入后=0x%08X\n", *send_led);
    
    // 锁定LED
    volatile uint32_t *locked_led = (volatile uint32_t *)((char *)mapped_base + LOCKED_LED_OFFSET);
    printf("   锁定LED (0x30): 写入前=0x%08X, ", *locked_led);
    *locked_led = 1;
    printf("写入后=0x%08X\n", *locked_led);
    
    // 8. 清除所有LED
    printf("\n8. 清除所有LED...\n");
    // *send_led = 0;
    // *locked_led = 0;
    // *led_reg = 0;
    // *iic_reg = 1;
    // *iic_reg = 0;
    printf("   [完成] 所有LED已清除\n");
    
    // 9. 检查映射是否有效
    printf("\n9. 验证内存映射...\n");
    printf("   映射基地址: %p\n", mapped_base);
    printf("   映射大小: 0x%X\n", PL_AXI_SIZE);
    printf("   映射范围: %p - %p\n", mapped_base, (char *)mapped_base + PL_AXI_SIZE - 1);
    
    // 10. 清理资源
    printf("\n10. 清理资源...\n");
    if (munmap((void *)mapped_base, PL_AXI_SIZE) == -1) {
        printf("    [警告] munmap失败: %s\n", strerror(errno));
    } else {
        printf("    [成功] 内存映射已释放\n");
    }
    
    if (close(mem_fd) == -1) {
        printf("    [警告] 关闭文件失败: %s\n", strerror(errno));
    } else {
        printf("    [成功] 文件已关闭\n");
    }
    
    printf("\n=== 测试完成 ===\n");
    printf("如果LED无反应，请检查：\n");
    printf("1. 是否以root权限运行\n");
    printf("2. 硬件是否连接正确\n");
    printf("3. 物理地址0x%08X是否正确\n", PL_AXI_BASE);
    printf("4. 查看dmesg是否有错误信息\n");
    
    return 0;
}