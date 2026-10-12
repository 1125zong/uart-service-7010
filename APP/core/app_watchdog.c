/*
 * =====================================================================================
 *
 * Filename:  app_watchdog.c
 *
 * Description:  看门狗功能实现
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "app_watchdog.h"
#include "app_com.h"
#include "hal_flash.h"
#include "app_dev.h"
#include "app_flash_config.h"

/* 外部变量和函数声明 */
extern SEM_ID g_config_mutex;
extern volatile void* pl_led_base_addr;

/* 看门狗寄存器定义 */
#define WATCHDOG_CTRL_REG_OFFSET    0x274   /* 看门狗控制寄存器 */
#define WATCHDOG_FEED_REG_OFFSET    0x278   /* 看门狗喂狗寄存器 */
#define WATCHDOG_FEED_INTERVAL_MS   500     /* 喂狗间隔500ms */

/* 看门狗线程控制变量 */
static pthread_t g_watchdog_thread_id;
static volatile int g_watchdog_thread_running = 0;

/* 看门狗线程函数 */
static void* watchdog_thread(void* arg)
{
    unsigned int feed_reg_addr;
    (void)arg;  /* 未使用的参数 */
    
    /* 设置线程名 */
    pthread_setname_np(pthread_self(), "WatchdogTask");
    
    LOG_INFO("Watchdog thread started\n");
    
    /* 计算喂狗寄存器地址 */
    feed_reg_addr = (unsigned int)pl_led_base_addr + WATCHDOG_FEED_REG_OFFSET;
    
    /* 交替喂狗：每500ms写0，再每500ms写1 */
    while(g_watchdog_thread_running)
    {
        /* 写入0 */
        *(volatile unsigned int *)feed_reg_addr = 0;
        usleep(WATCHDOG_FEED_INTERVAL_MS * 1000);
        
        if(!g_watchdog_thread_running) 
        {
            break;
        }       
        /* 写入1 */
        *(volatile unsigned int *)feed_reg_addr = 1;
        usleep(WATCHDOG_FEED_INTERVAL_MS * 1000);
    }
    
    LOG_INFO("Watchdog thread stopped\n");
    return NULL;
}

/**
 * @brief 启动看门狗线程
 */
static int start_watchdog_thread(void)
{
    pthread_attr_t attr;
    int ret;
    
    if(g_watchdog_thread_running)
    {
        LOG_INFO("Watchdog thread already running\n");
        return 0;
    }
    
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    g_watchdog_thread_running = 1;
    
    ret = pthread_create(&g_watchdog_thread_id, &attr, watchdog_thread, NULL);
    pthread_attr_destroy(&attr);
    
    if(ret != 0)
    {
        LOG_ERROR("Failed to create watchdog thread: %s\n", strerror(ret));
        g_watchdog_thread_running = 0;
        return -1;
    }
    
    LOG_INFO("Watchdog thread created successfully\n");
    return 0;
}

/**
 * @brief 停止看门狗线程
 */
static void stop_watchdog_thread(void)
{
    if(!g_watchdog_thread_running)
    {
        return;
    }
    
    LOG_INFO("Stopping watchdog thread...\n");
    g_watchdog_thread_running = 0;
    
    /* 等待线程结束 (最多等待2秒) */
    usleep(2000000);
}

/**
 * @brief 根据Flash配置自动初始化看门狗
 * @details 系统上电时调用，检查看门狗配置，如使能则启动喂狗
 */
void watchdog_init_from_config(void)
{
    unsigned int ctrl_reg_addr;
    
    /* 检查硬件基地址是否有效 */
    if(pl_led_base_addr == NULL || pl_led_base_addr == (void*)MAP_FAILED)
    {
        LOG_ERROR("Watchdog init failed: hardware base address not initialized\n");
        return;
    }
    
    /* 检查看门狗是否使能 */
    if(g_system_config.device.watchdog_enable == 1)
    {
        LOG_INFO("Watchdog is enabled in config, auto-starting...\n");
        
        /* 使能看门狗硬件 */
        ctrl_reg_addr = (unsigned int)pl_led_base_addr + WATCHDOG_CTRL_REG_OFFSET;
        *(volatile unsigned int *)ctrl_reg_addr = 1;
        
        /* 启动喂狗线程 */
        if(start_watchdog_thread() < 0)
        {
            LOG_ERROR("Watchdog init failed: failed to start feed thread\n");
            *(volatile unsigned int *)ctrl_reg_addr = 0;
            return;
        }
        
        LOG_INFO("Watchdog auto-started successfully\n");
    }
    else
    {
        LOG_INFO("Watchdog is disabled in config\n");
    }
}

/**
 * @brief 配置看门狗
 * @param enable 1=使能, 0=禁用
 * @return 0成功, -1失败
 */
int watchdog_config(unsigned char enable)
{
    unsigned int ctrl_reg_addr;
    
    /* 检查硬件基地址是否有效 */
    if(pl_led_base_addr == NULL || pl_led_base_addr == (void*)MAP_FAILED)
    {
        LOG_ERROR("Watchdog config failed: hardware base address not initialized\n");
        return -1;
    }
    
    /* 获取配置锁 */
    semTake(g_config_mutex, WAIT_FOREVER);
    
    /* 更新看门狗配置 */
    g_system_config.device.watchdog_enable = enable;
    
    /* 保存配置到 Flash */
    if(dev_config_save() != OK)
    {
        semGive(g_config_mutex);
        LOG_ERROR("Watchdog config failed: failed to save config to flash\n");
        return -1;
    }
    
    /* 控制看门狗硬件 */
    ctrl_reg_addr = (unsigned int)pl_led_base_addr + WATCHDOG_CTRL_REG_OFFSET;
    
    if(enable == 1)
    {
        /* 使能看门狗：向控制寄存器写1 */
        *(volatile unsigned int *)ctrl_reg_addr = 1;
        LOG_INFO("Watchdog enabled (wrote 1 to ctrl reg)\n");
        
        /* 启动看门狗喂狗线程 */
        if(start_watchdog_thread() < 0)
        {
            /* 启动失败，关闭看门狗 */
            *(volatile unsigned int *)ctrl_reg_addr = 0;
            semGive(g_config_mutex);
            LOG_ERROR("Watchdog config failed: failed to start watchdog thread\n");
            return -1;
        }
    }
    else
    {
        /* 禁用看门狗：停止喂狗线程并向控制寄存器写0 */
        *(volatile unsigned int *)ctrl_reg_addr = 0;
        stop_watchdog_thread();
        LOG_INFO("Watchdog disabled (wrote 0 to ctrl reg)\n");
    }
    
    semGive(g_config_mutex);
    
    LOG_INFO("Watchdog config saved and applied successfully\n");
    return 0;
}
