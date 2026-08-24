#include "ui_state_lock.h"
#include <stdio.h>
#include "ui_menu_system.h"
#include "app_data.h"
#include "ui_state_menu_list.h" // 用于父状态
//#include"hal_log.h"
/*
 * 状态：LockPasswd (on_enter)
 * 职责：只读显示密码
 */
static void lock_on_enter(ui_manager_t* manager) 
{
    app_config_t* config = (app_config_t*)manager->user_data;
    
    manager->display->clear();
    
    // 屏幕宽度
    #define SCREEN_WIDTH 320
    #define SCREEN_HEIGHT 170
    
    // 1. 绘制标题（水平居中，垂直居中上方）
    const char* title = "LockPasswd";
    int title_len = strlen(title);
    int title_width = title_len * FONT_WIDTH;
    int title_x = (SCREEN_WIDTH - title_width) / 2;
    int title_y = 33; // 标题位置
    manager->display->draw_text(title_x, title_y, title);

    // 2. 绘制密码（水平居中，垂直居中）
    const char* password = config->lock_password;
    int password_len = strlen(password);
    int password_width = password_len * FONT_WIDTH;
    int password_x = (SCREEN_WIDTH - password_width) / 2;
    int password_y = 75; // 密码位置
    manager->display->draw_text(password_x, password_y, password);
    
    // 3. 绘制提示（水平居中，垂直居中下方）
    const char* hint = "(Menu/OK: Back)";
    int hint_len = strlen(hint);
    int hint_width = hint_len * FONT_WIDTH;
    int hint_x = (SCREEN_WIDTH - hint_width) / 2;
    int hint_y = 117; // 提示位置
    manager->display->draw_text(hint_x, hint_y, hint);
    
    manager->display->refresh();
}

/*
 * 状态：LockPasswd (handle_input)
 * 职责：按 Menu 或 OK 键返回
 */
static void lock_handle_input(ui_manager_t* manager, key_event_t event) 
{
    switch (event) {
        case KEY_EVENT_MENU:
        case KEY_EVENT_CONFIRM: // OK 键
            // 行为: 返回父菜单
            ui_set_state(manager, manager->current_state->parent_state);
            break;
            
        // 忽略 UP, DOWN
        case KEY_EVENT_UP:
        case KEY_EVENT_DOWN:
        default:
            break; 
    }
}

// 构造实例
const ui_state_t g_state_lock = {
    .on_enter = lock_on_enter,
    .handle_input = lock_handle_input,
    .on_exit = NULL,
    .parent_state = &g_state_main_menu_list // 父级是主菜单
};
// 刷新密码锁定界面
void refresh_password_locked_interface(void)
{
    //LOG_INFO("Enter Refresh password locked interface\n");
    ui_manager_t* manager = ui_get_manager();

    if(manager == NULL)
    {
       // LOG_ERROR("ui_manager_t is NULL\n");
        return;
    }
        
	if (manager != NULL && manager->current_state == &g_state_lock)
    {
        //LOG_INFO("Refresh password locked interface\n");
		lock_on_enter(manager);
	}
}