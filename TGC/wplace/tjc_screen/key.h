#ifndef __KEY_H
#define __KEY_H

extern volatile void* pl_led_base_addr;

// 按键硬件地址定义
#define     KEY_MENU_ADDR     	(*(volatile unsigned int *)(pl_led_base_addr+0x20))
#define     KEY_UP_ADDR       	(*(volatile unsigned int *)(pl_led_base_addr+0x24))
#define     KEY_CONFIRM_ADDR    (*(volatile unsigned int *)(pl_led_base_addr+0x28))
#define     KEY_DOWN_ADDR  		(*(volatile unsigned int *)(pl_led_base_addr+0x2C))


#define     KEY_RESET  			(*(volatile unsigned int *)(pl_led_base_addr+0x400))

// --- 按键事件定义 ---
typedef enum {
    KEY_EVENT_NONE,
    KEY_EVENT_MENU,    // 菜单键（通常用于返回/退出）
    KEY_EVENT_UP,      // 上
    KEY_EVENT_DOWN,    // 下
    KEY_EVENT_CONFIRM,  // 确认/进入
	KEY_EVENT_RESET  // 确认/进入
} key_event_t;

key_event_t my_keypad_get_event(void);
void *KeyProcessTask(void *arg);
void stop_key_process_task(void);

#endif // !__KEY_H
