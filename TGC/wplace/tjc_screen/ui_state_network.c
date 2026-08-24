#include "ui_state_network.h"
#include <stdio.h>
#include <string.h>      // 用于 strcpy, strlen
#include <ctype.h>       // 用于 isdigit
#include "ui_menu_system.h"
#include "app_data.h" 
#include "ui_state_menu_list.h" // 用于父状态
#include "ui_state_restart.h"      // 用于修改后跳转

// =========================================================================


// --- 内部状态定义 ---
typedef enum {
    NET_MODE_LAN_SELECT, 
    NET_MODE_SETTINGS_LIST,
    NET_MODE_DISPLAY_ITEM,
    NET_MODE_EDIT_IP_CONFIG, // DHCP/Static 切换
    NET_MODE_EDIT_IP_GENERIC // IP/Mask/Gateway 数字编辑
} net_mode_t;

// --- 内部状态变量 ---
static net_mode_t current_mode;
static int current_lan_port = 0; 
static int current_edit_target_index = 0; // 跟踪正在编辑/显示的项

// --- 编辑器专用变量 ---
static char edit_buffer[17]; // (15 IP chars + 1 null + 1 extra)
static int edit_cursor_pos = 0; // (IP: 0-14, Config: 0-1)


// --- 菜单项定义 ---
#if (NUM_LAN_PORTS > 1)
static const char* lan_select_items[] = {"LAN1", "LAN2", "Exit"};
static const int num_lan_select_items = 3;
#endif
static const char* settings_list_items[] = {
    "Ethernet Status", // 0
    "MAC Address",     // 1
    "IP Address",      // 2
    "IP Config",       // 3
    "Network Mask",    // 4
    "Gateway",         // 5
    "Exit"             // 6
};
static const int num_settings_list_items = 7;
// 屏幕高度（像素）
#define SCREEN_HEIGHT 240
// 每个菜单项的高度（像素）
#define ITEM_HEIGHT 30
// 标题栏的高度（像素）
#define TITLE_HEIGHT 30
// 每页最多显示的项目数 = (屏幕高度 - 标题高度) / 每个项目高度
// 240-30=210, 210/30=7，所以每页最多显示7个项目
#define MAX_ITEMS_PER_PAGE ((SCREEN_HEIGHT - TITLE_HEIGHT) / ITEM_HEIGHT) // 每页最多显示的项目数

/**
 * @brief 将UI配置同步到全局变量g_system_config
 * @param lan_port_idx LAN端口索引
 * @param lan_port_data LAN端口数据
 */
// static void sync_ui_config_to_global(int lan_port_idx, lan_port_t* lan_port_data)
// {
//     // 使用互斥锁保护全局配置的访问
//     if(semTake(g_config_mutex, WAIT_FOREVER) == OK)
//     {
//         // 更新IP配置模式
//         g_system_config.device.ip_config_mode[lan_port_idx] = 
//             (lan_port_data->ip_config == IP_CONFIG_DHCP) ? IP_MODE_DHCP : IP_MODE_STATIC;
        
//         // 将IP地址字符串转换为网络字节序
//         g_system_config.device.ip_address[lan_port_idx] = inet_addr(lan_port_data->ip_address);
//         if(lan_port_idx == 0) {
//             g_system_config.device.ip_address1 = inet_addr(lan_port_data->ip_address);
//         } else {
//             g_system_config.device.ip_address2 = inet_addr(lan_port_data->ip_address);
//         }
        
//         // 将子网掩码字符串转换为网络字节序
//         g_system_config.device.netmask[lan_port_idx] = inet_addr(lan_port_data->network_mask);
        
//         // 将网关字符串转换为网络字节序
//         g_system_config.device.gateway[lan_port_idx] = inet_addr(lan_port_data->gateway);
        
//         semGive(g_config_mutex);
//     }
// }

// =========================================================================
// 编辑器绘制函数
// =========================================================================

/**
 * @brief 绘制 "IP Config" (DHCP/Static) 编辑界面
 */
