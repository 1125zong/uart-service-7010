/*
 * Linux文件系统版本的Flash抽象层
 * 将Flash操作转换为文件操作
 */

#include "hal_flash.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>


//#define fmsh
#ifdef fmsh
/* 配置文件路径定义 */
#define CONFIG_DIR                "/root/config/config_data"
#define BASIC_CONFIG_FILE         "/root/config/config_data/basic_config.dat"
#define NETWORK_CONFIG_FILE       "/root/config/config_data/network_config.dat"
#define RESTORE_FLAG_FILE         "/root/config/config_data/restore_flag.dat"

#else
/* 配置文件路径定义 */
#define CONFIG_DIR                "/mnt/config/config_data"
#define BASIC_CONFIG_FILE         "/mnt/config/config_data/basic_config.dat"
#define NETWORK_CONFIG_FILE       "/mnt/config/config_data/network_config.dat"
#define RESTORE_FLAG_FILE         "/mnt/config/config_data/restore_flag.dat"
#endif


/* 配置数据的最大大小 */
#define MAX_CONFIG_SIZE           (1024 * 1024)  // 1MB

/**
 * @brief 确保配置目录存在（递归创建）
 *        使用宏定义保留旧实现，通过宏将旧函数重命名为 legacy 版本，
 *        再重新实现支持递归创建多级目录的新版本。
 * @return 成功返回0，失败返回-1
 */

/* 宏定义：将旧实现重命名为 ensure_config_dir_legacy，保留原函数 */
#define ensure_config_dir ensure_config_dir_legacy
static int ensure_config_dir_legacy(void)
{
    struct stat st = {0};
    
    if (stat(CONFIG_DIR, &st) == -1) {
        // 尝试创建目录
        if (mkdir(CONFIG_DIR, 0755) != 0) 
        {
            printf("Error creating config directory: %s\n", strerror(errno));
            return -1;
        }
    }
    
    return 0;
}
#undef ensure_config_dir

/**
 * @brief 递归创建目录（可创建多级目录）
 * @param path 目录路径
 * @param mode 目录权限
 * @return 成功返回0，失败返回-1
 */
static int mkdir_recursive(const char *path, mode_t mode)
{
    struct stat st = {0};
    char tmp[PATH_MAX];
    size_t len;
    char *p;

    /* 空路径或长度超出限制，直接返回 */
    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    len = strlen(path);
    if (len >= sizeof(tmp)) {
        return -1;
    }

    /* 复制路径到临时缓冲区 */
    strcpy(tmp, path);

    /* 去除末尾的 '/'，避免影响目录创建 */
    if (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    /* 逐级检查并创建目录 */
    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            /* 若当前层级目录不存在则创建 */
            if (stat(tmp, &st) != 0) {
                if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                    return -1;
                }
            }
            *p = '/';
        }
    }

    /* 创建最后一层目录 */
    if (stat(tmp, &st) != 0) {
        if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
            return -1;
        }
    }

    return 0;
}

/**
 * @brief 确保配置目录存在（递归创建多级目录）
 * @return 成功返回0，失败返回-1
 */
