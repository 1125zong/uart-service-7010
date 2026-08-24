#include "ui_menu_system.h"
#include <stddef.h> // (确保 NULL 被定义)
#include "app_data.h"
extern ui_manager_t g_ui_manager;
extern app_config_t g_app_data;

/**
 * @brief 内部辅助函数：安全地调用 on_enter
 */
static void _ui_enter_state(ui_manager_t* manager, const ui_state_t* state) {
    if (manager && state && state->on_enter) {
        state->on_enter(manager);
    }
}

/**
 * @brief 内部辅助函数：安全地调用 on_exit
 */
static void _ui_exit_state(ui_manager_t* manager, const ui_state_t* state) {
    if (manager && state && state->on_exit) {
        state->on_exit(manager);
    }
}


/*
 * ==========================================================================
 * 4. 模块核心 API (实现)
 * ==========================================================================
 */

/**
 * @brief 初始化 UI 管理器
 */
void ui_init(ui_manager_t* manager, 
             const display_driver_t* driver, 
             void* user_data, 
             const ui_state_t* initial_state)
{
    // 1. 检查传入的指针是否有效
    if (!manager || !driver || !initial_state) {
        return;
    }

    // 2. 初始化管理器内部数据
    manager->display = driver;
    manager->user_data = user_data;
    manager->current_state = NULL; // 先设为NULL，ui_set_state 会处理
    manager->current_selection = 0;
    
    manager->text_buffer[0] = '\0';

    // 3. (重要) 初始化物理显示驱动
    if (manager->display->init) {
        manager->display->init();
    }
    
    // 4. 进入初始状态
    ui_set_state(manager, initial_state);
}

/**
 * @brief 状态机核心：切换到新状态
 */
void ui_set_state(ui_manager_t* manager, const ui_state_t* new_state)
{
    if (!manager || !new_state) {
        return;
    }

    // 1. 调用当前状态的 on_exit (如果存在)
    _ui_exit_state(manager, manager->current_state);

    // 2. 切换到新状态
    manager->current_state = new_state;

    // 3. 重置通用暂存器（新菜单默认从第0项开始）
    manager->current_selection = 0;

    // 4. 调用新状态的 on_enter (如果存在)
    _ui_enter_state(manager, manager->current_state);
}

/**
 * @brief 核心函数：向 UI 模块推送一个按键事件
 */
void ui_handle_input(ui_manager_t* manager, key_event_t event)
{
    if (!manager || !manager->current_state || !manager->current_state->handle_input) {
        // 如果当前状态没有定义输入处理函数，则不执行任何操作
        return;
    }
    
    // 将事件分发给当前状态的 handle_input 函数
    manager->current_state->handle_input(manager, event);
}

/**
 * @brief 强制重绘当前界面
 */
void ui_refresh(ui_manager_t* manager)
{
    if (!manager || !manager->current_state || !manager->current_state->on_enter) {
        // 如果当前状态没有定义 on_enter 函数，则无法重绘
        return;
    }
    
    // 简单地重新调用 on_enter 函数来重绘整个界面
    manager->current_state->on_enter(manager);
}
ui_manager_t* ui_get_manager(void)
{
    return &g_ui_manager;
}


//获取全局变量g_app_data
app_config_t* get_app_data(void)
{
    return &g_app_data;
}