static void draw_edit_ip_config_page(ui_manager_t* manager)
{
    manager->display->clear();
    
    // 屏幕宽度
    #define SCREEN_WIDTH 320
    
    // 绘制标题（水平居中）
    const char* title = settings_list_items[current_edit_target_index];
    int title_len = strlen(title);
    int title_width = title_len * FONT_WIDTH;
    int title_x = (SCREEN_WIDTH - title_width) / 2;
    manager->display->draw_text(title_x, 60, title); // "IP Config"

    // 绘制 DHCP/Static 选项（水平居中，垂直居中）
    const char* dhcp_text = "DHCP";
    const char* static_text = "Static";
    int dhcp_len = strlen(dhcp_text);
    int static_len = strlen(static_text);
    int dhcp_width = dhcp_len * FONT_WIDTH;
    int static_width = static_len * FONT_WIDTH;
    int dhcp_x = (SCREEN_WIDTH - dhcp_width) / 2;
    int static_x = (SCREEN_WIDTH - static_width) / 2;
    
    // edit_cursor_pos: 0 = DHCP, 1 = Static
    if (edit_cursor_pos == 0) {
        manager->display->draw_highlighted_text(dhcp_x, 110, ">DHCP");
        manager->display->draw_text(static_x, 150, " Static");
    } else {
        manager->display->draw_text(dhcp_x, 110, " DHCP");
        manager->display->draw_highlighted_text(static_x, 150, ">Static");
    }
    manager->display->refresh();
}

/**
 * @brief (辅助函数) 仅刷新 IP 编辑器中高亮的字符
 */
static void refresh_edit_ip_generic_char(ui_manager_t* manager)
{
    if (edit_cursor_pos < 0) edit_cursor_pos = 0;
    if (edit_cursor_pos >= (int)strlen(edit_buffer)) edit_cursor_pos = strlen(edit_buffer) - 1;
    while (edit_buffer[edit_cursor_pos] == '.') { edit_cursor_pos++; }
    
    char flash_char_str[2];
    flash_char_str[0] = edit_buffer[edit_cursor_pos];
    flash_char_str[1] = '\0';
    
    // 屏幕宽度
    #define SCREEN_WIDTH 320
    
    // 计算 IP 地址的总宽度
    int ip_len = strlen(edit_buffer);
    int ip_width = ip_len * FONT_WIDTH;
    int ip_x = (SCREEN_WIDTH - ip_width) / 2;
    
    uint8_t x_pixel = ip_x + (edit_cursor_pos * FONT_WIDTH);
    
    manager->display->draw_highlighted_text(x_pixel, 120, flash_char_str);
    // manager->display->refresh();
}




/**
 * @brief 完整重绘 IP 编辑器 (清除屏幕)
 */
static void draw_edit_ip_generic_page_full(ui_manager_t* manager)
{
    manager->display->clear();
    
    // 屏幕宽度
    #define SCREEN_WIDTH 320
    
    // 绘制标题（水平居中）
    const char* title = settings_list_items[current_edit_target_index];
    int title_len = strlen(title);
    int title_width = title_len * FONT_WIDTH;
    int title_x = (SCREEN_WIDTH - title_width) / 2;
    manager->display->draw_text(title_x, 60, title);
    
    // 绘制 IP 地址（水平居中，垂直居中）
    int ip_len = strlen(edit_buffer);
    int ip_width = ip_len * FONT_WIDTH;
    int ip_x = (SCREEN_WIDTH - ip_width) / 2;
    manager->display->draw_text(ip_x, 120, edit_buffer);
    
    refresh_edit_ip_generic_char(manager);
}


// =========================================================================
// 只读和列表绘制函数
// =========================================================================

/**
 * @brief [已修改] 绘制“只读项”界面
 * (Ethernet Status, MAC Address, IP Config)
 */
