/*
 * =================================================================
 *
 * VxWorks MTD 环境变量读写库 (无文件系统依赖)
 * (V-Final 方案 - 共享环境 @ mtd2)
 *
 * 实现了 U-Boot fw_setenv 的核心功能:
 * 1. 知道 mtd2 (app_env) 的冗余扇区 (0x120000 和 0x130000)。
 * 2. 知道如何计算 U-Boot CRC32。
 * 3. 知道如何安全地在两个扇区之间交替写入。
 *
 * 【重要】:
 * 1. 必须提供 MTD/Flash 驱动的占位函数。
 * 2. CRC32 算法必须与 U-Boot (zlib) 匹配。
 * 3. 环境变量大小 (ENV_SIZE) 必须与 U-Boot (CONFIG_ENV_SIZE) 匹配。
 * =================================================================
 */

/* * 包含的头文件 (根据您的 VxWorks 环境)
 * 您需要 stdint.h (用于 uint32_t), string.h (用于 memcmp, memcpy, strlen)
 * 和 STATUS (OK/ERROR) 的定义。
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>
#include <arpa/inet.h>  // for htonl
#include "hal_log.h"
#include "hal_flash.h"
#include "system_abstraction_layer.h"





/* --- 2. U-Boot CRC32 (zlib) 算法 --- */
/* (这与 update_client_v4.c 和 image_update.py 中的算法完全相同) */
static const uint32_t crc32_table[256] = {
    0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
    0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
    0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
    0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
    0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
    0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
    0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
    0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
    0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
    0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
    0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
    0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
    0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
    0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
    0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
    0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
    0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
    0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
    0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
    0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
    0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
    0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
    0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
    0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
    0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
    0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
    0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
    0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
    0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
    0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
    0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
    0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
    0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
    0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
    0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
    0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
    0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
    0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
    0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
    0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
    0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
    0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
    0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
    0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
    0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
    0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
    0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
    0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
    0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
    0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
    0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
    0x2d02ef8dL
};

/*
 * U-Boot CRC32 计算函数
 * (与 U-Boot 源码 common/crc32.c 中的 crc32_no_comp 行为一致)
 */
uint32_t calculate_crc32(uint32_t crc_in, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    uint32_t crc = crc_in ^ 0xffffffffL;
    while (len--) {
        crc = crc32_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffL;
}


#if 1
/**
 * @brief 直接操作 /dev/mtd2 设置环境变量值
 * 顺序: bitstream_b_size(0), image_b_size(4), bootpart(8), fpgapart(12)
 * @param key: 环境变量名 (bitstream_b_size/image_b_size/bootpart/fpgapart)
 * @param value: 环境变量值
 * @return 成功返回 0，失败返回 -1
 */
int set_uboot_env(const char *key, const char *value) 
{
    if (!key || !value) 
    {
        fprintf(stderr, "参数无效\n");
        return -1;
    }

    // 环境变量存储在偏移 0 处，使用 mtdblock 块设备
    const char *mtd_dev = "/dev/mtd2";
 
    // 读取现有数据（从偏移0开始读取16字节）
    char env_buf[16] = {0};
    int val = atoi(value);
    int fd = -1;

    LOG_INFO("\nvalue: %s      val: %d\n", value, val);

    // 打开设备读取现有数据
    fd = open(mtd_dev, O_RDWR);
    if (fd < 0) 
    {
        fprintf(stderr, "打开 %s 失败\n", mtd_dev);
        return -1;
    }

    // 读取现有的环境变量数据
    if (read(fd, env_buf, sizeof(env_buf)) < 0)
    {
        fprintf(stderr, "读取环境变量失败\n");
        close(fd);
        return -1;
    }

    // 打印写入前的数据
    LOG_INFO("[BEFORE] Read data from %s: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
             mtd_dev,
             env_buf[0],  env_buf[1],  env_buf[2],  env_buf[3],
             env_buf[4],  env_buf[5],  env_buf[6],  env_buf[7],
             env_buf[8],  env_buf[9],  env_buf[10], env_buf[11],
             env_buf[12], env_buf[13], env_buf[14], env_buf[15]);

    // 根据key确定偏移
    int offset = 0;
    if (strcmp(key, "bitstream_b_size") == 0)
    {
        offset = 0;
    } 
    else if (strcmp(key, "image_b_size") == 0)
    {
        offset = 4;
    } 
    else if (strcmp(key, "bootpart") == 0) 
    {
        offset = 8;
    }
    else if (strcmp(key, "fpgapart") == 0) 
    {
        offset = 12;
    }
    else 
    {
        fprintf(stderr, "不支持的变量: %s\n", key);
        close(fd);
        return -1;
    }

    // 写入值 (大端序，4字节)
    env_buf[offset]     = (val >> 24) & 0xFF;
    env_buf[offset + 1] = (val >> 16) & 0xFF;
    env_buf[offset + 2] = (val >> 8) & 0xFF;
    env_buf[offset + 3] = val & 0xFF;

    // 重新定位到开头，写入数据
    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        fprintf(stderr, "lseek 写入位置失败\n");
        close(fd);
        return -1;
    }

    // 写入整个缓冲区（保持其他位置的数据不变）
    if (write(fd, env_buf, sizeof(env_buf)) < 0)
    {
        fprintf(stderr, "写入环境变量失败\n");
        close(fd);
        return -1;
    }

    // 关键: 同步数据到 Flash，确保真正写入
    fsync(fd);

    // 验证写入
    char verify_buf[16] = {0};
    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        fprintf(stderr, "lseek 验证位置失败\n");
        close(fd);
        return -1;
    }

    if (read(fd, verify_buf, sizeof(verify_buf)) < 0)
    {
        fprintf(stderr, "验证读取失败\n");
        close(fd);
        return -1;
    }

    // 打印写入后的数据
    LOG_INFO("[AFTER] Verify data from %s: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
             mtd_dev,
             verify_buf[0],  verify_buf[1],  verify_buf[2],  verify_buf[3],
             verify_buf[4],  verify_buf[5],  verify_buf[6],  verify_buf[7],
             verify_buf[8],  verify_buf[9],  verify_buf[10], verify_buf[11],
             verify_buf[12], verify_buf[13], verify_buf[14], verify_buf[15]);

    close(fd);

    printf("set env ok: %s = %d\n", key, val);

    // 调用 sync 确保数据持久化
    system("sync");

    return 0;
}

