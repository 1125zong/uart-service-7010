
/* 头文件 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mount.h>

#include "screen_uart.h"
#include "hal_flash.h"

#include "hal_axi16550.h"

// 定义编译时间宏（简化调用）
#define COMPILE_DATE    __DATE__
#define COMPILE_TIME    __TIME__

void print_compile_time(void)
{
    printf("\r\n");
    printf("**********************************************************************************\n");
    printf("*                                                                                *\n");
    printf("*            App Compile Time: %s %s                                             *\n", COMPILE_DATE, COMPILE_TIME);
    printf("*                                                                                *\n");
    printf("**********************************************************************************\n");
    printf("\r\n"); 
}

//外部函数声明
extern void app_start(void);

int  main(void)
{
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




