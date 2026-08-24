#include "ui_state_factory_reset.h"
#include <stdio.h>    // 用于 printf (调试)
#include <unistd.h>   // 用于 sleep (Linux 测试)
#include "ui_menu_system.h"
#include "ui_state_menu_list.h" // 用于父状态
#include "ui_state_main.h"           // 用于重置后返回

/*
 * 状态：Factory Reset 确认 (on_enter)
 * 职责：绘制 "Yes/No" 选项, 默认选中 "No"
 */
static void factory_reset_on_enter(ui_manager_t* manager) 
{
    // 0 = "No", 1 = "Yes". 默认 0.
    
    manager->display->clear();
    
    // 屏幕宽度
    #define SCREEN_WIDTH 320
    
    // 1. 绘制标题（水平居中，垂直居中上方）
    const char* title = "Factory Reset";
    int title_len = strlen(title);
    int title_width = title_len * FONT_WIDTH;
    int title_x = (SCREEN_WIDTH - title_width) / 2;
    int title_y = 60; // 标题位置
    manager->display->draw_text(title_x, title_y, title);

    // 2. 绘制 "No" (索引 0)（水平居中，垂直居中）
    const char* no_text = (manager->current_selection == 0) ? ">No" : " No";
    int no_len = strlen(no_text);
    int no_width = no_len * FONT_WIDTH;
    int no_x = (SCREEN_WIDTH - no_width) / 2;
    int no_y = 120; // No 选项位置
    if (manager->current_selection == 0) {
        manager->display->draw_highlighted_text(no_x, no_y, no_text);
    } else {
        manager->display->draw_text(no_x, no_y, no_text);
    }
    
    // 3. 绘制 "Yes" (索引 1)（水平居中，垂直居中下方）
    const char* yes_text = (manager->current_selection == 1) ? ">Yes" : " Yes";
    int yes_len = strlen(yes_text);
    int yes_width = yes_len * FONT_WIDTH;
    int yes_x = (SCREEN_WIDTH - yes_width) / 2;
    int yes_y = 150; // Yes 选项位置
    if (manager->current_selection == 1) {
        manager->display->draw_highlighted_text(yes_x, yes_y, yes_text);
    } else {
        manager->display->draw_text(yes_x, yes_y, yes_text);
    }
    
    manager->display->refresh();
}

/*
 * 状态：Factory Reset 确认 (handle_input)
 * 职责：处理按键
 */
static void factory_reset_handle_input(ui_manager_t* manager, key_event_t event) 
{
    switch (event) {
        case KEY_EVENT_UP:
        case KEY_EVENT_DOWN:
            // 只有两个选项，上下键都是切换
            manager->current_selection = !manager->current_selection; // (0 变 1, 1 变 0)
            // 重绘
            manager->current_state->on_enter(manager);
            break;
            
        case KEY_EVENT_MENU: // 菜单键
            // 行为: 等同于选 "No", 返回父菜单
            ui_set_state(manager, manager->current_state->parent_state);
            break;
            
        case KEY_EVENT_CONFIRM: // OK 键
            if (manager->current_selection == 0) {
                // 选中 "No"
                ui_set_state(manager, manager->current_state->parent_state);
            } else {
                // 选中 "Yes"
                
                // 1. 显示 "Wait A Moment"（水平居中，垂直居中）
                manager->display->clear();
                const char* wait_text = "Wait A Moment...";
                int wait_len = strlen(wait_text);
                int wait_width = wait_len * FONT_WIDTH;
                int wait_x = (SCREEN_WIDTH - wait_width) / 2;
                int wait_y = 120; // 垂直居中位置
                manager->display->draw_text(wait_x, wait_y, wait_text);
                manager->display->refresh();
                
                // 2. [应用] 执行恢复出厂设置
                // (在 Linux 测试中, 我们打印日志并休眠)
                printf("APP: === FACTORY RESETTING NOW ===\n");
                sleep(2); // 模拟操作耗时
                
                // 3. (在真实硬件中，设备会重启或重新加载配置)
                // (在测试中，我们手动返回“上电界面”)
                ui_set_state(manager, &g_state_main);
            }
            break;
            
        default:
            break;
    }
}

// 构造实例
const ui_state_t g_state_factory_reset = {
    .on_enter = factory_reset_on_enter,
    .handle_input = factory_reset_handle_input,
    .on_exit = NULL, 
    .parent_state = &g_state_main_menu_list // 父级是主菜单
};