/**
 * @brief 直接操作 /dev/mtd2 获取环境变量值（从偏移4K处读取）
 * @param var_name: 环境变量名
 * @param out_buf: 存储变量值的缓冲区
 * @param buf_len: 缓冲区大小
 * @return 成功返回 0，失败返回 -1
 */
int get_uboot_env(const char *var_name, char *out_buf, size_t buf_len) 
{
    if (!var_name || !out_buf || buf_len == 0) 
    {
        fprintf(stderr, "参数无效\n");
        return -1;
    }

    // 环境变量存储在偏移 4K 处
    const char *mtd_dev = "/dev/mtdblock2";
    const int ENV_BASE_OFFSET = 4096;  // 偏移4K作为起始地址
    char env_buf[16] = {0};

    // 打开设备
    int fd = open(mtd_dev, O_RDONLY);
    if (fd < 0) 
    {
        fprintf(stderr, "打开 %s 失败\n", mtd_dev);
        return -1;
    }

    // 使用 lseek 定位到偏移 4K 处
    if (lseek(fd, ENV_BASE_OFFSET, SEEK_SET) < 0)
    {
        fprintf(stderr, "lseek 失败\n");
        close(fd);
        return -1;
    }

    // 读取数据
    if (read(fd, env_buf, sizeof(env_buf)) < 0)
    {
        fprintf(stderr, "读取环境变量失败\n");
        close(fd);
        return -1;
    }
    close(fd);

    // 根据key确定在4K偏移基础上的偏移
    int offset = 0;
    if (strcmp(var_name, "bitstream_b_size") == 0) 
    {
        offset = 0;
    }
    else if (strcmp(var_name, "image_b_size") == 0) 
    {
        offset = 4;
    }
    else if (strcmp(var_name, "bootpart") == 0) 
    {
        offset = 8;
    }
    else if (strcmp(var_name, "fpgapart") == 0) 
    {
        offset = 12;
    }
    else 
    {
        fprintf(stderr, "不支持的变量: %s\n", var_name);
        return -1;
    }

    // 读取值 (大端序)
    unsigned int val = (env_buf[offset] << 24) | (env_buf[offset + 1] << 16) 
                     | (env_buf[offset + 2] << 8) | env_buf[offset + 3];

    snprintf(out_buf, buf_len, "%u", val);
    printf("get U-Boot env %s = %s\n", var_name, out_buf);
    return 0;
}

