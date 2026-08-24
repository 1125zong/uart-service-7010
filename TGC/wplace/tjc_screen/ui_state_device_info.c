#include "ui_state_device_info.h"
#include <stdio.h>
#include <string.h>
#include "app_data.h"
#include "ui_menu_system.h" 

// 导入主菜单 (用于返回)
#include "ui_state_menu_list.h"

// --- [新增] 内部状态定义 ---
typedef enum {
    MODE_LIST,      // 正在显示菜单列表
    MODE_DISPLAY_ITEM // 正在显示一个只读项
} dev_info_mode_t;

// [新增] 用于管理内部状态的静态变量
static dev_info_mode_t current_mode = MODE_LIST;
// [新增] 记录当前正在查看哪个只读项 (对应 menu_items 的索引)
static int current_display_item_index = 0;


// --- 菜单定义 ---
static const char* menu_items[] = {
    "Server Name",
    "Firmware Version",
    "Model Name",
    "Serial number",
    "Exit"
};

//屏幕分辨率为 320x240
static const int num_menu_items = 5;
#define MAX_ITEMS_PER_PAGE 240 // 屏幕最多显示8行

// --- [新增] 绘制只读界面的辅助函数 ---
static void draw_display_page(ui_manager_t* manager, int item_index)
{
    app_config_t* config = (app_config_t*)manager->user_data;
    manager->display->clear();

    // 屏幕尺寸
    #define SCREEN_WIDTH 320
    #define SCREEN_HEIGHT 240

    // 1. 绘制标题 (即菜单项的名称) - 水平居中，垂直偏上
    int title_len = strlen(menu_items[item_index]);
    int title_width = title_len * FONT_WIDTH;
    int title_x = (SCREEN_WIDTH - title_width) / 2;
    int title_y = 60; // 垂直居中偏上
    manager->display->draw_text(title_x, title_y, menu_items[item_index]);

    // 2. 根据索引绘制不同的数据 - 水平居中，垂直居中
    int data_y = 105; // 垂直居中
    switch (item_index) 
    {
        case 0: // "Server Name"
            // 示例: QPort125S-65809
            snprintf(manager->text_buffer, sizeof(manager->text_buffer),
                     "%s-%s",
                     config->model_name,
                     config->serial_number);
            int server_name_len = strlen(manager->text_buffer);
            int server_name_width = server_name_len * FONT_WIDTH;
            int server_name_x = (SCREEN_WIDTH - server_name_width) / 2;
            manager->display->draw_text(server_name_x, data_y, manager->text_buffer);
            break;
            
        case 1: // "Firmware Version"
            int firmware_len = strlen(config->firmware_version);
            int firmware_width = firmware_len * FONT_WIDTH;
            int firmware_x = (SCREEN_WIDTH - firmware_width) / 2;
            manager->display->draw_text(firmware_x, data_y, config->firmware_version);
            break;
            
        case 2: // "Model Name"
            int model_len = strlen(config->model_name);
            int model_width = model_len * FONT_WIDTH;
            int model_x = (SCREEN_WIDTH - model_width) / 2;
            manager->display->draw_text(model_x, data_y, config->model_name);    
            break;
            
        case 3: // "Serial number"
            int serial_len = strlen(config->serial_number);
            int serial_width = serial_len * FONT_WIDTH;
            int serial_x = (SCREEN_WIDTH - serial_width) / 2;
            manager->display->draw_text(serial_x, data_y, config->serial_number);
            break;
    }

    // 3. 绘制提示 - 水平居中，垂直偏下
    const char* hint_text = "(Menu: Back)";
    int hint_len = strlen(hint_text);
    int hint_width = hint_len * FONT_WIDTH;
    int hint_x = (SCREEN_WIDTH - hint_width) / 2;
    int hint_y = 150; // 垂直居中偏下
    manager->display->draw_text(hint_x, hint_y, hint_text);
    manager->display->refresh();
}

// --- [新增] 绘制菜单列表的辅助函数 ---
static void draw_list_page(ui_manager_t* manager)
{
    manager->display->clear();
    
    int current_page = manager->current_selection / MAX_ITEMS_PER_PAGE;
    int start_index = current_page * MAX_ITEMS_PER_PAGE;
    
    int end_index = start_index + MAX_ITEMS_PER_PAGE;
    if (end_index > num_menu_items) {
        end_index = num_menu_items;
    }

    for (int i = start_index; i < end_index; i++) 
    {
        int display_line = (i * 30) % MAX_ITEMS_PER_PAGE + 30; 
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
 * 状态：Device Info (on_enter)
 * [已重写] 根据内部模式决定绘制哪个界面
 */
static void dev_info_on_enter(ui_manager_t* manager) 
{
    if (current_mode == MODE_LIST) {
        draw_list_page(manager);
    } else {
        draw_display_page(manager, current_display_item_index);
    }
}

/*
 * 状态：Device Info (handle_input)
 * [已重写] 根据内部模式决定如何处理按键
 */
static void dev_info_handle_input(ui_manager_t* manager, key_event_t event) 
{
    if (current_mode == MODE_LIST) 
    {
        // --- 我们在“列表模式” ---
        switch (event) {
            case KEY_EVENT_UP:
                if (manager->current_selection == 0) {
                    manager->current_selection = num_menu_items - 1;
                } else {
                    manager->current_selection--;
                }
                draw_list_page(manager); // 重绘列表
                break;
                
            case KEY_EVENT_DOWN:
                manager->current_selection++;
                if (manager->current_selection >= num_menu_items) {
                    manager->current_selection = 0;
                }
                draw_list_page(manager); // 重绘列表
                break;
                
            case KEY_EVENT_CONFIRM: // OK 键
                if (manager->current_selection == 4) // 索引 4 是 "Exit"
                {
                    // 行为: 退出到父菜单
                    ui_set_state(manager, manager->current_state->parent_state);
                } 
                else 
                {
                    // 行为: 进入“显示模式”
                    current_display_item_index = manager->current_selection;
                    current_mode = MODE_DISPLAY_ITEM;
                    draw_display_page(manager, current_display_item_index); // 绘制详情页
                }
                break;
                
            case KEY_EVENT_MENU: // 菜单键
                // 行为: 退出到父菜单
                ui_set_state(manager, manager->current_state->parent_state);
                break;
                
            default:
                break; 
        }
    } 
    else 
    {
        // --- 我们在“显示详情模式” ---
        // 在只读界面，只响应 "Menu" 键
        if (event == KEY_EVENT_MENU) 
        {
            // 行为: 返回“列表模式”
            current_mode = MODE_LIST;
            draw_list_page(manager); // 重绘列表
        }
    }
}

/*
 * [新增] 当我们退出 Device Info 状态时（例如返回主菜单）
 * 我们必须重置内部状态，以便下次进入时从列表开始。
 */
static void dev_info_on_exit(ui_manager_t* manager)
{
    (void)manager; // (避免 "unused parameter" 警告)
    current_mode = MODE_LIST;
    current_display_item_index = 0;
}


// 构造实例
const ui_state_t g_state_device_info = {
    .on_enter = dev_info_on_enter,
    .handle_input = dev_info_handle_input,
    .on_exit = dev_info_on_exit, // [重要] 添加 on_exit 清理函数
    .parent_state = &g_state_main_menu_list 
};

