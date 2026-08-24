#include "my_screen_driver.h"
#include <stdio.h> // 用于 sprintf, fprintf, stderr
#include <string.h> // 用于 strlen, memcpy, strerror
#include <unistd.h>  // 用于 write, close, usleep
#include <fcntl.h>   // 用于 open
#include <termios.h> // 用于 termios
#include <errno.h>   // 用于 errno
#include <linux/serial.h> 

// =========================================================================
// 1. 底层硬件依赖（Linux 串口）
// =========================================================================

// [TODO] 更改此处的设备路径
#define SERIAL_DEVICE "/dev/ttyUSB0" 
// [TODO] 更改此处的波特率
#define BAUD_RATE B115200

// [修改] 指令后延迟 (单位: 微秒)
// (80ms = 80000 microseconds)
#define POST_COMMAND_DELAY_US 80000

// 全局文件描述符
static int g_serial_fd = -1;

/**
 * @brief [新] 辅助函数：打印十六进制调试信息
 */
static void print_hex_dump(const char* prefix, const uint8_t* data, uint16_t length) {
    // 确保 length 是 unsigned，以便与 i 比较
    printf("DRIVER: %s (%u bytes): ", prefix, (unsigned int)length);
    for (uint16_t i = 0; i < length; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}


/**
 * @brief [Linux] 平台相关的 UART 初始化
 */
static void platform_uart_init(void) {
    
	sysUartInit(0,115200);
}

/**
 * @brief [Linux] 平台相关的 UART 发送
 */
static void platform_uart_send(const uint8_t* data, uint16_t length) {
    
    // 在发送前打印完整的十六进制数据包
    print_hex_dump("TX", data, length);

   sysUartSend(0 ,  data  ,  length);
}

/**
 * @brief [新增] 关闭串口 (用于 atexit)
 */
void platform_uart_close(void) {
    if (g_serial_fd != -1) {
        printf("\nDRIVER: Closing %s.\n", SERIAL_DEVICE);
        // [新增] 在关闭前，最后再刷新一次缓冲区
        tcdrain(g_serial_fd);
        close(g_serial_fd);
        g_serial_fd = -1;
    }
}

// =========================================================================
// 2. 实现 display_driver_t 的接口函数 (使用您的新协议)
// =========================================================================

// --- 协议常量 ---
// --- TJC屏幕是以 ASCII指令 + [参数] + 三个FF结尾 的数据帧形式
static const uint8_t FRAME_FOOTER[] = { 0XFF, 0XFF, 0XFF };

// [可修改---默认清屏为黑色] 清屏指令 
#define CMD_CLEAR_SCREEN "cls BLACK"

// [可修改---一个字符为15，无背景[和刷屏颜色一致],白色字体，水平左对齐，垂直居中] 写字指令
#define CMD_DRAW_TEXT_15x30 "xstr %d,%d,%d,30,0,WHITE,BLACK,0,1,3,\"%s\""
//  进行反显[因为TJC没有反显指令]
#define CMD_DRAW_TEXT_15x30_REVERSE "xstr %d,%d,%d,30,0,BLACK,WHITE,0,1,1,\"%s\""    

/**
 * @brief [修改] 发送一个完整格式的指令到串口屏
 */
static void send_command(const char *cmd_str) {

    //判断要发送的数据是否为空
    if (cmd_str == NULL || strlen(cmd_str) == 0) 
    {
        fprintf(stderr, "DRIVER: ERROR! Empty command string!\n");
        return;
    }
    
    // (缓冲区用于构建完整的数据包，以便一次性发送)
    // TJC发送的是 指令(字符串) + 0XFF 0XFF 0XFF 结尾的十六进制数据
    uint8_t packet_buf[128]; // (应足够大以容纳最大数据包)
    uint16_t packet_len = 0;

    // 1. 获取业务指令的实际长度
    uint16_t cmd_len = strlen(cmd_str);
    // 2. 检查缓冲区是否能容纳「指令+帧尾」
    uint16_t required_space = cmd_len + sizeof(FRAME_FOOTER);
    if (required_space <= sizeof(packet_buf)) {
        // 拷贝业务指令到数据包
        memcpy(packet_buf + packet_len, cmd_str, cmd_len);
        packet_len += cmd_len;
    } else {
        fprintf(stderr, "DRIVER: ERROR! Command too large for buffer!\n");
        return;
    }

    // 3. 写入淘晶驰固定帧尾（3个0xFF）
    memcpy(packet_buf + packet_len, FRAME_FOOTER, sizeof(FRAME_FOOTER));
    packet_len += sizeof(FRAME_FOOTER);

    // 4. 一次性发送完整数据包（指令+帧尾）
    platform_uart_send(packet_buf, packet_len);
}

/**
 * @brief [重写] 清除屏幕
 */
static void my_screen_clear(void) {
    // 发送 0x22 清屏指令，数据长度为 0
    send_command(CMD_CLEAR_SCREEN);
    printf("DRIVER: Screen Cleared (Cmd 0x%02X).\n", CMD_CLEAR_SCREEN);
}

/**
 * @brief [重写] 初始化屏幕
 */
static void my_screen_init(void) {
    platform_uart_init();
    // (可选) 在启动时清屏
    my_screen_clear();
    printf("DRIVER: Screen Initialized (using custom protocol).\n");
}

/**
 * @brief [重写] 绘制标准文本 (8x16)
 */
static void my_screen_draw_text(uint8_t x, uint8_t y, const char* str) {
    //判断要发送的数据是否为空
    if (str == NULL || strlen(str) == 0) 
    {
        fprintf(stderr, "DRIVER: ERROR! Empty command string!\n");
        return;
    }

    // 使用一个缓冲区来构建数据负载
    char cmd_draw_buf[80] = {0}; // (应足够大)

    uint16_t str_len = strlen(str);
    uint16_t w_pixel = FONT_WIDTH * str_len;

    int fmt_result = snprintf(cmd_draw_buf, sizeof(cmd_draw_buf) - 1, CMD_DRAW_TEXT_15x30, x, y, w_pixel, str);

    // 求的是一个完整的数据长度
    uint16_t data_len = strlen(cmd_draw_buf);

    // 检查格式化是否成功（是否缓冲区不足、格式化错误）
    if (fmt_result < 0 || (uint16_t)fmt_result >= sizeof(cmd_draw_buf)) 
    {
        fprintf(stderr, "DRIVER: ERROR! Draw text buffer overflow or format error!\n");
        return;
    }
    // 发送格式化后的文本绘制指令（无需else，前面判断不通过已return）
    send_command(cmd_draw_buf);
    printf("DRIVER: DrawText at (X:%d, Y:%d): '%s'\n", x, y, str);
}

/**
 * @brief [重写] 绘制高亮 (反显) 文本 (8x16)
 */
static void my_screen_draw_highlighted_text(uint8_t x, uint8_t y, const char* str) {
   // 保留原有参数校验逻辑
    if (str == NULL || strlen(str) == 0) 
    {
        fprintf(stderr, "DRIVER: ERROR! Empty text string!\n");
        return;
    }

    char cmd_draw_buf[80] = {0};
    uint16_t str_len = strlen(str);
    uint16_t w_pixel = FONT_WIDTH * str_len; (void)w_pixel; // 抑制未使用警告

    // 关键：使用反显指令宏进行格式化（交换了颜色）
    int fmt_result = snprintf(cmd_draw_buf, sizeof(cmd_draw_buf) - 1, 
                              CMD_DRAW_TEXT_15x30_REVERSE, x, y, w_pixel, str);

    // 保留原有缓冲区溢出判断
    if (fmt_result < 0 || (uint16_t)fmt_result >= sizeof(cmd_draw_buf)) {
        fprintf(stderr, "DRIVER: ERROR! Draw text reverse buffer overflow!\n");
        return;
    }

    // 发送反显指令
    send_command(cmd_draw_buf);
    printf("DRIVER: DrawText REVERSE at (X:%d, Y:%d): '%s'\n", x, y, str);
}

/**
 * @brief [重写] 绘制窗口 (简化为清屏和标题)
 */
static void my_screen_draw_window(const char* title) {
    my_screen_clear();
    
    char title_buffer[32]; 
    snprintf(title_buffer, sizeof(title_buffer), "[ %s ]", title);
    
    // 在 (0,0) 绘制标题
    my_screen_draw_text(0, 0, title_buffer);
    
    printf("DRIVER: DrawWindow with Title: '%s'\n", title);
}

/**
 * @brief [不变] 串口屏无缓冲区，刷新为空
 */
static void my_screen_refresh(void) {
    // 串口屏是即时模式，无需刷新
}


// =========================================================================
// 3. 构造并暴露驱动实例 (不变)
// =========================================================================
const display_driver_t g_my_display_driver = {
    .init = my_screen_init,
    .clear = my_screen_clear,
    .draw_text = my_screen_draw_text,
    .draw_highlighted_text = my_screen_draw_highlighted_text,
    .draw_window = my_screen_draw_window,
    .refresh = my_screen_refresh
};

const display_driver_t* get_my_screen_driver(void) {
    return &g_my_display_driver;
}

