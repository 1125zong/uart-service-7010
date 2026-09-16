/*
 * 直接LED操作测试程序
 * 直接映射物理地址并操作LED，不依赖任何项目代码
 * 
 * 编译：gcc -o test_led_direct test_led_direct.c
 * 运行：sudo ./test_led_direct
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

// 物理地址定义
#define PL_AXI_BASE         0x40000000
#define RECEIVE_LED_OFFSET  0x130
#define IIC_REG_OFFSET      0x280

int main() {
    int fd = -1;
    volatile void *base = MAP_FAILED;
    volatile uint32_t *led_reg = NULL;
    volatile uint32_t *iic_reg = NULL;
    
    printf("=== 直接LED操作测试 ===\n\n");
    
    // 1. 打开 /dev/mem
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        printf("错误：无法打开 /dev/mem: %s\n", strerror(errno));
        printf("请使用 sudo 运行此程序\n");
        return -1;
    }
    
    // 2. 映射物理地址（只映射需要的4KB）
    base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, PL_AXI_BASE);
    if (base == MAP_FAILED) {
        printf("错误：mmap 失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    
    printf("物理基地址: 0x%08X\n", PL_AXI_BASE);
    printf("虚拟基地址: %p\n\n", base);
    
    // 3. 计算寄存器地址
    led_reg = (volatile uint32_t *)((char *)base + RECEIVE_LED_OFFSET);
    iic_reg = (volatile uint32_t *)((char *)base + IIC_REG_OFFSET);
    
    printf("接收LED寄存器 (0x%08X): %p\n", PL_AXI_BASE + RECEIVE_LED_OFFSET, led_reg);
    printf("IIC触发寄存器 (0x%08X): %p\n\n", PL_AXI_BASE + IIC_REG_OFFSET, iic_reg);
    
    // 4. 读取初始值
    printf("初始值:\n");
    printf("  接收LED: 0x%08X\n", *led_reg);
    printf("  IIC触发: 0x%08X\n\n", *iic_reg);
    
    // 5. 执行devmem等效操作
    printf("执行操作:\n");
    
    // devmem 0x40000130 32 1
    printf("1. 写入接收LED (0x130) = 1\n");
    *led_reg = 1;
    printf("   写入后: 0x%08X\n", *led_reg);
    
    // devmem 0x40000280 32 1
    printf("2. 写入IIC触发 (0x280) = 1\n");
    *iic_reg = 1;
    printf("   写入后: 0x%08X\n", *iic_reg);
    
    // devmem 0x40000280 32 0
    printf("3. 写入IIC触发 (0x280) = 0\n");
    *iic_reg = 0;
    printf("   写入后: 0x%08X\n", *iic_reg);
    
    // 6. 验证操作
    printf("\n最终状态:\n");
    printf("  接收LED: 0x%08X\n", *led_reg);
    printf("  IIC触发: 0x%08X\n", *iic_reg);
    
    // 7. 清理
    munmap((void *)base, 0x1000);
    close(fd);
    
    printf("\n=== 测试完成 ===\n");
    printf("如果LED亮起，说明硬件和映射正常。\n");
    printf("请检查您的代码是否正确调用了初始化函数。\n");
    
    return 0;
}