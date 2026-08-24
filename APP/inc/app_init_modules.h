#ifndef APP_INIT_MODULES_H
#define APP_INIT_MODULES_H

#include "app_com.h"

/* ------------------ Resource Initialization Functions ------------------ */

/**
 * @brief 初始化全局资源（消息队列和互斥锁）
 * @return 成功返回OK，失败返回ERROR
 */
STATUS init_global_resources(void);

/**
 * @brief 初始化通道状态
 */
void init_channel_states(void);

/* ------------------ Task Creation Functions ------------------ */

/**
 * @brief 创建配置任务管理器
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_config_task_manager(void);

/**
 * @brief 创建实时调度任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_realtime_scheduler_task(void);

/**
 * @brief 创建UDP搜索任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_udp_search_task(void);

/**
 * @brief 创建报告管理任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_report_manager_task(void);

/**
 * @brief 创建按键处理任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_key_process_task(void);

/**
 * @brief 创建应用程序任务
 * @return 成功返回OK，失败返回ERROR
 */
STATUS create_application_tasks(void);

/**
 * @brief 清理/回收所有应用程序任务
 * 在程序退出时调用，用于回收所有线程资源
 */
void cleanup_application_tasks(void);

#endif /* APP_INIT_MODULES_H */