static int ensure_config_dir(void)
{
    struct stat st = {0};

    /* 目录已存在，直接返回成功 */
    if (stat(CONFIG_DIR, &st) == 0) {
        return 0;
    }

    /* 递归创建配置目录 */
    if (mkdir_recursive(CONFIG_DIR, 0755) != 0) {
        printf("Error creating config directory: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * @brief 根据Flash地址映射到相应的配置文件
 * @param flashStart Flash起始地址
 * @return 对应的文件路径，如果地址无效则返回NULL
 */
static const char* get_file_path_for_address(uint32_t flashStart)
{
    if (flashStart == FLASH_START_ADDR) 
    {
        return BASIC_CONFIG_FILE;
    } 

    else if (flashStart == FLASH_START_ADDR_NET_INFO) 
    {
        return NETWORK_CONFIG_FILE;
    }

    else if (flashStart == FLASH_START_ADDR_RESTORE_FLAG) 
    {
        return RESTORE_FLAG_FILE;
    }
    
    // 检查是否在有效地址范围内
    if (flashStart >= FLASH_START_ADDR_MIN && flashStart < FLASH_END_ADDR) 
    {

        // 对于其他地址，使用基础配置文件，但需要计算偏移量
        return BASIC_CONFIG_FILE;
    }
    
    return NULL;
}

/**
 * @brief 计算文件中的偏移量
 * @param flashStart Flash起始地址
 * @return 在文件中的偏移量
 */
static uint32_t calculate_file_offset(uint32_t flashStart)
{
    if (flashStart >= FLASH_START_ADDR && flashStart < FLASH_END_ADDR) 
    {
        return flashStart - FLASH_START_ADDR;
    }
    
    return 0;
}

/**
 * @brief 安全的Flash写入函数 (Linux文件版本)
 * @param flashStart Flash起始地址
 * @param buf 要写入的数据缓冲区
 * @param nbytes 要写入的字节数
 * @return 成功返回FLASH_SUCCESS，失败返回错误码
 */
int hal_flash_write(uint32_t flashStart, const char * buf, uint32_t nbytes)
{
    if (buf == NULL || nbytes == 0) 
    {
        return FLASH_ERROR_HW;
    }
    
    // 检查地址范围
    if (flashStart < FLASH_START_ADDR_MIN || flashStart >= FLASH_END_ADDR) 
    {
        printf("Error: Flash write start address 0x%08X is out of range [0x%08X - 0x%08X)\n",
               flashStart, FLASH_START_ADDR_MIN, FLASH_END_ADDR);
        return FLASH_ERROR_ADDR;
    }
    
    uint32_t endAddr = flashStart + nbytes;
    if (endAddr > FLASH_END_ADDR) 
    {
        printf("Error: Flash write end address 0x%08X exceeds maximum address 0x%08X\n",
               endAddr, FLASH_END_ADDR);
        return FLASH_ERROR_SIZE;
    }
    
    // 确保配置目录存在
    if (ensure_config_dir() != 0) 
    {
        return FLASH_ERROR_HW;
    }
    
    // 获取相应的文件路径
    const char* file_path = get_file_path_for_address(flashStart);
    if (file_path == NULL) {
        printf("Error: Invalid flash address 0x%08X\n", flashStart);
        return FLASH_ERROR_ADDR;
    }
    printf("file_path: %s\n",  file_path);
    
    // 以读写模式打开文件（如果不存在则创建）
    int fd = open(file_path, O_RDWR|O_CREAT);
    if (fd == -1) {
        printf("Error: Cannot open file %s for writing: %s\n", file_path, strerror(errno));
        return FLASH_ERROR_HW;
    }
    
    // 写入数据
    size_t written = write(fd, buf, nbytes);
    if (written != nbytes) {
        printf("Error: Only wrote %zu of %u bytes to %s\n", written, nbytes, file_path);
        close(fd);
        return FLASH_ERROR_HW;
    }
    
    // 确保数据写入磁盘
    close(fd);
    
    return FLASH_SUCCESS;
}

/**
 * @brief 安全的Flash读取函数 (Linux文件版本)
 * @param flashStart Flash起始地址
 * @param buf 用于存储读取数据的缓冲区
 * @param nbytes 要读取的字节数
 * @return 成功返回实际读取的字节数，失败返回错误码
 */
int hal_flash_read(uint32_t flashStart, char * buf, uint32_t nbytes)
{
    if (buf == NULL || nbytes == 0) 
    {
        return FLASH_ERROR_HW;
    }
    
    // 检查地址范围
    if (flashStart < FLASH_START_ADDR_MIN || flashStart >= FLASH_END_ADDR) 
    {
        printf("Error: Flash read start address 0x%08X is out of range [0x%08X - 0x%08X)\n",
               flashStart, FLASH_START_ADDR_MIN, FLASH_END_ADDR);
        return FLASH_ERROR_ADDR;
    }
    
    uint32_t endAddr = flashStart + nbytes;
    if (endAddr > FLASH_END_ADDR) 
    {
        printf("Error: Flash read end address 0x%08X exceeds maximum address 0x%08X\n",
               endAddr, FLASH_END_ADDR);
        return FLASH_ERROR_SIZE;
    }
    
    // 获取相应的文件路径
    const char* file_path = get_file_path_for_address(flashStart);
    if (file_path == NULL) 
    {
        printf("Error: Invalid flash address 0x%08X\n", flashStart);
        return FLASH_ERROR_ADDR;
    }
     
    int fd = open(file_path, O_RDWR|O_CREAT, 0644);
    if (fd == -1) 
    {
        printf("Error: Cannot open file %s for reading: %s\n", file_path, strerror(errno));
        return FLASH_ERROR_HW;
    }      
    // 读取数据
    size_t read_count = read(fd, buf, nbytes);
    
    close(fd);
    
    // 返回实际读取的字节数
    return (int)read_count;
}

/**
 * @brief 擦除Flash扇区 (Linux文件版本)
 * @param flashStart Flash起始地址
 * @param nbytes 要擦除的字节数
 * @return 成功返回FLASH_SUCCESS，失败返回错误码
 */
int hal_flash_erase(uint32_t flashStart, uint32_t nbytes)
{
    // 检查地址范围
    if (flashStart < FLASH_START_ADDR_MIN || flashStart >= FLASH_END_ADDR) {
        printf("Error: Flash erase start address 0x%08X is out of range [0x%08X - 0x%08X)\n",
               flashStart, FLASH_START_ADDR_MIN, FLASH_END_ADDR);
        return FLASH_ERROR_ADDR;
    }
    
    uint32_t endAddr = flashStart + nbytes;
    if (endAddr > FLASH_END_ADDR) {
        printf("Error: Flash erase end address 0x%08X exceeds maximum address 0x%08X\n",
               endAddr, FLASH_END_ADDR);
        return FLASH_ERROR_SIZE;
    }
    
    // 确保配置目录存在
    if (ensure_config_dir() != 0) {
        return FLASH_ERROR_HW;
    }
    
    // 获取相应的文件路径
    const char* file_path = get_file_path_for_address(flashStart);
    if (file_path == NULL) {
        printf("Error: Invalid flash address 0x%08X\n", flashStart);
        return FLASH_ERROR_ADDR;
    }
    
    // 计算文件中的偏移量
    uint32_t file_offset = calculate_file_offset(flashStart);
    
    // 以读写模式打开文件（如果不存在则创建）
    FILE* file = fopen(file_path, "r+b");
    if (file == NULL) {
        // 尝试创建新文件
        file = fopen(file_path, "w+b");
        if (file == NULL) {
            printf("Error: Cannot create/open file %s for erase: %s\n", file_path, strerror(errno));
            return FLASH_ERROR_HW;
        }
    }
    
    // 移动到指定偏移量
    if (fseek(file, file_offset, SEEK_SET) != 0) {
        fclose(file);
        return FLASH_ERROR_HW;
    }
    
    // 将指定数量的字节设置为0xFF（Flash擦除后的状态）
    for (uint32_t i = 0; i < nbytes; i++) {
        if (fputc(0xFF, file) == EOF) {
            fclose(file);
            return FLASH_ERROR_HW;
        }
    }
    
    // 确保数据写入磁盘
    fflush(file);
    fclose(file);
    
    return FLASH_SUCCESS;
}

/**
 * @brief 外部Flash写入函数（兼容性实现）
 * @param flashStart Flash起始地址
 * @param buf 要写入的数据缓冲区
 * @param nbytes 要写入的字节数
 * @return 成功返回0，失败返回-1
 */
int my_flash_write(uint32_t flashStart, char * buf, uint32_t nbytes)
{
    int result = hal_flash_write(flashStart, (const char *)buf, nbytes);
    // 将FLASH_SUCCESS转换为0，错误码转换为-1
    return (result == FLASH_SUCCESS) ? 0 : -1;
}

/**
 * @brief 外部Flash读取函数（兼容性实现）
 * @param flashStart Flash起始地址
 * @param buf 用于存储读取数据的缓冲区
 * @param nbytes 要读取的字节数
 * @return 成功返回0，失败返回-1
 */
int my_flash_read(uint32_t flashStart, char * buf, uint32_t nbytes)
{
    int result = hal_flash_read(flashStart, buf, nbytes);
    // 将实际读取的字节数转换为0表示成功，-1表示失败
    return (result > 0 || (result == 0 && nbytes == 0)) ? 0 : -1;
}