static void draw_display_item_page(ui_manager_t* manager)
{
    app_config_t* config = (app_config_t*)manager->user_data;
    // 获取当前选定 LAN 口的数据
    lan_port_t* lan = &config->lan_ports[current_lan_port];
    
    manager->display->clear();
    
    // 屏幕宽度
    #define SCREEN_WIDTH 320

    // 1. 绘制标题 (即菜单项的名称) - 水平居中
    if (NUM_LAN_PORTS > 1) {
        snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
                 "LAN%d: %s", 
                 current_lan_port + 1,
                 settings_list_items[current_edit_target_index]);
    } else {
        snprintf(manager->text_buffer, sizeof(manager->text_buffer), 
                 "%s", 
                 settings_list_items[current_edit_target_index]);
    }
    int title_len = strlen(manager->text_buffer);
    int title_width = title_len * FONT_WIDTH;
    int title_x = (SCREEN_WIDTH - title_width) / 2;
    manager->display->draw_text(title_x, 60, manager->text_buffer);

    // 2. 根据索引绘制不同的数据 - 水平居中
    const char* data_to_display = NULL;
    
    switch (current_edit_target_index) 
    {
        case 0: // "Ethernet Status"
            data_to_display = lan->ethernet_status;
            break;
            
        case 1: // "MAC Address"
            data_to_display = lan->mac_address;
            break;

        // [FIX] 添加 DHCP 模式下的只读 IP/Mask/Gateway
        case 2: // "IP Address"
            data_to_display = lan->ip_address;
            break;
            
        case 3: // "IP Config"
            data_to_display = (lan->ip_config == IP_CONFIG_DHCP) ? "DHCP" : "Static";
            break;
            
        case 4: // "Network Mask"
             data_to_display = lan->network_mask;
             break;
             
        case 5: // "Gateway"
             data_to_display = lan->gateway;
             break;
    }
    
    if (data_to_display) {
        int data_len = strlen(data_to_display);
        int data_width = data_len * FONT_WIDTH;
        int data_x = (SCREEN_WIDTH - data_width) / 2;
        manager->display->draw_text(data_x, 120, data_to_display); // 垂直居中显示
    }

    // 3. 绘制提示 - 水平居中
    const char* hint_text = "(Menu/OK: Back)";
    int hint_len = strlen(hint_text);
    int hint_width = hint_len * FONT_WIDTH;
    int hint_x = (SCREEN_WIDTH - hint_width) / 2;
    manager->display->draw_text(hint_x, 180, hint_text); // 底部显示
    manager->display->refresh();
}


static void draw_settings_list_page(ui_manager_t* manager)
{
    manager->display->clear();
    
    // 绘制标题（左对齐）
    if (NUM_LAN_PORTS > 1) {
        snprintf(manager->text_buffer, sizeof(manager->text_buffer), "[LAN%d]", current_lan_port + 1);
    } else {
        snprintf(manager->text_buffer, sizeof(manager->text_buffer), "[NetWork Setting]");
    }
    manager->display->draw_text(30, 30, manager->text_buffer);

    // 菜单列表的Y轴偏移量（标题下方）
    #define SETTINGS_LIST_Y_OFFSET 60 
    
    // 计算当前页码：当前选择的索引 / 每页项目数
    int current_page = manager->current_selection / MAX_ITEMS_PER_PAGE;
    
    // 计算当前页的起始索引：当前页码 * 每页项目数
    int start_index = current_page * MAX_ITEMS_PER_PAGE;
    
    // 计算当前页的结束索引：起始索引 + 每页项目数
    int end_index = start_index + MAX_ITEMS_PER_PAGE;
    
    // 确保结束索引不超过总项目数
    if (end_index > num_settings_list_items) end_index = num_settings_list_items;

    // 检查当前选中的是否是 Exit 选项
    int is_exit_selected = (manager->current_selection == num_settings_list_items - 1);
    
    // 绘制当前页的项目
    if (is_exit_selected) {
        // 如果选中的是 Exit，只显示标题和 Exit（左对齐）
        int display_line = SETTINGS_LIST_Y_OFFSET;
        snprintf(manager->text_buffer, sizeof(manager->text_buffer), ">%s", settings_list_items[num_settings_list_items - 1]);
        manager->display->draw_highlighted_text(30, display_line, manager->text_buffer);
    } else {
        // 否则显示正常项目（不包括 Exit）
        for (int i = start_index; i < end_index; i++) 
        {
            // 跳过 Exit 选项
            if (i == num_settings_list_items - 1) continue;
            
            // 计算当前项目的Y坐标：(当前项目在页内的索引) * 项目高度 + Y轴偏移量
            int display_line = ((i - start_index) * ITEM_HEIGHT) + SETTINGS_LIST_Y_OFFSET; 
            
            const char* item_text = settings_list_items[i];
            
            // 如果是当前选中的项目，绘制高亮文本（左对齐）
            if (i == manager->current_selection) {
                snprintf(manager->text_buffer, sizeof(manager->text_buffer), ">%s", item_text);
                manager->display->draw_highlighted_text(30, display_line, manager->text_buffer);
            } else {
                // 否则绘制普通文本（左对齐）
                snprintf(manager->text_buffer, sizeof(manager->text_buffer), " %s", item_text);
                manager->display->draw_text(30, display_line, manager->text_buffer);
            }
        }
    }
    
    // 刷新屏幕
    manager->display->refresh();
}

