#include "ui_state_menu_list.h"
#include <stdio.h>
#include "ui_menu_system.h"
#include "app_data.h" // [新增]
#include "ui_state_main.h"

// 导入所有子状态
#include "ui_state_device_info.h"
#include "ui_state_network.h"
#include "ui_state_lock.h"
#include "ui_state_restart.h"
#include "ui_state_factory_reset.h"


// --- 菜单定义 ---
static const char* menu_items[] = {
    "Device Info",      // 索引 0
    "NetWork Setting",  // 索引 1
    "LockPasswd",       // 索引 2
    "Restart",          // 索引 3
    "Factory Reset",    // 索引 4
    "Exit"              // 索引 5
};
//屏幕分辨率为320x240
static const int num_menu_items = 6;
#define MAX_ITEMS_PER_PAGE 240 // 屏幕最多显示7行


/*
 * 状态：主菜单列表 (on_enter)
 * [已按新要求修改]
 */
static void menu_list_on_enter(ui_manager_t* manager)
{
    // (app_config_t* config = (app_config_t*)manager->user_data;) // 不再需要
    
    manager->display->clear();  //先清屏
    
    // [修改] 始终显示标题，移除重启警告
    manager->display->draw_text(30, 30, "[Main Menu]");   //绘制文本
    
    
    // --- 绘制列表 (从第1行 Y=30 开始) ---
    #define MENU_LIST_Y_OFFSET 60
    // (7行总高(240) - 1行标题(30) = 6行(210))
    #define ITEMS_PER_PAGE_WITH_TITLE 210

    int current_page = manager->current_selection / (ITEMS_PER_PAGE_WITH_TITLE / 30);
    int start_index = current_page * (ITEMS_PER_PAGE_WITH_TITLE / 30);
    int end_index = start_index + (ITEMS_PER_PAGE_WITH_TITLE / 30);
    if (end_index > num_menu_items) end_index = num_menu_items;

    for (int i = start_index; i < end_index; i++) 
    {
        int display_line = ((i * 30) % ITEMS_PER_PAGE_WITH_TITLE) + MENU_LIST_Y_OFFSET; 
        const char* item_text = menu_items[i];
        
        if (i == manager->current_selection) {
            snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
                     ">%s", item_text);
            manager->display->draw_highlighted_text(30, display_line, manager->text_buffer);
        } else {
            snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
                     " %s", item_text);
            manager->display->draw_text(30, display_line, manager->text_buffer);
        }
    }
    
    manager->display->refresh();
}


/*
 * 状态：主菜单列表 (handle_input)
 */
static void menu_list_handle_input(ui_manager_t* manager, key_event_t event)
{
    switch (event) {
        case KEY_EVENT_UP:
            if (manager->current_selection == 0) {
                manager->current_selection = num_menu_items - 1;
            } else {
                manager->current_selection--;
            }
            menu_list_on_enter(manager); // 重绘
            break;
            
        case KEY_EVENT_DOWN:
            manager->current_selection++;
            if (manager->current_selection >= num_menu_items) {
                manager->current_selection = 0;
            }
            menu_list_on_enter(manager); // 重绘
            break;
            
        case KEY_EVENT_CONFIRM: // OK
        {
            // 根据选项跳转到不同状态
            const ui_state_t* next_state = NULL;
            switch (manager->current_selection) {
                case 0: // "Device Info"
                    next_state = &g_state_device_info;
                    break;
                case 1: // "NetWork Setting"
                    next_state = &g_state_network;
                    break;
                case 2: // "LockPasswd"
                    next_state = &g_state_lock;
                    break;
                case 3: // "Restart"
                    next_state = &g_state_restart;
                    break;
                case 4: // "Factory Reset"
                    next_state = &g_state_factory_reset;
                    break;
                case 5: // "Exit"
                    // "Exit" 的行为同 "Menu"
                    menu_list_handle_input(manager, KEY_EVENT_MENU);
                    return; // 提前返回
            }
            
            if (next_state) {
                ui_set_state(manager, next_state);
            }
            break;
        }
            
        case KEY_EVENT_MENU: // 菜单
        { 
            // (app_config_t* config = (app_config_t*)manager->user_data;) // 不再需要
            
            // [修改] 用户的要求是，无论状态如何，"Menu" 键都应返回主界面（上电界面）
            // 状态 (例如 "Reboot") 会显示在主界面上
            
            // if (config->current_status == STATUS_REBOOT_REQUIRED) {
            //     // 如果需要重启，Menu 键也强制跳转到重启确认
            //     ui_set_state(manager, &g_state_restart);
            // } else {
            //     // 否则, 正常返回上电界面 (父状态)
            //     ui_set_state(manager, manager->current_state->parent_state);
            // }
            
            // [新逻辑] 始终返回上电界面 (父状态)
            ui_set_state(manager, manager->current_state->parent_state);
            
            break;
        } 
            
        default:
            break; 
    }
}


// 构造实例
const ui_state_t g_state_main_menu_list = {
    .on_enter = menu_list_on_enter,
    .handle_input = menu_list_handle_input,
    .on_exit = NULL,
    .parent_state = &g_state_main 
};

