
/* 头文件 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mount.h>

#include "screen_uart.h"
#include "hal_flash.h"

#include "hal_axi16550.h"
#include "app_version.h"

// 定义编译时间宏（简化调用）
#define COMPILE_DATE    __DATE__
#define COMPILE_TIME    __TIME__

void print_compile_time(void)
{
    char ver_line[96];
    char time_line[96];

    /* 版本号来自 app_dev.h 的 FW_VERSION_*，编译时间来自 __DATE__/__TIME__ */
    snprintf(ver_line, sizeof(ver_line), "App Version     : %s", FW_VERSION_STRING);
    snprintf(time_line, sizeof(time_line), "App Compile Time: %s %s", COMPILE_DATE, COMPILE_TIME);

    printf("\r\n");
    printf("**********************************************************************************\n");
    printf("*                                                                                *\n");
    printf("*            %-68s*\n", ver_line);
    printf("*            %-68s*\n", time_line);
    printf("*                                                                                *\n");
    printf("**********************************************************************************\n");
    printf("\r\n"); 
}

//外部函数声明
extern void app_start(void);

int  main(int argc, char *argv[])
{
    /*
     * 支持 --version / -v：只打印版本号后退出，不初始化任何资源。
     * 便于在设备上直接查询二进制版本：
     *   /mnt/apps/uart_service --version
     */
    if (argc > 1 &&
        (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0))
    {
        printf("%s\n", FW_VERSION_STRING);
        return 0;
    }

    print_compile_time();  

    app_start();

    printf("\n");
    printf("==============================================\n");
    printf("\n");
    return 0;
}

#if 0
void test_env(void)
{
uint8_t env_boot_partition[3];
    uint8_t update_flag[3];
	//获取环境变量boot_partition值确定写入哪个分区  env_boot_partition
    int ret = get_uboot_env("boot_partition", env_boot_partition, sizeof(env_boot_partition) );
    if(ret != 0)
    {
        printf("get_uboot_env boot_partition failed\n");
        return -1;
    }

    if(env_boot_partition[0] == 'B')
    {
        printf("boot_partition is B\n");
    }

    ret = get_uboot_env("upgrade_ok", update_flag, sizeof(update_flag)  );
    if(ret != 0)
    {
        printf("get_uboot_env upgrade_ok failed\n");
        return -1;
    }

    else
    {
        printf("upgrade_ok is %d\n", update_flag[0]);
    }


    if( set_uboot_env("upgrade_ok", '1') != 0)
    {
        printf("错误：无法设置环境变量 upgrade_ok\n");
        return -1;
    }
    else
    {
        printf("设置环境变量 upgrade_ok 1 成功\n");
    }
}
#endif




