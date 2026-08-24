#include "ui_state_restart.h"
#include <stdio.h>
#include <unistd.h> // for sleep
#include "ui_menu_system.h"
#include "ui_state_main.h"      // 用于重启后返回
#include "ui_state_menu_list.h" // [新增] 用于选 "No" 时返回
#include "app_data.h"           // [新增] 用于访问 current_status

// #include"hal_log.h"
// #include "app_dev.h"

// #include "system_abstraction_layer.h"
extern void sysReboot(void);
// --- 菜单定义 ---
static const char* menu_items[] = {
    "No",  // 索引 0
    "Yes"  // 索引 1
};
static const int num_menu_items = 2;
// (我们把 "No" 作为默认项)
static int current_selection = 0; 


/**
 * @brief 辅助函数: 绘制 "Yes/No" 确认界面
 */
static void draw_confirm_page(ui_manager_t* manager)
{
    manager->display->clear();
    
    // 屏幕宽度和高度
    #define SCREEN_WIDTH 320
    #define SCREEN_HEIGHT 240
    
    // 绘制标题（水平居中，垂直居中上方）
    const char* title = "Restart ?";
    int title_len = strlen(title);
    int title_width = title_len * FONT_WIDTH;
    int title_x = (SCREEN_WIDTH - title_width) / 2;
    int title_y = 33; // 标题位置
    manager->display->draw_text(title_x, title_y, title);

    // 绘制选项（水平居中，垂直居中）
    for (int i = 0; i < num_menu_items; i++) 
    {
        int display_line = 75 + (i * 32); // 选项位置
        if (i == 1) 
        {
            display_line += 10;
        }
        if (i == current_selection) {
            snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
                     ">%s", menu_items[i]);
            int text_len = strlen(manager->text_buffer);
            int text_width = text_len * FONT_WIDTH;
            int text_x = (SCREEN_WIDTH - text_width) / 2;
            manager->display->draw_highlighted_text(text_x - 10, display_line, manager->text_buffer);
        } else {
            snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
                     " %s", menu_items[i]);
            int text_len = strlen(manager->text_buffer);
            int text_width = text_len * FONT_WIDTH;
            int text_x = (SCREEN_WIDTH - text_width) / 2;
            manager->display->draw_text(text_x - 10, display_line, manager->text_buffer);   
        }
    }
    manager->display->refresh();
}

/**
 * @brief 辅助函数: 绘制 "Waiting..." 界面
 */
static void draw_wait_page(ui_manager_t* manager)
{
    manager->display->clear();
    
    // 屏幕宽度
    #define SCREEN_WIDTH 320
    
    // 绘制 "Wait A Moment..." 文本（水平居中，垂直居中）
    const char* wait_text = "Wait A Moment...";
    int wait_len = strlen(wait_text);
    int wait_width = wait_len * FONT_WIDTH;
    int wait_x = (SCREEN_WIDTH - wait_width) / 2;
    int wait_y = 65; // 垂直居中位置
    manager->display->draw_text(wait_x, wait_y, wait_text);
    manager->display->refresh();
}


/*
 * 状态：Restart (on_enter)
 */
static void restart_on_enter(ui_manager_t* manager) 
{
    current_selection = 0; // 默认选中 "No"
    draw_confirm_page(manager);
}

/*
 * 状态：Restart (handle_input)
 * [已修改]
 */
static void restart_handle_input(ui_manager_t* manager, key_event_t event) 
{
    // [新增] 获取 app_config
    app_config_t* config = (app_config_t*)manager->user_data;

    switch (event) {
        case KEY_EVENT_UP:
        case KEY_EVENT_DOWN:
            current_selection = (current_selection == 0) ? 1 : 0; // 切换
            draw_confirm_page(manager);
            break;
            
        case KEY_EVENT_CONFIRM: // OK
            if (current_selection == 0) // 0 = "No"
            {
                // [修改] "No": 保持 current_status 标志 (如果存在), 返回主菜单
                ui_set_state(manager, &g_state_main_menu_list);
            } 
            else // 1 = "Yes"
            {
                // [修改] "Yes": 清除标志, 显示 "Waiting..."
                config->current_status = 0;
                
                draw_wait_page(manager);
				               //保存配置
                // if (dev_config_save() != OK) 
                // {
                //     LOG_ERROR("ERROR[reboot]: Failed to save configuration to flash.\n");
                // }

                // if (save_config() != OK) 
                // {
                //     LOG_ERROR("ERROR[reboot]: Failed to save configuration to flash.\n");
                // }
                
                // --- 模拟重启 ---
                printf("RESTART: Rebooting system now...\n");
                sleep(2); // 模拟 2 秒重启
				//sysReboot();
                printf("RESTART: System rebooted.\n");
                
                // 重启后, 返回到“上电界面”
                //ui_set_state(manager, &g_state_main);
            }
            break;
            
        case KEY_EVENT_MENU: // 菜单键 (同 "No")
            // [修改] "Menu": 保持 current_status 标志, 返回主菜单
            ui_set_state(manager, &g_state_main_menu_list);
            break;
            
        default:
            break; 
    }
}

// 构造实例
const ui_state_t g_state_restart = {
    .on_enter = restart_on_enter,
    .handle_input = restart_handle_input,
    .on_exit = NULL, 
    .parent_state = &g_state_main_menu_list 
};

