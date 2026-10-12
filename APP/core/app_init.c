/*
 * =====================================================================================
 *
 * Filename:  app_init.c
 *
 * Description:  应用程序的主入口函数。
 * 负责调用各个初始化模块完成系统初始化。
 *
 * =====================================================================================
 */
#include <sys/mount.h>
#include "app_com.h"
#include "app_udp_search.h"
#include "app_net_cfg.h"
#include "app_net_con.h"
#include "app_report.h"
#include "app_net_scheduler.h"

#include "hal_axi16550.h"
#include "key.h"

#include "app_init_modules.h"
#include "app_watchdog.h"
#include "app_crash_handler.h"


extern void cleanup_pl_memory_access(void);
static int mount_jffs2(void);
static int echo_fw_config(void);

/**
 * @brief 应用程序的主入口函数
 */

#if 0
// 子线程：每100ms打印一次
void* print_thread(void* arg)
{

    void test_uart(void);
    void test_init(void);
    test_init();
    
    while (1)
    {
        printf("[子线程] 100ms 定时输出\n");
        test_uart();

        // 高精度休眠 100ms，不占CPU
        struct timespec ts = {0,  100 * 1000};
        nanosleep(&ts, NULL);
    }
    return NULL;
}

void pthread(void)
{
        pthread_t tid;
    // 创建子线程
    if (pthread_create(&tid, NULL, print_thread, NULL) != 0)
    {
        perror("线程创建失败");
        return 1;
    }

    printf("[主线程] 已启动，现在进入阻塞等待（CPU 0%）\n");

    pthread_join(tid, NULL);
}
#endif


void app_start(void) 
{
    int ret;
	log_init(LOG_LEVEL_DEBUG);

    /* 注册崩溃信号处理，确保后续初始化中崩溃可生成日志 */
    //crash_handler_init();

	//FPGA_Info_Read();
	if (init_global_resources() != OK)
	{
        LOG_ERROR("FATAL: Failed to initialize global resources.\n");
        return;
    }	

    //echo_fw_config();
    //mount_jffs2();
	ret = dev_config_init();

    /* 配置加载成功后，将RX/TX镜像端口恢复到FPGA */
    if (ret == OK)
    {
        if (port_mirror_apply(g_system_config.mirror_rx_port, 0x00) != OK)
        {
            LOG_ERROR("Failed to restore RX mirror port %u\n", (unsigned int)g_system_config.mirror_rx_port);
        }

        if (port_mirror_apply(g_system_config.mirror_tx_port, 0x01) != OK)
        {
            LOG_ERROR("Failed to restore TX mirror port %u\n", (unsigned int)g_system_config.mirror_tx_port);
        }
    }
    else
    {
        LOG_ERROR("Failed to initialize device configuration\n");
    }

	dev_config_dump_all();
	init_channel_states();
    
    /* 根据Flash配置自动初始化看门狗 */
    watchdog_init_from_config();



    // extern void test_rx_led(void);
    // test_rx_led();

    // extern void test_init(void);
    // test_init();  
    // extern void test_uart(void);
    // test_uart();


#if 1
    ret = create_application_tasks();
    if (ret == OK)
    {
        LOG_INFO("Application tasks created successfully.\n");
    }
    else
    {
        LOG_ERROR("Failed to create application tasks.\n");
        cleanup_application_tasks();
        return;
    }

    // 程序退出前回收所有线程资源
    LOG_INFO("Application is exiting, cleaning up threads...\n");
    cleanup_application_tasks();
    cleanup_pl_memory_access();

#endif

}
/**
 * @brief 挂载 jffs2 文件系统
 * 
 * 该函数将 /dev/mtdblock3 挂载到 /mnt 目录
 * 
 * @return int 成功返回 0，失败返回 -1
 */
static int mount_jffs2(void)
{
    int ret;
    
    printf("mount jffs2 filesystem...\n");
    
    // 尝试挂载：mount -t jffs2 /dev/mtdblock3 /mnt
    ret = mount("/dev/mtdblock3", "/mnt", "jffs2", 0, NULL);
    
    if (ret != 0)
    {
        perror("mount failed!");
        printf("can not mount /dev/mtdblock3 to /mnt\n");
        return -1;
    }
    
    printf("mount /dev/mtdblock3 to /mnt success!\n");
    return 0;
}

static int echo_fw_config(void)
{
    // 以 w 模式打开 = 清空文件 + 覆盖写入
    FILE *fp = fopen("/etc/fw_env.config", "w");
    if (fp == NULL) {
        perror("open /etc/fw_env.config failed!");
        return -1;
    }

    // 要写入的内容（末尾加 \n 是规范换行）
    const char *data = "/dev/mtd0  0xe0000  0x20000  0x10000\n";

    // 写入文件
    fputs(data, fp);

    // 必须关闭文件
    fclose(fp);

    printf("overwrite /etc/fw_env.config success!\n");

    return 0;
}