#else

/**
 * @brief 设置 U-Boot 环境变量（带 fallback 机制）
 * @param key: 环境变量名
 * @param value: 环境变量值
 * @return 成功返回 0，失败返回 -1
 */
int set_uboot_env(const char *key, const char *value) 
{
    char command[512];
    int ret;

    /* 步骤1: 尝试直接调用 fw_setenv */
    snprintf(command, sizeof(command), "fw_setenv %s %s 2>&1", key, value);
    LOG_INFO("set_uboot_env: 尝试直接调用...\n");

    FILE *fp = popen(command, "r");
    if (fp != NULL) {
        char output[256] = {0};
        char line[128];
        while (fgets(line, sizeof(line), fp) != NULL) {
            strncat(output, line, sizeof(output) - strlen(output) - 1);
        }
        ret = pclose(fp);
        
        if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
            LOG_INFO("set_uboot_env: 直接调用成功 %s = %s\n", key, value);
            return 0;
        }
        LOG_INFO("set_uboot_env: 直接调用失败, output: %s\n", output);
    }

    /* 步骤2: 尝试通过 system() 调用 */
    snprintf(command, sizeof(command), "fw_setenv %s %s", key, value);
    LOG_INFO("set_uboot_env: 尝试 system() 调用...\n");
    
    ret = system(command);
    if (ret == -1) {
        LOG_ERROR("set_uboot_env: system() 调用失败\n");
    } else if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
        LOG_INFO("set_uboot_env: system() 调用成功 %s = %s\n", key, value);
        return 0;
    } else {
        LOG_INFO("set_uboot_env: system() 调用失败, errcode: %d\n", WEXITSTATUS(ret));
    }

    /* 步骤3: 尝试带 sudo */
    LOG_INFO("set_uboot_env: 尝试 sudo 调用...\n");
    snprintf(command, sizeof(command), "sudo fw_setenv %s %s 2>&1", key, value);
    
    fp = popen(command, "r");
    if (fp != NULL) {
        char output[256] = {0};
        char line[128];
        while (fgets(line, sizeof(line), fp) != NULL) {
            strncat(output, line, sizeof(output) - strlen(output) - 1);
        }
        ret = pclose(fp);
        
        if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
            LOG_INFO("set_uboot_env: sudo 调用成功 %s = %s\n", key, value);
            return 0;
        }
        LOG_ERROR("set_uboot_env: sudo 调用失败, output: %s\n", output);
    }

    LOG_ERROR("set_uboot_env: 所有方法均失败 %s = %s\n", key, value);
    return -1;
}


/**
 * @brief 调用 fw_printenv 获取 U-Boot 环境变量的值
 * @param var_name: 要获取的环境变量名（如 "ipaddr"、"bootargs"）
 * @param out_buf: 存储变量值的缓冲区
 * @param buf_len: 缓冲区大小
 * @return 成功返回 0，失败返回 -1
 */
int get_uboot_env(const char *var_name, char *out_buf, size_t buf_len) 
{
    if (var_name == NULL || out_buf == NULL || buf_len == 0) {
        fprintf(stderr, "参数无效\n");
        return -1;
    }

    // 拼接 fw_printenv 命令（-n 仅输出变量值，避免解析多余内容）
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "fw_printenv -n %s", var_name);

    printf("get_uboot_env cmd: %s\n", cmd);
    // 执行命令并读取输出
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        fprintf(stderr, "执行 fw_printenv 失败: %s\n", strerror(errno));
        return -1;
    }

    // 读取命令输出（变量值）到缓冲区
    memset(out_buf, 0, buf_len);
    if (fgets(out_buf, buf_len - 1, fp) == NULL) 
    {
        fprintf(stderr, "读取变量 %s 失败（变量不存在或配置错误）\n", var_name);
        pclose(fp);
        return -1;
    }

    // 去除换行符（fgets 会读取换行符）
    size_t len = strlen(out_buf);
    if (len > 0 && out_buf[len - 1] == '\n') {
        out_buf[len - 1] = '\0';
    }

    // 关闭管道并检查命令退出码
    int ret = pclose(fp);
    if (WEXITSTATUS(ret) != 0) {
        fprintf(stderr, "fw_printenv 执行出错，退出码: %d\n", WEXITSTATUS(ret));
        return -1;
    }

    printf("get U-Boot env %s = %s\n", var_name, out_buf);
    return 0;
}

#endif