#if (NUM_LAN_PORTS > 1)
static void draw_lan_select_page(ui_manager_t* manager)
{
    manager->display->clear();
    
    // 屏幕宽度
    #define SCREEN_WIDTH 320
    
    // 绘制标题（水平居中）
    const char* title = "NetWork Setting";
    int title_len = strlen(title);
    int title_width = title_len * FONT_WIDTH;
    int title_x = (SCREEN_WIDTH - title_width) / 2;
    manager->display->draw_text(title_x, 30, title);
    
    // 绘制 LAN 选择项目（水平居中）
    for (int i = 0; i < num_lan_select_items; i++) 
    {
        int display_line = 60 + (i * 30);
        const char* item_text = lan_select_items[i];
        if (i == manager->current_selection) {
            snprintf(manager->text_buffer, sizeof(manager->text_buffer), ">%s", item_text);
            int text_len = strlen(manager->text_buffer);
            int text_width = text_len * FONT_WIDTH;
            int text_x = (SCREEN_WIDTH - text_width) / 2;
            manager->display->draw_highlighted_text(text_x, display_line, manager->text_buffer);
        } else {
            snprintf(manager->text_buffer, sizeof(manager->text_buffer), " %s", item_text);
            int text_len = strlen(manager->text_buffer);
            int text_width = text_len * FONT_WIDTH;
            int text_x = (SCREEN_WIDTH - text_width) / 2;
            manager->display->draw_text(text_x, display_line, manager->text_buffer);
        }
    }
    manager->display->refresh();
}
#endif


// =========================================================================
// 状态机核心函数 (on_enter, on_exit, handle_input)
// =========================================================================

/*
 * 状态：NetWork Setting (on_enter)
 */
static void network_on_enter(ui_manager_t* manager) 
{
    switch (current_mode)
    {
        case NET_MODE_SETTINGS_LIST:
            draw_settings_list_page(manager);
            break;
        case NET_MODE_DISPLAY_ITEM:
            draw_display_item_page(manager);
            break;
        case NET_MODE_EDIT_IP_CONFIG:
            draw_edit_ip_config_page(manager);
            break;
        case NET_MODE_EDIT_IP_GENERIC:
            draw_edit_ip_generic_page_full(manager); 
            break;
            
#if (NUM_LAN_PORTS > 1)
        case NET_MODE_LAN_SELECT:
        default: 
            current_mode = NET_MODE_LAN_SELECT;
            current_lan_port = 0; 
            draw_lan_select_page(manager);
            break;
#else
        default: 
            current_mode = NET_MODE_SETTINGS_LIST;
            current_lan_port = 0; 
            draw_settings_list_page(manager);
            break;
#endif
    }
}

/*
 * 状态：NetWork Setting (handle_input)
 * [已修改] 修复了 NUM_LAN_PORTS == 1 时的编译错误
 */
