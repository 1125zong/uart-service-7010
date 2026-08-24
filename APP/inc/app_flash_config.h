/**
 * @file app_flash_config.h
 * @brief Flash配置存储和读取相关函数声明
 * 
 * 本文件声明了用于在嵌入式系统中使用Flash存储配置参数的函数接口。
 */

#ifndef APP_FLASH_CONFIG_H
#define APP_FLASH_CONFIG_H

#include "app_com.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------ Public API Functions ------------------ */

/**
 * @brief 保存配置参数到Flash
 * @param config 指向配置参数结构体的指针
 * @return 0表示成功，负数表示失败
 */
int save_config_to_flash(const SystemConfiguration* config);

/**
 * @brief 从Flash加载配置参数
 * @param config 指向配置参数结构体的指针，用于存储从Flash读取的数据
 * @return 0表示成功，负数表示失败
 */
int load_config_from_flash(SystemConfiguration* config);

/**
 * @brief 系统初始化时调用此函数加载配置
 * @return 0表示成功，负数表示失败
 */
int init_system_config(void);

/**
 * @brief 保存当前配置到Flash
 * @return 0表示成功，负数表示失败
 */
int save_current_config(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FLASH_CONFIG_H */