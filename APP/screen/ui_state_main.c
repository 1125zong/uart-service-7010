#include "ui_state_main.h"
#include "app_data.h"     // 1. 包含数据结构定义
#include <stdio.h>        // 2. 包含 sprintf/snprintf
#include <string.h>       // 3. 包含 strlen

// 3. (重要) 我们必须知道按键后要跳转到“哪里”
#include "ui_state_menu_list.h"

/*
 * 状态：主界面 (on_enter)
 * 职责：绘制上电状态屏
 * [已按新要求重写]
 */
static void main_on_enter(ui_manager_t* manager)
{
    // 1. 获取应用数据
    app_config_t* config = (app_config_t*)manager->user_data;
    
    // 2. 清理屏幕 (使用我们抽象的驱动)
    manager->display->clear();

    // 3. 绘制 Model (第 0 行)
    // 左侧: [model_name]
    int model_name_len = strlen(config->model_name);
    int text_width = model_name_len * FONT_WIDTH;
    int x_pos = (320 - text_width) / 2;
    snprintf(manager->text_buffer, sizeof(manager->text_buffer),
             "%s", config->model_name);
    manager->display->draw_text(x_pos, 10, manager->text_buffer);
    
    // 右侧: [model] (右对齐)
    const char* model_value = config->model;
    int value_len = strlen(model_value);
    int x_pos_px = (320 - value_len * FONT_WIDTH) / 2;  
    manager->display->draw_text(x_pos_px, 48, model_value);


    // 4. 绘制 IP 地址 (第 1 行)
#if (NUM_LAN_PORTS == 1)
    int IP_len = strlen(config->lan_ports[0].ip_address); // "IP: "
    printf("IP_len: %d\n", IP_len);
    int IP_width = IP_len * FONT_WIDTH;
    int IPx_pos = (320 - IP_width) / 2;
    snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
             "%s", config->lan_ports[0].ip_address);
    manager->display->draw_text(IPx_pos, 88, manager->text_buffer);
#elif (NUM_LAN_PORTS == 2)
    // [修改] IP1 和 IP2 不在同一行
    snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
             "IP1:%s", config->lan_ports[0].ip_address);
    // (确保不会溢出屏幕)
    manager->text_buffer[SCREEN_WIDTH_CHARS] = '\0'; 
    int ip1_len = strlen(manager->text_buffer);
    int ip1_width = ip1_len * FONT_WIDTH;
    int ip1_x = (320 - ip1_width) / 2;
    manager->display->draw_text(ip1_x, 88, manager->text_buffer);

    // snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
    //          "IP2:%s", config->lan_ports[1].ip_address);
    // // (确保不会溢出屏幕)
    // manager->text_buffer[SCREEN_WIDTH_CHARS] = '\0'; 
    // int ip2_len = strlen(manager->text_buffer);
    // int ip2_width = ip2_len * FONT_WIDTH;
    // int ip2_x = (320 - ip2_width) / 2;
    // manager->display->draw_text(ip2_x, 150, manager->text_buffer);
#endif

    // 5. 绘制 Status (第 2 行)
    // [修改] 使用新的 status enum
    const char* status_str_ptr = NULL; // 用于指向 "Normal", "Lock" 等
    
    switch(config->current_status) 
    {
        case STATUS_NORMAL:
            status_str_ptr = "Normal";
            break;
        case STATUS_LOCK:
            status_str_ptr = "Lock";
            break;
        case STATUS_REBOOT_REQUIRED:
            status_str_ptr = "Reboot"; // 提示用户重启
            break;
        case STATUS_ERROR:
            // "ERR[1]"
            snprintf(manager->text_buffer, sizeof(manager->text_buffer),
                     "Err[%d]", config->error_code);
            status_str_ptr = manager->text_buffer; // 指向 buffer
            break;
    }
    
    if (status_str_ptr) {
        int status_len = strlen(status_str_ptr);
        int status_width = status_len * FONT_WIDTH;
        int statusx_pos = (320 - status_width) / 2;
        manager->display->draw_text(statusx_pos, 128, status_str_ptr);  
    }
    // 6. 刷新屏幕（如果驱动需要）
    manager->display->refresh();
}


/*
 * 状态：主界面 (handle_input)
 * 职责：等待任意按键，然后进入“主菜单列表”
 */
static void main_handle_input(ui_manager_t* manager, key_event_t event)
{
    // [已重新启用]
    // 只要不是“无按键”事件
    if (event != KEY_EVENT_NONE)
    {
        // 切换到“主菜单列表”状态
        // (g_state_main_menu_list 是图中的 "主菜单" 列表)
        ui_set_state(manager, &g_state_main_menu_list);
    }
}


/*
 * 构造“主状态”实例
 */
const ui_state_t g_state_main = {
    .on_enter = main_on_enter,
    .handle_input = main_handle_input,
    .on_exit = NULL,      // 退出此状态时无需清理
    .parent_state = NULL  // 这是根状态，没有父级
};

void refresh_main_interface(void)
{
    //LOG_INFO("Enter Refresh password locked interface\n");
    extern ui_manager_t* ui_get_manager(void);
    ui_manager_t* manager = ui_get_manager();

    if(manager == NULL)
    {
       // LOG_ERROR("ui_manager_t is NULL\n");
        return;
    }
        
	if (manager != NULL && manager->current_state == &g_state_main)
    {
        //LOG_INFO("Refresh password locked interface\n");
		main_on_enter(manager);
	}
}