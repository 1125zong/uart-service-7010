#ifndef UI_MENU_SYSTEM_H
#define UI_MENU_SYSTEM_H

#include <stdint.h> // 用于 uint8_t 等类型
#include <stddef.h> // 用于 NULL

#include "key.h"
#define FONT_WIDTH 15 
#define SCREEN_WIDTH_CHARS (320 / FONT_WIDTH) // 21
#define NUM_LAN_PORTS 1
/*
 * ==========================================================================
 * 1. 驱动抽象层 (Display Driver Interface)
 * ==========================================================================
 * 这是您为了“兼容性”需要实现的部分。
 */
typedef struct {
    /**
     * @brief 初始化显示设备
     */
    void (*init)(void);

    /**
     * @brief 清除屏幕
     */
    void (*clear)(void);

    /**
     * @brief 在指定坐标绘制字符串 (标准字体)
     * @param x X 坐标
     * @param y Y 坐标
     * @param str 要绘制的字符串
     */
    void (*draw_text)(uint8_t x, uint8_t y, const char* str);
    
    /**
     * @brief 绘制高亮（反显）的文本，用于菜单选中项
     * @param x X 坐标
     * @param y Y 坐标
     * @param str 要绘制的字符串
     */
    void (*draw_highlighted_text)(uint8_t x, uint8_t y, const char* str);

    /**
     * @brief （可选）绘制标题栏或窗口框
     * @param title 标题字符串
     */
    void (*draw_window)(const char* title);
    
    /**
     * @brief 将内部缓冲区的内容“刷”到屏幕上
     * (如果您的屏幕驱动没有缓冲区，此函数可以为空)
     */
    void (*refresh)(void);

} display_driver_t;


/*
 * ==========================================================================
 * 2. UI 逻辑层 (State Machine Definitions)
 * ==========================================================================
 */



// --- 前向声明 ---
struct ui_manager_s;
struct ui_state_s;

// --- 状态（界面）定义 ---
typedef struct ui_state_s {
    /**
     * @brief [必需] 当进入此状态时调用（用于绘制界面）
     */
    void (*on_enter)(struct ui_manager_s* manager);

    /**
     * @brief [必需] 当此状态下发生按键时调用（用于处理逻辑）
     */
    void (*handle_input)(struct ui_manager_s* manager, key_event_t event);

    /**
     * @brief [可选] 当退出此状态时调用（用于清理） (可以为 NULL)
     */
    void (*on_exit)(struct ui_manager_s* manager);

    /**
     * @brief [可选] 指向父状态，方便“菜单”键实现“返回”功能 (可以为 NULL)
     */
    const struct ui_state_s* parent_state; 

} ui_state_t;


/*
 * ==========================================================================
 * 3. UI 管理器 (The "Module" Handle)
 * ==========================================================================
 */
typedef struct ui_manager_s {
    
    // --- 核心状态 ---
    const ui_state_t* current_state; 
    
    // --- 硬件抽象 ---
    const display_driver_t* display; 
    
    // --- 数据链接 ---
    /**
     * @brief 指向应用数据的指针 (例如，一个包含IP、MAC地址等的全局配置结构体)
     */
    void* user_data; 
    
    // --- 状态暂存数据 ---
    /**
     * @brief 供状态内部使用的数据（例如：菜单的当前选中项索引）
     */
    uint8_t current_selection; 

    /**
     * @brief 供状态内部使用的通用暂存缓冲区
     */
    char text_buffer[64]; 

} ui_manager_t;


/*
 * ==========================================================================
 * 4. 模块核心 API (Function Prototypes)
 * ==========================================================================
 */

/**
 * @brief 初始化 UI 管理器
 */
void ui_init(ui_manager_t* manager, 
             const display_driver_t* driver, 
             void* user_data, 
             const ui_state_t* initial_state);

/**
 * @brief 核心函数：向 UI 模块推送一个按键事件
 */
void ui_handle_input(ui_manager_t* manager, key_event_t event);

/**
 * @brief 用于在应用数据更新时，强制重绘当前界面
 */
void ui_refresh(ui_manager_t* manager);

/**
 * @brief 状态机核心：切换到新状态
 */
void ui_set_state(ui_manager_t* manager, const ui_state_t* new_state);


#endif // UI_MENU_SYSTEM_H
