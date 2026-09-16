# LED操作问题解决方案

## 问题描述
- `devmem 0x40000130 32 1` 等命令可以点亮LED
- 但代码中调用 `rxled()` 等函数无反应

## 根本原因
代码中使用 `sysAxiWriteLong((int32_t)pl_led_base_addr + reg_address, value)`，可能存在以下问题：
1. `pl_led_base_addr` 未初始化或为 `MAP_FAILED`
2. `init_pl_memory_access()` 未被调用
3. 地址计算方式不安全

## 解决方案

### 方案1：运行独立测试程序（推荐）
```bash
cd test
gcc -o test_led_direct test_led_direct.c
sudo ./test_led_direct
```
如果测试程序能点亮LED，说明硬件和映射正常，问题在现有代码。

### 方案2：修改现有代码
修改 `HAL/hal_axi16550.c` 中的 `rxled()` 函数：

```c
void rxled(int i, int action)
{
    if (i < 0 || i > 15) return;
    
    // 检查映射是否有效
    if (pl_led_base_addr == MAP_FAILED) {
        printf("错误：PL内存未映射！\n");
        return;
    }
    
    uint32_t reg_address = RECEIVE_LED_OFFSET + (i * 4);  
    uint32_t value = (action == 1) ? 1 : 0;  
    
    // 使用指针算术，避免类型转换问题
    volatile uint32_t *led_reg = (volatile uint32_t *)((char *)pl_led_base_addr + reg_address);
    volatile uint32_t *iic_reg = (volatile uint32_t *)((char *)pl_led_base_addr + IIC_REG_OFFSET);
    
    *led_reg = value;      // 等效于 devmem 0x40000130 32 1
    *iic_reg = 1;          // 等效于 devmem 0x40000280 32 1
    *iic_reg = 0;          // 等效于 devmem 0x40000280 32 0
}
```

### 方案3：添加调试信息
在现有代码中添加调试输出：
```c
void rxled(int i, int action)
{
    printf("[DEBUG] rxled called, pl_led_base_addr = %p\n", pl_led_base_addr);
    if (pl_led_base_addr == MAP_FAILED) {
        printf("[ERROR] PL内存未映射！\n");
        return;
    }
    // ... 原有代码
}
```

## 检查清单

1. **初始化检查**：确保 `init_pl_memory_access()` 被调用
2. **权限检查**：程序必须以root权限运行
3. **地址检查**：`pl_led_base_addr` 不应为 `MAP_FAILED`
4. **编译检查**：确保重新编译了修改后的代码

## 快速测试步骤

1. 运行独立测试程序：
   ```bash
   sudo ./test_led_direct
   ```

2. 如果测试程序成功：
   - 检查 `init_pl_memory_access()` 是否被调用
   - 在 `rxled()` 中添加调试输出
   - 确保重新编译

3. 如果测试程序也失败：
   - 检查物理地址 `0x40000000` 是否正确
   - 检查硬件连接
   - 查看 `dmesg` 输出

## 文件位置
- 独立测试程序：`test/test_led_direct.c`
- 现有代码：`HAL/hal_axi16550.c`
- 头文件：`HAL/hal_axi16550.h`