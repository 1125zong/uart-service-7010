/*
 * Copyright 2019-2022 中航工业西安航空计算技术研究所
 *
 * 对本文件的拷贝、发布、修改或任何其它用途必须得到
 * 中航工业西安航空计算技术研究所的书面协议许可.
 *
 * Copyright (C) 2019-2020 ACTRI
 * All Rights Reserved.
 */

#ifndef __HAL_FLASH_H_
#define __HAL_FLASH_H_

//#include <vxWorks.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flash地址范围定义 */

#define FLASH_START_ADDR_MIN 					(0xE8520000)
#define FLASH_START_ADDR    					(0xE9F00000)//basic info  31M处
#define FLASH_START_ADDR_NET_INFO    			(0xE9F00000 + 64 * 1024)//net info
#define FLASH_START_ADDR_RESTORE_FLAG    		(0xE9F00000 + 2 * 64 * 1024 )//恢复出厂设置标志位

#define FLASH_END_ADDR      	(0xE9F00000 + 1 * 1024 * 1024)
#define FLASH_SIZE          	(FLASH_END_ADDR - FLASH_START_ADDR_MIN)

/* 函数返回值定义 */
#define FLASH_SUCCESS       0
#define FLASH_ERROR_ADDR    -1
#define FLASH_ERROR_SIZE    -2
#define FLASH_ERROR_HW      -3





/**
 * @brief 安全的Flash写入函数
 * @details 限制地址范围在 E8FE0000 - E8FF0000 (64KB)
 * @param flashStart Flash起始地址
 * @param buf 要写入的数据缓冲区
 * @param nbytes 要写入的字节数
 * @return 成功返回FLASH_SUCCESS，失败返回错误码
 * @retval FLASH_SUCCESS 写入成功
 * @retval FLASH_ERROR_ADDR 地址越界
 * @retval FLASH_ERROR_SIZE 数据大小超出范围
 * @retval FLASH_ERROR_HW 硬件错误
 */
int hal_flash_write(uint32_t flashStart, const char * buf, uint32_t nbytes);


/**
 * @brief 安全的Flash读取函数
 * @details 限制地址范围在 E8FE0000 - E8FF0000 (64KB)
 * @param flashStart Flash起始地址
 * @param buf 用于存储读取数据的缓冲区
 * @param nbytes 要读取的字节数
 * @return 成功返回实际读取的字节数，失败返回错误码
 * @retval >0 实际读取的字节数
 * @retval FLASH_ERROR_ADDR 地址越界
 * @retval FLASH_ERROR_SIZE 数据大小超出范围
 * @retval FLASH_ERROR_HW 硬件错误
 */
int hal_flash_read(uint32_t flashStart, char * buf, uint32_t nbytes);

/**
 * @brief 擦除Flash扇区
 * @details 限制地址范围在 E8FE0000 - E8FF0000 (64KB)
 * @param flashStart Flash起始地址
 * @param nbytes 要擦除的字节数
 * @return 成功返回FLASH_SUCCESS，失败返回错误码
 * @retval FLASH_SUCCESS 擦除成功
 * @retval FLASH_ERROR_ADDR 地址越界
 * @retval FLASH_ERROR_SIZE 数据大小超出范围
 * @retval FLASH_ERROR_HW 硬件错误
 */
int hal_flash_erase(uint32_t flashStart, uint32_t nbytes);

/**
 * @brief 释放Flash映射资源
 * @return 成功返回0，失败返回-1
 */
int hal_flash_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_FLASH_H_ */