static void network_handle_input(ui_manager_t* manager, key_event_t event) 
{
    app_config_t* config = (app_config_t*)manager->user_data;
    lan_port_t* lan = &config->lan_ports[current_lan_port];
    
    // --- 检查是否需要跳转到重启界面 ---
    int exit_network_menu = 0;

#if (NUM_LAN_PORTS > 1) 
    // [FIX 1] 将此块包裹在 #if 中
    if (current_mode == NET_MODE_LAN_SELECT) {
        if (event == KEY_EVENT_MENU) exit_network_menu = 1;
        if (event == KEY_EVENT_CONFIRM && manager->current_selection == (num_lan_select_items - 1)) exit_network_menu = 1; // "Exit"
    }
#endif
    
    if (current_mode == NET_MODE_SETTINGS_LIST) {
        if (event == KEY_EVENT_MENU) exit_network_menu = 1;
        if (event == KEY_EVENT_CONFIRM && manager->current_selection == (num_settings_list_items - 1)) exit_network_menu = 1; // "Exit"
    }
    
    if (exit_network_menu) {
        if (config->current_status == STATUS_REBOOT_REQUIRED) {
            ui_set_state(manager, &g_state_restart);
        } else {
            ui_set_state(manager, manager->current_state->parent_state);
        }
        return; // *** 退出函数 ***
    }
    // ------------------------------------

    switch (current_mode)
    {
        // --- IP/Mask/Gateway 数字编辑器 ---
        case NET_MODE_EDIT_IP_GENERIC:
            switch (event) {
                case KEY_EVENT_UP:
                    if (isdigit(edit_buffer[edit_cursor_pos])) {
                        edit_buffer[edit_cursor_pos] = (edit_buffer[edit_cursor_pos] == '9') ? '0' : edit_buffer[edit_cursor_pos] + 1;
                        config->current_status = STATUS_REBOOT_REQUIRED; 
                        refresh_edit_ip_generic_char(manager); 
                    }
                    break;
                case KEY_EVENT_DOWN:
                    if (isdigit(edit_buffer[edit_cursor_pos])) {
                        edit_buffer[edit_cursor_pos] = (edit_buffer[edit_cursor_pos] == '0') ? '9' : edit_buffer[edit_cursor_pos] - 1;
                        config->current_status = STATUS_REBOOT_REQUIRED; 
                        refresh_edit_ip_generic_char(manager); 
                    }
                    break;
                case KEY_EVENT_CONFIRM: // OK: 移动光标
                {
                    edit_cursor_pos++;
                    if (edit_buffer[edit_cursor_pos] == '.') edit_cursor_pos++; 
                    
                    if (edit_cursor_pos >= (int)strlen(edit_buffer)) {
                        // --- 编辑完成: 保存并退出 ---
                        if (current_edit_target_index == 2) strcpy(lan->ip_address, edit_buffer);
                        else if (current_edit_target_index == 4) strcpy(lan->network_mask, edit_buffer);
                        else if (current_edit_target_index == 5) strcpy(lan->gateway, edit_buffer);
                        
                        current_mode = NET_MODE_SETTINGS_LIST;
                        draw_settings_list_page(manager);
                    } else {
                        // 移动光标 (重绘整个界面)
                        draw_edit_ip_generic_page_full(manager);
                    }
                    break;
                }
                case KEY_EVENT_MENU: // Menu: 取消
                    current_mode = NET_MODE_SETTINGS_LIST;
                    draw_settings_list_page(manager);
                    break;
                default: break;
            }
            break; // 结束 NET_MODE_EDIT_IP_GENERIC

        // --- DHCP/Static 编辑器 ---
        case NET_MODE_EDIT_IP_CONFIG:
             switch (event) {
                case KEY_EVENT_UP:
                case KEY_EVENT_DOWN:
                    edit_cursor_pos = (edit_cursor_pos == 0) ? 1 : 0; // 切换 0/1
                    draw_edit_ip_config_page(manager);
                    break;
                case KEY_EVENT_CONFIRM: // OK: 保存
                    config->current_status = STATUS_REBOOT_REQUIRED; 
                    lan->ip_config = (edit_cursor_pos == 0) ? IP_CONFIG_DHCP : IP_CONFIG_STATIC;
                    current_mode = NET_MODE_SETTINGS_LIST;
                    draw_settings_list_page(manager);
                    break;
                case KEY_EVENT_MENU: // Menu: 取消
                    current_mode = NET_MODE_SETTINGS_LIST;
                    draw_settings_list_page(manager);
                    break;
                default: break;
            }
            break; // 结束 NET_MODE_EDIT_IP_CONFIG

        // --- 只读项 ---
        case NET_MODE_DISPLAY_ITEM:
            if (event == KEY_EVENT_MENU || event == KEY_EVENT_CONFIRM) {
                current_mode = NET_MODE_SETTINGS_LIST;
                draw_settings_list_page(manager);
            }
            break; // 结束 NET_MODE_DISPLAY_ITEM

        // --- 设置列表 ---
        case NET_MODE_SETTINGS_LIST:
            switch (event) {
                case KEY_EVENT_UP:
                    if (manager->current_selection == 0) manager->current_selection = num_settings_list_items - 1;
                    else manager->current_selection--;
                    draw_settings_list_page(manager);
                    break;
                case KEY_EVENT_DOWN:
                    if (manager->current_selection >= (num_settings_list_items - 1)) manager->current_selection = 0;
                    else manager->current_selection++;
                    draw_settings_list_page(manager);
                    break;
                case KEY_EVENT_CONFIRM: // OK: 进入子模式
                {
                    current_edit_target_index = manager->current_selection;
                    int sel = current_edit_target_index;
                    
                    if (sel == 0 || sel == 1) { // Status, MAC
                        current_mode = NET_MODE_DISPLAY_ITEM;
                        draw_display_item_page(manager);
                    }
                    else if (sel == 2 || sel == 4 || sel == 5) { // IP, Mask, Gateway
                        if (lan->ip_config == IP_CONFIG_STATIC) {
                            if (sel == 2) strcpy(edit_buffer, lan->ip_address);
                            else if (sel == 4) strcpy(edit_buffer, lan->network_mask);
                            else if (sel == 5) strcpy(edit_buffer, lan->gateway);
                            edit_cursor_pos = 0;
                            current_mode = NET_MODE_EDIT_IP_GENERIC;
                            draw_edit_ip_generic_page_full(manager);
                        } else {
                            // (DHCP 模式下, 这些是只读的)
                            current_mode = NET_MODE_DISPLAY_ITEM;
                            draw_display_item_page(manager);
                        }
                    }
                    else if (sel == 3) { // IP Config
                        edit_cursor_pos = (lan->ip_config == IP_CONFIG_DHCP) ? 0 : 1;
                        current_mode = NET_MODE_EDIT_IP_CONFIG;
                        draw_edit_ip_config_page(manager);
                    }
                    // (sel == 6 'Exit' 已在函数顶部处理)
                    break;
                }
                default: break;
            }
            break; // 结束 NET_MODE_SETTINGS_LIST

        // [FIX 2] 将 case 移到 #if 之外
        case NET_MODE_LAN_SELECT: 
#if (NUM_LAN_PORTS > 1)
            // --- LAN 选择列表 ---
             switch (event) {
                case KEY_EVENT_UP:
                    if (manager->current_selection == 0) manager->current_selection = num_lan_select_items - 1;
                    else manager->current_selection--;
                    draw_lan_select_page(manager);
                    break;
                case KEY_EVENT_DOWN:
                    if (manager->current_selection >= (num_lan_select_items - 1)) manager->current_selection = 0;
                    else manager->current_selection++;
                    draw_lan_select_page(manager);
                    break;
                case KEY_EVENT_CONFIRM: // OK: 进入
                    if (manager->current_selection == 0) { // LAN1
                        current_lan_port = 0;
                        current_mode = NET_MODE_SETTINGS_LIST;
                        manager->current_selection = 0; 
                        draw_settings_list_page(manager);
                    } else if (manager->current_selection == 1) { // LAN2
                        current_lan_port = 1;
                        current_mode = NET_MODE_SETTINGS_LIST;
                        manager->current_selection = 0; 
                        draw_settings_list_page(manager);
                    }
                    // (sel == 2 'Exit' 已在函数顶部处理)
                    break;
                default: break;
            }
#else
            // [FIX 2] 消除警告：
            // 当 NUM_LAN_PORTS == 1 时，此状态不应被进入
            // 如果意外进入，则恢复到列表模式
            printf("NETWORK: Error - entered LAN_SELECT mode with 1 LAN port.\n");
            current_mode = NET_MODE_SETTINGS_LIST; // 恢复
            draw_settings_list_page(manager);
#endif
            break; // 结束 NET_MODE_LAN_SELECT
    }
}


/*
 * 状态：NetWork Setting (on_exit)
 */
static void network_on_exit(ui_manager_t* manager)
{
    (void)manager; 
    
    // [重要] 确保下次进入此状态时，是从头开始
    
#if (NUM_LAN_PORTS > 1)
    current_mode = NET_MODE_LAN_SELECT;
#else
    current_mode = NET_MODE_SETTINGS_LIST;
#endif
    current_lan_port = 0;
    current_edit_target_index = 0;
    edit_cursor_pos = 0;
}


// 构造实例
const ui_state_t g_state_network = {
    .on_enter = network_on_enter,
    .handle_input = network_handle_input,
    .on_exit = network_on_exit, 
    .parent_state = &g_state_main_menu_list 
};

