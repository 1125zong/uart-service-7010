#include "key.h"
#include <stdio.h>

#include <string.h>

// 添加UI系统相关的头文件
#include "ui_menu_system.h"
#include "my_screen_driver.h"
#include "app_data.h"
#include "ui_state_main.h"
#include "app_com.h"
#include "app_dev.h"
#include"hal_log.h"

#include <sys/reboot.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

// 声明外部函数和变量
ui_manager_t g_ui_manager;
app_config_t g_app_data;

static volatile uint8_t last_key_state = KEY_EVENT_NONE;

// 添加用于按键事件同步的互斥锁和条件变量
static pthread_mutex_t key_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t key_cond = PTHREAD_COND_INITIALIZER;

// 标志变量，用于通知线程退出
static volatile int key_thread_running = 1;

#ifdef TIMER
ACoreOs_id timer_id;
Timer_ID pTimerId;
#endif

void load_app_data(void);
void timer_start(void);
void timer_init(void);
void sysReboot(void);

// ---获得按键值 ---
key_event_t my_keypad_get_event(void) 
{
    int pressed_count = 0;
    key_event_t pressed_key = KEY_EVENT_NONE;
        
    // 检查各个按键状态，0表示按下，1表示未按下
    if (KEY_MENU_ADDR == 0) 
    {
    	printf("KEY_MENU_ADDR!\n");   	   	
        pressed_count++;           
		pressed_key = KEY_EVENT_MENU;     
    }
    
    if (KEY_UP_ADDR == 0 ) 
    {
    	printf("KEY_UP_ADDR!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_UP;
        
    }
    
    if (KEY_DOWN_ADDR == 0 ) 
    {
    	printf("KEY_DOWN_ADDR!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_DOWN;
        
    }
    
    if (KEY_CONFIRM_ADDR == 0 ) 
    {
    	printf("KEY_CONFIRM_ADDR!\n");
        pressed_count++;
        pressed_key = KEY_EVENT_CONFIRM;
        
    }
    
    if (KEY_RESET == 0 ) 
	{
		printf("KEY_RESET!\n");
		pressed_count++;
		pressed_key = KEY_EVENT_RESET;
		
	}
    
    //按键去抖和处理单次触发
    if(pressed_key != last_key_state)
    {
    	last_key_state = pressed_key;
    	if(pressed_key != KEY_EVENT_NONE)
    	{
    		//新按键按下，返回按键事件
    		return pressed_key;
    		
    	}
    	else
    	{
    		//按键释放，不返回事件
    		return KEY_EVENT_NONE;
    	}
    }
    
    // 按键状态未变化，不返回事件(防止重复触发)
    return KEY_EVENT_NONE;
}

// 用于检测按键状态变化的辅助函数
static key_event_t check_key_state_change(void)
{
    static volatile uint8_t prev_key_state = KEY_EVENT_NONE;
    key_event_t event = my_keypad_get_event();
    
    // 检测按键状态变化
    if (event != prev_key_state) 
    {
        prev_key_state = event;
        return event;
    }
    
    return KEY_EVENT_NONE;
}

// 停止按键处理任务的函数
void stop_key_process_task(void)
{
    key_thread_running = 0;
}

/**
 * @brief 按键处理任务
 * @details 该任务负责检测硬件按键状态并将其传递给UI系统
 */

void* KeyProcessTask(void *arg)
{
    int id = *(int *)arg;
    LOG_INFO("Key processing task started, id=%d\n", id);

    int ret = pthread_setname_np(pthread_self(), "KeyProcessTask");
    if(ret != 0)
    {
        LOG_ERROR("KeyProcessTask: pthread_setname_np failed: %s\n", strerror(ret));
        return NULL;
    }


    //初始化UI系统
    load_app_data();
    
    //初始化UI管理器
    ui_init(
        &g_ui_manager,                 // UI管理器实例
        get_my_screen_driver(),        // 屏幕驱动
        &g_app_data,                   // 应用数据
        &g_state_main                  // 初始状态
    );  
    
    // 主循环 - 优化的休眠机制
    while (key_thread_running) 
    {
        // 获取按键事件
        key_event_t event = check_key_state_change();
        
        // 如果有按键事件，传递给UI系统
        if (event != KEY_EVENT_NONE) 
        {
            printf("event: %d   last_key_state: %d\n", event, last_key_state);
            ui_handle_input(&g_ui_manager, event);
        }
        if(event == KEY_EVENT_RESET)
        {
            printf("event == KEY_EVENT_RESET!\n");
            
            taskDelay(10000);//1s
            sysReboot();
        }
        
        // 使用更长的休眠时间来降低CPU使用率，但保持合理的响应速度
        usleep(50000); // 休眠50毫秒
    }
    
    return NULL;
}



// 3. 填充您的应用数据 (示例)
void load_app_data(void)
{
    printf("MAIN: App data loaded.\n");
    
    // --- 通用设置 ---
    strcpy(g_app_data.model_name, g_system_config.device.model_name);
    strcpy(g_app_data.model, g_system_config.device.model_name); //
    
    // --- 状态 ---
    uint8_t current_status = g_system_config.device.lock_status;
    g_app_data.current_status = current_status; // e.g., Start in Lock state
    g_app_data.error_code = 1; // 仅在 STATUS_ERROR 时显示

    
    // [Device Info]
    snprintf(g_app_data.firmware_version, sizeof(g_app_data.firmware_version), "V%d.%d.%d.%d", 
             g_system_config.device.firmware_version[0], g_system_config.device.firmware_version[1], 
             g_system_config.device.firmware_version[2], g_system_config.device.firmware_version[3]);
    snprintf(g_app_data.serial_number, sizeof(g_app_data.serial_number), "%04d%02d%02d%06d", 
             g_system_config.device.serial_no.year, g_system_config.device.serial_no.month, 
             g_system_config.device.serial_no.day, g_system_config.device.serial_no.number);

    // [Lock Password]
    strcpy(g_app_data.lock_password, g_system_config.device.lock_password);
    
    // --- 网络数据 ---
    // --- LAN 1 (索引 0) ---
    strcpy(g_app_data.lan_ports[0].ethernet_status, (g_system_config.device.lan_state1 == 1) ? "100M/Link" : "No Link");
    snprintf(g_app_data.lan_ports[0].mac_address, sizeof(g_app_data.lan_ports[0].mac_address), 
             "%02X:%02X:%02X:%02X:%02X:%02X", 
             g_system_config.device.mac_address1[0], g_system_config.device.mac_address1[1], 
             g_system_config.device.mac_address1[2], g_system_config.device.mac_address1[3], 
             g_system_config.device.mac_address1[4], g_system_config.device.mac_address1[5]);
    snprintf(g_app_data.lan_ports[0].ip_address, sizeof(g_app_data.lan_ports[0].ip_address), 
             "%d.%d.%d.%d",           
             g_system_config.device.ip_address1 & 0xFF,       
             (g_system_config.device.ip_address1 >> 8) & 0xFF,
             (g_system_config.device.ip_address1 >> 16) & 0xFF,
             (g_system_config.device.ip_address1 >> 24) & 0xFF);


    snprintf(g_app_data.lan_ports[0].network_mask, sizeof(g_app_data.lan_ports[0].network_mask), 
             "%d.%d.%d.%d", 
             g_system_config.device.netmask[0] & 0xFF, 
             (g_system_config.device.netmask[0] >> 8) & 0xFF,
             (g_system_config.device.netmask[0] >> 16) & 0xFF, 
             (g_system_config.device.netmask[0] >> 24) & 0xFF);

    snprintf(g_app_data.lan_ports[0].gateway, sizeof(g_app_data.lan_ports[0].gateway), 
             "%d.%d.%d.%d", 
             g_system_config.device.gateway[0] & 0xFF, 
             (g_system_config.device.gateway[0] >> 8) & 0xFF, 
             (g_system_config.device.gateway[0] >> 16) & 0xFF,         
             (g_system_config.device.gateway[0] >> 24) & 0xFF);
    g_app_data.lan_ports[0].ip_config = (g_system_config.device.ip_config_mode[0] == 0x01) ? IP_CONFIG_STATIC : IP_CONFIG_DHCP; // [修改]

    // --- LAN 2 (索引 1) ---
    strcpy(g_app_data.lan_ports[1].ethernet_status, (g_system_config.device.lan_state2 == 1) ? "100M/Link" : "No Link");
    snprintf(g_app_data.lan_ports[1].mac_address, sizeof(g_app_data.lan_ports[1].mac_address), 
             "%02X:%02X:%02X:%02X:%02X:%02X", 
             g_system_config.device.mac_address2[0], g_system_config.device.mac_address2[1], 
             g_system_config.device.mac_address2[2], g_system_config.device.mac_address2[3], 
             g_system_config.device.mac_address2[4], g_system_config.device.mac_address2[5]);
    snprintf(g_app_data.lan_ports[1].ip_address, sizeof(g_app_data.lan_ports[1].ip_address), 
             "%d.%d.%d.%d",                      
             g_system_config.device.ip_address2 & 0xFF,
             (g_system_config.device.ip_address2 >> 8) & 0xFF, 
             (g_system_config.device.ip_address2 >> 16) & 0xFF, 
             (g_system_config.device.ip_address2 >> 24) & 0xFF);

    snprintf(g_app_data.lan_ports[1].network_mask, sizeof(g_app_data.lan_ports[1].network_mask), 
             "%d.%d.%d.%d", 
             g_system_config.device.netmask[1] & 0xFF, 
             (g_system_config.device.netmask[1] >> 8) & 0xFF, 
             (g_system_config.device.netmask[1] >> 16) & 0xFF, 
             (g_system_config.device.netmask[1] >> 24) & 0xFF);
    snprintf(g_app_data.lan_ports[1].gateway, sizeof(g_app_data.lan_ports[1].gateway), 
             "%d.%d.%d.%d", 
             g_system_config.device.gateway[1] & 0xFF, 
             (g_system_config.device.gateway[1] >> 8) & 0xFF, 
             (g_system_config.device.gateway[1] >> 16) & 0xFF, 
             (g_system_config.device.gateway[1] >> 24) & 0xFF);
    g_app_data.lan_ports[1].ip_config = (g_system_config.device.ip_config_mode[1] == 0x01) ? IP_CONFIG_STATIC : IP_CONFIG_DHCP; // [修改]
}


void sysReboot(void)
{
    // 同步数据到磁盘，确保数据不丢失
    sync();
    // 等待一段时间让sync完成
    sleep(1);
    // 执行系统重启
    printf("reboot...\n");
    reboot(RB_AUTOBOOT);

}



/**********************************TIMER*******************************************************/

#ifdef TIMER

void timer_service_func (ACoreOs_id timer_id, void *arg)
{    
	ACoreOs_timer_cancel(pTimerId);
}
//消抖

void timer_init(void)
{	
	ACoreOs_timer_create((ACoreOs_name)"timer1", &timer_id);
	printf("timer id: %x\n", *((UINT32 *)timer_id));
	
	ACoreOs_timer_get_id((ACoreOs_name)"timer1", &pTimerId);
	
	printf("pTimerId id: %x\n", *( (UINT32 *)pTimerId));

}

void timer_start(void)
{
	ACoreOs_timer_fire_after(pTimerId,100 *50, timer_service_func , NULL,  ACOREOS_TIMER_FOREVER ,  1);
}

void timer_test(void)

{	
	timer_init();
}

#endif
/**********************************TIMER*******************************************************/
