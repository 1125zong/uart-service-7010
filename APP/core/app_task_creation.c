/*
 * =====================================================================================
 *
 * Filename:  app_task_creation.c
 *
 * Description:  应用程序任务创建模块
 * 负责创建所有应用程序任务
 *
 * =====================================================================================
 */
#include <pthread.h>
#include "app_com.h"
#include "app_udp_search.h"
#include "app_net_cfg.h"
#include "app_net_con.h"
#include "app_report.h"
#include "app_net_scheduler.h"
#include "key.h"
#include "app_init_modules.h"



#define REALTIME_SCHEDULER_PRIORITY   50
#define CONFIG_TASK_MANAGER_PRIORITY  60
#define CONN_MANAGER_PRIORITY         70
#define UDP_SEARCH_PRIORITY           75
#define REPORT_MANAGER_PRIORITY       80
#define KEY_PROCESS_PRIORITY          90  // 按键处理任务优先级，优先级低于其他任务

// 任务栈大小
#define DEFAULT_STACK_SIZE            (32 * 1024) // 32KB

#define NUM_THREADS 5  // 要创建的线程数量

pthread_t threads[NUM_THREADS];  // 存储线程ID的数组
int       thread_args[NUM_THREADS] = {0, 1, 2, 3, 4};    // 存储每个线程的编号参数

/* ------------------ Function Implementations ------------------ */

/**
 * @brief 创建配置任务管理器
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_config_task_manager(void)
{
    int ret;
    // g_config_task_manager_tid = taskSpawn("tConfigManager",
    //     CONFIG_TASK_MANAGER_PRIORITY, 0, DEFAULT_STACK_SIZE,
    //     (task_func) ConfigTaskManager, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);   
    // LOG_INFO("Config task manager tid: %d\n", g_config_task_manager_tid);

    //创建配置任务管理器线程
    ret = pthread_create(&threads[0], NULL, (void*)ConfigTaskManager, (void*)&thread_args[0]);
    if (ret!= 0)
    {
        LOG_ERROR("Failed to create config task manager thread\n", strerror(errno));
        return ERROR;
    }
    sleep(1);
    return OK;
}

/**
 * @brief 创建实时调度任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_realtime_scheduler_task(void)
{
    int ret;
    // g_realtime_scheduler_tid = taskSpawn("tRealTimeScheduler",
    //                                      REALTIME_SCHEDULER_PRIORITY,
    //                                      0, DEFAULT_STACK_SIZE,
    //                                      (task_func)RealTimeSchedulerTask,
    //                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    // LOG_INFO("Realtime scheduler tid: %d\n", g_realtime_scheduler_tid);
    
    ret = pthread_create(&threads[3], NULL, (void*)RealTimeSchedulerTask, (void*)&thread_args[3]);
    if (ret != 0)
    {
        LOG_ERROR("Failed to create realtime scheduler thread\n", strerror(errno));
        return ERROR;
    }
    

    return OK;
}

/**
 * @brief 创建UDP搜索任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_udp_search_task(void)
{
    
    // LOG_INFO("UDP search tid: %d\n", g_udp_search_tid);

    //创建UDP搜索线程
    int ret = pthread_create(&threads[1], NULL, (void*)UdpSearchTask, (void*)&thread_args[1]);
    if (ret!= 0)
    {
        LOG_ERROR("Failed to create UDP search thread\n", strerror(errno));
        return ERROR;
    }
    
    return OK;
}

/**
 * @brief 创建报告管理任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_report_manager_task(void)
{
    int ret;
    // g_report_manager_tid = taskSpawn("ReportManager",
    //                                 REPORT_MANAGER_PRIORITY,
    //                                 0, DEFAULT_STACK_SIZE,
    //                                 (task_func)ReportManagerTask,
    //                                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    // LOG_INFO("Report manager tid: %d\n", g_report_manager_tid);

    //创建上报线程
    ret = pthread_create(&threads[2], NULL, (void*)ReportManagerTask, (void*)&thread_args[2]);
    if (ret!= 0)
    {
        LOG_ERROR("Failed to create report manager thread\n", strerror(errno));
        return ERROR;
    }

    
    return OK;
}

/**
 * @brief 创建按键处理任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_key_process_task(void)
{
    int ret;
    // g_key_process_tid = taskSpawn("tKeyProcess",
    //                             KEY_PROCESS_PRIORITY,
    //                             0, DEFAULT_STACK_SIZE,
    //                             (task_func)KeyProcessTask,
    //                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0);   
    // LOG_INFO("Key process tid: %d\n", g_key_process_tid);
    
    //创建按键处理线程
    ret = pthread_create(&threads[4], NULL, (void*)KeyProcessTask, (void*)&thread_args[4]);
    if (ret != 0)
    {
        LOG_ERROR("Failed to create key process thread\n", strerror(errno));
        return ERROR;
    }

    return OK;
}

/**
 * @brief 创建应用程序任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_application_tasks(void)
{
    
    ConnectionManager_TaskStart();

    start_update_server(); 

    if (create_config_task_manager() != OK)
    {
        return ERROR;
    }
    
    if (create_udp_search_task() != OK) 
    {
        return ERROR;
    }
    
    if (create_report_manager_task() != OK) 
    {
        return ERROR;
    }
    
    if (create_realtime_scheduler_task() != OK)
	{
		return ERROR;
	}


#if 1   
   if (create_key_process_task() != OK) 
   {
       return ERROR;
   }

#endif    
    


    
    return OK;
}

/**
 * @brief 清理/回收所有应用程序任务
 * 在程序退出时调用，用于回收所有线程资源
 */
void cleanup_application_tasks(void)
{
    LOG_INFO("\r\n");
    LOG_INFO("Cleaning up application tasks...\n");
    LOG_INFO("\r\n");

    // 1. 停止连接管理器
    ConnectionManager_TaskStop();

#if 1
    // 2. 停止固件更新服务器
    stop_update_server();

        
    // 3. 回收其他 5 个线程资源
    for (int i = 0; i < NUM_THREADS ; i++) //NUM_THREADS
    {
        if (threads[i] != 0) 
        {
            int ret = pthread_join(threads[i], NULL);
            if (ret == 0) 
            {
                LOG_INFO("Thread %d joined successfully\n", i);
            } 
            else 
            {
                LOG_ERROR("Failed to join thread %d: %s\n", i, strerror(errno));
            }
        }
    }
#endif    
    LOG_INFO("All application tasks cleaned up.\n");
}
