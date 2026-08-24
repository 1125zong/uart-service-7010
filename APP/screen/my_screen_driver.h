#ifndef MY_SCREEN_DRIVER_H
#define MY_SCREEN_DRIVER_H

#include "ui_menu_system.h"

/**
 * @brief [修正] 这是修复您编译错误的头文件。
 * 它只声明 main.c 需要的 "public" 函数。
 * 所有内部函数 (my_screen_clear 等) 都 *不* 应该在这里声明。
 */

// 公开的API (用于 main.c)
const display_driver_t* get_my_screen_driver(void);

/**
 * @brief [新增] 关闭串口 (用于 main.c 的 atexit)
 */
void platform_uart_close(void);

/**
 * @brief 旋转屏幕方向
 */
void my_screen_rotate(void);

#endif // MY_SCREEN_DRIVER_H

