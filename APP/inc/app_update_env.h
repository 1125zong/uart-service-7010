#ifndef __APP_UPDATE_ENV_H__
#define __APP_UPDATE_ENV_H__

/* VxWorks 环境变量读写库头文件 */
/* 兼容 V-Final 方案 - 共享环境 @ mtd2 */

#include <stdint.h>

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

/**
 * @brief 查找并加载有效的环境变量分区
 * @return STATUS OK/ERROR
 * @note 从两个冗余分区中读取环境变量,选择最新有效的分区
 */
STATUS app_fw_find_env(void);

/**
 * @brief 设置环境变量 (内存中修改)
 * @param name 变量名 (支持: ver_select, os_b_size, app_b_size, bit_size)
 * @param value 变量值
 * @return STATUS OK/ERROR
 * @note 此函数只修改内存中的环境变量，需要调用 app_fw_save() 保存到 Flash
 *       - ver_select: 单字符 ('a' 或 'b')
 *       - os_b_size: 十六进制值 (如 "0x1234")
 *       - app_b_size: 十六进制值 (如 "0x5678")
 *       - bit_size: 十六进制值 (如 "0x9ABC")
 */
STATUS app_fw_setenv(const char *name, const char *value);

/**
 * @brief 保存环境变量到 Flash
 * @return STATUS OK/ERROR
 * @note 将内存中的环境变量值写入非活动分区,支持冗余保护
 *       使用标志位轮转和CRC校验确保数据完整性
 */
STATUS app_fw_save(void);

int set_uboot_env(const char *key, const char *value) ;
int get_uboot_env(const char *var_name, char *out_buf, size_t buf_len);

/* --- 环境变量配置常量 --- */

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