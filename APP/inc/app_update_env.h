#ifndef __APP_UPDATE_ENV_H__
#define __APP_UPDATE_ENV_H__

/* VxWorks 环境变量读写库头文件 */
/* 兼容 V-Final 方案 - 共享环境 @ mtd2 */

#include <stdint.h>
#include <stddef.h>
#include "app_com.h"

/* --- MTD 驱动函数声明 (需要实现) --- */

/**
 * @brief 从 Flash 读取数据
 * @param offset 偏移地址
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return STATUS OK/ERROR
 */
STATUS flash_data_read(uint32_t offset, void* data, uint32_t len);

/**
 * @brief 向 Flash 写入数据
 * @param offset 偏移地址
 * @param data 数据缓冲区
 * @param len 数据长度
 * @return STATUS OK/ERROR
 */
STATUS flash_data_write(uint32_t offset, const void* data, uint32_t len);

/**
 * @brief 擦除 Flash 区域
 * @param offset 偏移地址
 * @param len 擦除长度
 * @return STATUS OK/ERROR
 */
STATUS flash_data_erase(uint32_t offset, uint32_t len);

/* --- CRC32 计算函数 --- */

/**
 * @brief 计算 U-Boot 兼容的 CRC32 校验和
 * @param crc_in 初始 CRC 值 (通常为0)
 * @param buf 数据缓冲区
 * @param len 数据长度
 * @return uint32_t CRC32 校验和
 */
uint32_t calculate_crc32(uint32_t crc_in, const void *buf, size_t len);

/* --- 环境变量操作接口 --- */

/*
 * 以下 app_fw_* 接口保留给旧的冗余 MTD env 实现兼容。
 * 当前 Linux/eMMC 方案使用标准 U-Boot 工具，实际调用 set_uboot_env/get_uboot_env。
 */
STATUS app_fw_find_env(void);
STATUS app_fw_setenv(const char *name, const char *value);
STATUS app_fw_save(void);

/**
 * @brief 设置 U-Boot 环境变量（底层调用 fw_setenv）
 * @param key 环境变量名，例如 boot_slot / boot_ok
 * @param value 环境变量值
 * @return 成功返回 0，失败返回 -1
 */
int set_uboot_env(const char *key, const char *value);

/**
 * @brief 读取 U-Boot 环境变量（底层调用 fw_printenv -n）
 * @param var_name 环境变量名
 * @param out_buf 输出缓冲区
 * @param buf_len 输出缓冲区长度
 * @return 成功返回 0，失败返回 -1
 */
int get_uboot_env(const char *var_name, char *out_buf, size_t buf_len);

/* --- 环境变量配置常量 --- */

/*
 * A/B 启动重试次数的统一取值（写回 boot_attempts 时使用）。
 * 必须与下面两处一致，否则每次开机都会多写一次 env：
 *   1) U-Boot 默认 env 的 boot_attempts
 *   2) /etc/init.d/S99uart-service 的 BOOT_ATTEMPTS_MAX
 */
#ifndef FW_ENV_BOOT_ATTEMPTS
#define FW_ENV_BOOT_ATTEMPTS    "7"
#endif

/* 冗余分区地址 */
#define ENV_BASE_ADDR_A  0x1830000  /* 分区A地址 */
#define ENV_BASE_ADDR_B  0x1840000  /* 分区B地址 */

/* 环境变量存储偏移 (每个分区内部偏移相同) */
#define ENV_OFFSET_VER_SELECT  0x0000  /* ver_select 偏移 (1字节) */
#define ENV_OFFSET_OS_B_SIZE   0x0004  /* os_b_size 偏移 (4字节) */
#define ENV_OFFSET_APP_B_SIZE  0x0008  /* app_b_size 偏移 (4字节) */
#define ENV_OFFSET_BIT_SIZE    0x000C  /* bit_size 偏移 (4字节) */
#define ENV_OFFSET_FLAG        0x0010  /* 标志位偏移 (1字节) */
#define ENV_OFFSET_CRC         0x0011  /* CRC32偏移 (4字节) */

/* 环境变量大小定义 */
#define ENV_SIZE_VER_SELECT  1  /* ver_select: 1字节 (如 'a' 或 'b') */
#define ENV_SIZE_OS_B_SIZE   4  /* os_b_size: 4字节 (如 0x1234) */
#define ENV_SIZE_APP_B_SIZE  4  /* app_b_size: 4字节 (如 0x5678) */
#define ENV_SIZE_BIT_SIZE    4  /* bit_size: 4字节 (如 0x9ABC) */
#define ENV_SIZE_FLAG        1  /* 标志位: 1字节 */
#define ENV_SIZE_CRC         4  /* CRC32: 4字节 */

#endif /* __APP_UPDATE_ENV_H__ */