/*
 * =================================================================
 * firmware_server_task_v2.c
 * =================================================================
 */

#include <stdio.h>
#include <stdlib.h>      /* 用于 malloc, free */
#include <string.h>      /* 用于 memcpy, memset */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>    /* 用于 chmod */
#include <sys/mount.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>        /* sockaddr_in, IPPROTO_TCP 等 */
#include <arpa/inet.h>

/* 旧 MTD/flashcp 实现已禁用；以下头文件仅在重新启用旧实现时需要。 */
#if 0
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <mtd/mtd-abi.h>
#endif

/* --- 1. 包含共享的定义 --- */
#include "hal_log.h"
/* 固件包结构体 (magic, crc, versions...) */
#include "app_update_header.h"

/* TCP 协议应答码 (STATUS_OK_TO_PROCEED...) */
#include "app_update_protocol.h"

#include "app_com.h"

#include "app_update_env.h"


/* --- 2. 服务器配置 --- */
#define SERVER_PORT             19001
#define SERVER_TASK_PRIORITY    40      /* 提升到最高优先级,确保升级过程独占CPU */
#define SERVER_STACK_SIZE       32768
#define MAX_CLIENT_QUEUE        1

/*
 * 固件包最大允许大小。
 * 新方案 rootfs 可能是 tar.gz 或完整 ext4 镜像，20MB 明显不够。
 * 如镜像超过 256MB，可通过编译参数 -DFW_MAX_PACKAGE_SIZE=... 覆盖。
 * 注意：app_com.h 里已有 MAX_PACKAGE_SIZE(256)，这里使用独立名字避免冲突。
 */
#ifndef FW_MAX_PACKAGE_SIZE
#define FW_MAX_PACKAGE_SIZE (256 * 1024 * 1024)
#endif

/* eMMC A/B 分区。按当前板卡分区表填写，可用编译参数覆盖。 */
#ifndef IMAGE_A_DEV
#define IMAGE_A_DEV     "/dev/mmcblk0p1"
#endif
#ifndef ROOTFS_A_DEV
#define ROOTFS_A_DEV    "/dev/mmcblk0p2"
#endif
#ifndef IMAGE_B_DEV
#define IMAGE_B_DEV     "/dev/mmcblk0p3"
#endif
#ifndef ROOTFS_B_DEV
#define ROOTFS_B_DEV    "/dev/mmcblk0p4"
#endif

#ifndef IMAGE_MOUNT_POINT
#define IMAGE_MOUNT_POINT   "/mnt/fw_image"
#endif
#ifndef ROOTFS_MOUNT_POINT
#define ROOTFS_MOUNT_POINT  "/mnt/fw_rootfs"
#endif

#define KERNEL_FILE_NAME        "image.ub"
#ifndef BITSTREAM_FILE_NAME
#define BITSTREAM_FILE_NAME     "top_7015.bit"
#endif
#ifndef APP_INSTALL_PATH_IN_ROOTFS
#define APP_INSTALL_PATH_IN_ROOTFS  "/usr/local/bin/uart-service"
#endif

typedef struct
{
    const char *name;
    const char *image_dev;
    const char *rootfs_dev;
} FirmwareSlot;

/* 任务 ID */
static int serverTaskId = 0;
static pthread_t server_thread_id = 0;  // POSIX 线程 ID

/* --- 3. 函数原型 --- */
void *update_server_main(void *arg);
STATUS handle_client(int client_sock);
STATUS process_firmware_package(int client_sock, char *buffer, long size);

static int write_mem_to_file(const void *buf, size_t buf_len, const char *path, mode_t mode);
static int write_mem_to_block_device(const void *buf, size_t buf_len, const char *dev);
static int write_mem_to_mounted_file(const void *buf, size_t buf_len,
                                     const char *dev, const char *mnt,
                                     const char *fstype, const char *path_in_fs,
                                     mode_t mode);
static int write_rootfs_to_target(const void *buf, uint32_t len,
                                  const char *dev, const char *mnt);
static int detect_ab_slots(FirmwareSlot *current, FirmwareSlot *target,
                           char *current_name, size_t current_name_len,
                           char *target_name, size_t target_name_len);
static STATUS finalize_upgrade(const char *current_slot, const char *target_slot);

/* 【修正】: 函数原型现在需要 header 指针和四个数据指针 */
STATUS write_firmware_to_flash(
    const fw_package_header_t *header,
    const char *bit_data,
    const char *kernel_data,
    const char *rootfs_data,
    const char *app_data
);

/**
 * @brief 校验固件包
 * @param buffer 固件包缓冲区
 * @param size 固件包大小
 * @param client_sock 客户端套接字(用于发送错误状态)
 * @param header 输出参数,返回解析的头部
 * @param bit_data 输出参数,返回bitstream数据指针
 * @param os_data 输出参数,返回OS数据指针
 * @param app_data 输出参数,返回App数据指针
 * @param kernel_data 输出参数,返回Kernel数据指针
 * @param rootfs_data 输出参数,返回rootfs数据指针
 * @return 成功返回OK,失败返回ERROR
 */
static STATUS validate_firmware_package(
    char *buffer,
    long size,
    int client_sock,
    fw_package_header_t **header,
    const char **bit_data,
    const char **os_data,
    const char **app_data,
    const char **kernel_data,
    const char **rootfs_data
);


/*
 * =================================================================
 * start_update_server() 任务函数
 * =================================================================
 */
STATUS start_update_server(void)
{
    if (serverTaskId != 0) {
        LOG_INFO("[!] Firmware update server task already running (ID: %d)\n", serverTaskId, 0,0,0,0,0);
        return ERROR;
    }

    /* 使用pthread_create 启动主服务器线程 */
    int ret = pthread_create(&server_thread_id, NULL, (void*)update_server_main, NULL);
    if (ret != 0) {
        LOG_INFO("[X] Fatal error: Cannot start tFwUpdateSrv thread: %s\n", strerror(errno), 0,0,0,0,0);
        serverTaskId = 0;
        server_thread_id = 0;
        return ERROR;
    }

    LOG_INFO("[+] Firmware update server started on port %d (Thread ID: %ld)\n", SERVER_PORT, (long)server_thread_id, 0,0,0,0);
    return OK;
}

/**
 * @brief 停止并回收固件更新服务器线程资源
 * @return 成功返回 OK，失败返回 ERROR
 */
STATUS stop_update_server(void)
{
    if (server_thread_id == 0) {
        LOG_INFO("[!] Firmware update server not running\n", 0,0,0,0,0);
        return ERROR;
    }
    
    LOG_INFO("[*] Stopping firmware update server (Thread ID: %ld)...\n", (long)server_thread_id, 0,0,0,0);

    // 等待线程退出并回收资源
    int ret = pthread_join(server_thread_id, NULL);
    if (ret != 0)
    {
        LOG_INFO("[X] Cannot join firmware update server thread: %s\n", strerror(errno), 0,0,0,0,0);
        return ERROR;
    }
    
    serverTaskId = 0;
    server_thread_id = 0;
    LOG_INFO("[+] Firmware update server stopped and resources recycled\n", 0,0,0,0,0);
    return OK;
}

/*
 * =================================================================
 * 主服务器任务 (使用 select 机制)
 * =================================================================
 */
void *update_server_main(void *arg)
{
    (void)arg;
    int server_sock, client_sock;
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    fd_set read_fds;
    struct timeval timeout;
    int select_ret;

    int ret = pthread_setname_np(pthread_self(), "tFwUpdateSrv");
    if (ret != 0) {
        LOG_INFO("[!] Cannot set thread name: %s\n", strerror(errno), 0,0,0,0,0);
        return NULL;
    }

    /* 1. 创建服务器套接字 */
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == ERROR) {
        LOG_INFO("tFwUpdateSrv: [X] Socket creation failed\n", 0,0,0,0,0,0);
        serverTaskId = 0;
        return NULL;
    }

    /* 2. 绑定地址 */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == ERROR) 
    {
        LOG_INFO("tFwUpdateSrv: [X] Bind port %d failed\n", SERVER_PORT, 0,0,0,0,0);
        close(server_sock);
        serverTaskId = 0;
        return NULL;
    }

    /* 3. 监听连接 */
    if (listen(server_sock, MAX_CLIENT_QUEUE) == ERROR) 
    {
        LOG_INFO("tFwUpdateSrv: [X] Listen failed\n", 0,0,0,0,0,0);
        close(server_sock);
        serverTaskId = 0;
        return NULL;
    }

    /* 4. 使用 select 等待客户端连接 */
    LOG_INFO("tFwUpdateSrv: [*] Waiting for client connection...\n", 0,0,0,0,0,0);

    while (1)
    {
        /* 清空文件描述符集合 */
        FD_ZERO(&read_fds);
        FD_SET(server_sock, &read_fds);

        /* 设置超时时间 (30秒) */
        timeout.tv_sec = 30;
        timeout.tv_usec = 0;

        /* 等待服务器套接字变得可读 (有新的连接) */
        select_ret = select(server_sock + 1, &read_fds, NULL, NULL, &timeout);

        if (select_ret == ERROR)
        {
            LOG_INFO("tFwUpdateSrv: [!] Select failed, retrying...\n", 0,0,0,0,0,0);
            taskDelay(100);  /* 延迟 100 tick 后重试 */
            continue;
        }
        else if (select_ret == 0)
        {
            /* 超时，继续等待 */
            LOG_INFO("tFwUpdateSrv: [*] Waiting for client connection...\n", 0,0,0,0,0,0);
            continue;
        }

        /* 检查服务器套接字是否可读 */
        if (FD_ISSET(server_sock, &read_fds))
        {
            client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);

            if (client_sock == ERROR) 
            {
                LOG_INFO("tFwUpdateSrv: [!] Accept failed, retrying...\n", 0,0,0,0,0,0);
                taskDelay(100);  /* 延迟 100 tick 后重试 */
                continue;
            }

            LOG_INFO("tFwUpdateSrv: [+] Client %s connected\n", inet_ntoa(client_addr.sin_addr), 0,0,0,0,0);

            /* 处理这个客户端 (阻塞) */
            if (handle_client(client_sock) == ERROR)
            {
                LOG_INFO("tFwUpdateSrv: [!] Client handling failed or disconnected\n", 0,0,0,0,0,0);
            }

            close(client_sock);
            LOG_INFO("tFwUpdateSrv: [*] Client disconnected, waiting for next...\n", 0,0,0,0,0,0);
        }
    }

    /* 5. 清理 */
    close(server_sock);
    serverTaskId = 0;
    LOG_INFO("tFwUpdateSrv: [*] Server task shutting down.\n", 0,0,0,0,0,0);
    return NULL;
}


/*
 * =================================================================
 * 客户端处理 (接收文件)
 * =================================================================
 */
STATUS handle_client(int client_sock)
{
    uint32_t net_size;
    int file_size;
    char *file_buffer = NULL;
    int bytes_read = 0;
    int total_bytes_read = 0;

    /* 1. 接收 4 字节的文件总长度 (网络字节序) */
    bytes_read = recv(client_sock, (char *)&net_size, sizeof(net_size), 0);
    if (bytes_read != sizeof(net_size))
    {
        LOG_INFO("handle_client: [X] Failed to receive file size (read %d bytes)\n", bytes_read);
        return ERROR;
    }

    LOG_INFO("handle_client: [*] Received file size: %x bytes\n", net_size);
    file_size = ntohl(net_size); /* 转换为 主机 字节序 */

    /* 2. 安全检查: 检查文件大小是否过大 */
    if (file_size == 0 || file_size > FW_MAX_PACKAGE_SIZE)
    {
        LOG_INFO("handle_client: [X] Invalid or oversized file size (Size: %u)\n", file_size);
        send_status(client_sock, STATUS_ERROR); /* (V4) 发送错误 */
        return ERROR;
    }

    LOG_INFO("handle_client: [*] Received file size: 0x%x bytes\n", file_size);

    /* 3. 分配 RAM 缓冲区 (关键) */
    file_buffer = (char *)malloc(file_size);
    if (file_buffer == NULL)
    {
        LOG_INFO("handle_client: [X] Fatal error: Malloc failed (cannot allocate %u bytes)\n", file_size);
        send_status(client_sock, STATUS_ERROR); /* (V4) 发送错误 */
        return ERROR;
    }

    /* 4. 循环接收, 直到收满 'file_size' 字节 */
    LOG_INFO("handle_client: [*] Receiving file...\n");
    while (total_bytes_read < file_size)
    {
        bytes_read = recv(client_sock, file_buffer + total_bytes_read, file_size - total_bytes_read, 0);

        if (bytes_read <= 0)
        {
            /* 客户端提前断开连接 */
            LOG_INFO("handle_client: [X] Receive interrupted (at %d bytes)\n", total_bytes_read);
            free(file_buffer);
            /* (V4) 不需要发送错误, 因为连接已断 */
            return ERROR;
        }
        total_bytes_read += bytes_read;

        //打印接收进度
        LOG_INFO("handle_client: [*] Receive progress: %d/%d bytes\n", total_bytes_read, file_size);
    }

    
    /* 5. 【关键】处理这个缓冲区中的文件 (校验、擦写、发送应答) */
    if (process_firmware_package(client_sock, file_buffer, file_size) != OK)
    {
        LOG_INFO("handle_client: [X] Firmware processing failed.\n");
        /* (process_firmware_package 已经发送了 STATUS_ERROR) */
        free(file_buffer);
        return ERROR;
    }

    /* 6. 清理 */
    free(file_buffer);
    LOG_INFO("handle_client: [+] Firmware update process successful.\n");
    return OK;
}


/*
 * =================================================================
 * 固件包校验 (封装的校验逻辑)
 * =================================================================
 */
static STATUS validate_firmware_package(
    char *buffer,
    long size,
    int client_sock,
    fw_package_header_t **header,
    const char **bit_data,
    const char **os_data,
    const char **app_data,
    const char **kernel_data,
    const char **rootfs_data
)
{
    uint32_t header_crc_calc, bit_crc_calc, os_crc_calc, app_crc_calc;
    uint32_t expected_total_size;
    uint32_t data_offset;

    /* 1. 结构体映射 (Casting) */
    if (size < (long)sizeof(fw_package_header_t))
    {
        LOG_INFO("validate_pkg: [X] Error: File size (%ld) is smaller than header size (%d)\n", size, (int)sizeof(fw_package_header_t));
        send_status(client_sock, STATUS_ERROR);
        return ERROR;
    }
    *header = (fw_package_header_t *)buffer;

    //printf("buf[0]: %x buf[1]: %x buf[2]: %x buf[3]: %x\n", buffer[0], buffer[1], buffer[2], buffer[3]);
    printf("header->magic_number: %x\n", (*header)->magic_number);

    /* 2. 【校验 #1: Magic Number】 */
    if ((*header)->magic_number != FW_PACKAGE_MAGIC)
    {
        LOG_INFO("validate_pkg: [X] Fatal error: Magic Number mismatch! (Expected: 0x%X, Got: 0x%X)\n", FW_PACKAGE_MAGIC, (*header)->magic_number);
        send_status(client_sock, STATUS_ERROR);
        return ERROR;
    }
    LOG_INFO("validate_pkg: [*] Verification 1/5: Magic Number OK (0x%X)\n", (*header)->magic_number);

    /* 3. 【校验 #2: Header CRC】 */
    /* (从偏移量 8 (update_flags) 开始, 到头部末尾) */
    const void *header_data_ptr = (const void *)(*header) + 8;
    size_t header_data_len = FW_PACKAGE_HEADER_SIZE - 8;
    header_crc_calc = calculate_crc32(0, header_data_ptr, header_data_len);

    LOG_INFO("validate_pkg: header_data_len: %d, header_crc_calc: 0x%x\n", header_data_len, header_crc_calc);

    if (header_crc_calc != (*header)->header_crc32)
    {
        LOG_INFO("validate_pkg: [X] Fatal error: Header CRC verification failed! (Expected: 0x%X, Got: 0x%X)\n", (*header)->header_crc32, header_crc_calc);
        send_status(client_sock, STATUS_ERROR);
        return ERROR;
    }
    LOG_INFO("validate_pkg: [*] Verification 2/5: Header CRC OK (0x%X)\n", (*header)->header_crc32);

    /* 4. 【校验 #3: 总大小】 */
    LOG_INFO("validate_pkg: HEADER_SIZE: %d, bit_length: %d, os_length: %d, app_length: %d, kernel_length: %d, rootfs_length: %d\n",
             FW_PACKAGE_HEADER_SIZE, (*header)->bit_length, (*header)->os_length, (*header)->app_length, (*header)->kernel_length, (*header)->rootfs_length);
    expected_total_size = FW_PACKAGE_HEADER_SIZE + (*header)->bit_length + (*header)->os_length + (*header)->app_length + (*header)->kernel_length + (*header)->rootfs_length;
    if (size != (long)expected_total_size)
    {
        LOG_INFO("validate_pkg: [X] Fatal error: Total file size mismatch! (Expected: %u, Got: %ld)\n", expected_total_size, size);
        send_status(client_sock, STATUS_ERROR);
        return ERROR;
    }
    LOG_INFO("validate_pkg: [*] Verification 3/5: Total size OK (%u bytes)\n", (unsigned int)size);

    /* 5. 定位数据 */
    data_offset = FW_PACKAGE_HEADER_SIZE;
    *bit_data = buffer + data_offset;

    data_offset += (*header)->bit_length;
    *os_data = buffer + data_offset;

    data_offset += (*header)->os_length;
    *app_data = buffer + data_offset;

    data_offset += (*header)->app_length;
    *kernel_data = buffer + data_offset;

    data_offset += (*header)->kernel_length;
    *rootfs_data = buffer + data_offset;

    /* 6. 【校验 #4: Bitstream CRC】 (如果需要更新) */
    if ((*header)->update_flags & UPDATE_FLAG_BITSTREAM)
    {
        if ((*header)->bit_length == 0)
        {
            LOG_INFO("validate_pkg: [X] Error: update_flags indicates Bitstream update, but bit_length is 0\n");
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }
        LOG_INFO("validate_pkg: [*] Verifying Bitstream (%u bytes)...\n", (*header)->bit_length);
        bit_crc_calc = calculate_crc32(0, *bit_data, (*header)->bit_length);
        if (bit_crc_calc != (*header)->bit_crc32)
        {
            LOG_INFO("validate_pkg: [X] Fatal error: Bitstream CRC verification failed! (Expected: 0x%X, Got: 0x%X)\n", (*header)->bit_crc32, bit_crc_calc);
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }
        LOG_INFO("validate_pkg: [*] Verification 4/5: Bitstream CRC OK (0x%X)\n", (*header)->bit_crc32);
    }
    else
    {
        LOG_INFO("validate_pkg: [*] Skip Bitstream verification (no update needed)\n", 0,0,0,0,0,0);
    }

    /* 7. 【校验 #5: OS CRC】 (如果需要更新) */
    if ((*header)->update_flags & UPDATE_FLAG_OS)
    {
        if ((*header)->os_length == 0)
        {
            LOG_INFO("validate_pkg: [X] Error: update_flags indicates OS update, but os_length is 0\n");
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }
        LOG_INFO("validate_pkg: [*] Verifying OS (%u bytes)...\n", (*header)->os_length);
        os_crc_calc = calculate_crc32(0, *os_data, (*header)->os_length);
        if (os_crc_calc != (*header)->os_crc32)
        {
            LOG_INFO("validate_pkg: [X] Fatal error: OS CRC verification failed! (Expected: 0x%X, Got: 0x%X)\n", (*header)->os_crc32, os_crc_calc);
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }
        LOG_INFO("validate_pkg: [*] Verification 5/5: OS CRC OK (0x%X)\n", (*header)->os_crc32);
    }
    else
    {
        LOG_INFO("validate_pkg: [*] Skip OS verification (no update needed)\n", 0,0,0,0,0,0);
    }

    /* 8. 【校验 #6: Application CRC】 (如果需要更新) */
    if ((*header)->update_flags & UPDATE_FLAG_APP)
    {
        if ((*header)->app_length == 0)
        {
            LOG_INFO("validate_pkg: [X] Error: update_flags indicates App update, but app_length is 0\n");
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }
        LOG_INFO("validate_pkg: [*] Verifying Application (%u bytes)...\n", (*header)->app_length);

        app_crc_calc = calculate_crc32(0, *app_data, (*header)->app_length);
        if (app_crc_calc != (*header)->app_crc32)
        {
            LOG_INFO("validate_pkg: [X] Fatal error: Application CRC verification failed! (Expected: 0x%X, Got: 0x%X)\n", (*header)->app_crc32, app_crc_calc);
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }
        LOG_INFO("validate_pkg: [*] Verification 6/5: Application CRC OK (0x%X)\n", (*header)->app_crc32);
    }
    else
    {
        LOG_INFO("validate_pkg: [*] Skip Application verification (no update needed)\n");
    }

    /* 8. 【校验 #7: KERNEL CRC】 (如果需要更新) */
    if ((*header)->update_flags & UPDATE_FLAG_KERNEL)
    {
        if ((*header)->kernel_length == 0)
        {
            LOG_ERROR("validate_pkg: [X] Error: update_flags indicates KERNEL update, but kernel_length is 0\n");
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }

        LOG_INFO("validate_pkg: [*] Verifying KERNEL (%u bytes)...\n", (*header)->kernel_length);
        if (calculate_crc32(0, *kernel_data, (*header)->kernel_length) != (*header)->kernel_crc32)
        {
            LOG_ERROR("validate_pkg: [X] Error: KERNEL verification failed\n");
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }
        LOG_INFO("validate_pkg: [*] Verification 7/5: KERNEL CRC OK (0x%X)\n", (*header)->kernel_crc32);
    }
    else
    {
        LOG_INFO("validate_pkg: [*] Skip KERNEL verification (no update needed)\n");
    }

    /* 8. 【校验 #8: rootfs CRC】 (如果需要更新) */
    if ((*header)->update_flags & UPDATE_FLAG_ROOTFS)
    {
        if ((*header)->rootfs_length == 0)
        {
            LOG_ERROR("validate_pkg: [X] Error: update_flags indicates rootfs update, but rootfs_length is 0\n");
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }

        LOG_INFO("validate_pkg: [*] Verifying rootfs (%u bytes)...\n", (*header)->rootfs_length);
        if (calculate_crc32(0, *rootfs_data, (*header)->rootfs_length) != (*header)->rootfs_crc32)
        {
            LOG_ERROR("validate_pkg: [X] Error: rootfs verification failed\n");
            send_status(client_sock, STATUS_ERROR);
            return ERROR;
        }

        LOG_INFO("validate_pkg: [*] Verification 8/5: rootfs CRC OK (0x%X)\n", (*header)->rootfs_crc32);
    }
    else
    {
        LOG_INFO("validate_pkg: [*] Verification 8/5: rootfs CRC skipped\n");
    }

    LOG_INFO("validate_pkg: [+] All verifications passed!\n");
    return OK;
}

/*
 * =================================================================
 * 固件包处理 (校验、写入)
 * =================================================================
 */
STATUS process_firmware_package(int client_sock, char *buffer, long size)
{
    fw_package_header_t *header = NULL;
    const char *bit_data = NULL;
    const char *os_data = NULL;
    const char *app_data = NULL;
    const char *kernel_data = NULL;
    const char *rootfs_data = NULL;

    /* 1. 校验固件包 */
    if (validate_firmware_package(buffer, size, client_sock, &header, &bit_data, &os_data, &app_data, &kernel_data, &rootfs_data) != OK)
    {
        LOG_INFO("process_pkg: [X] Firmware package verification failed.\n");
        return ERROR;
    }

    LOG_INFO("process_pkg: [+] Firmware package verification successful, preparing to write to FLASH.\n");

    /* 2. 【(V4) 发送应答 #1: 校验 OK】 */
    if (send_status(client_sock, STATUS_OK_TO_PROCEED) != 0)
    {
        LOG_INFO("process_pkg: [!] Failed to send OK_TO_PROCEED (client may be disconnected)\n");
        return ERROR; /* 停止, 不写入 FLASH */
    }

    /* 3. 【关键】执行 FLASH 擦写 (慢速操作) */
    /* (我们传入 header 指针, 以便 write 函数可以访问大小信息) */
    if (write_firmware_to_flash(header, bit_data, kernel_data, rootfs_data, app_data) != OK)
    {
        LOG_INFO("process_pkg: [X] Fatal error: FLASH write failed!\n");
        send_status(client_sock, STATUS_ERROR);
        return ERROR;
    }

    /* 4. 【(V4) 发送应答 #2: 写入完成】 */
    LOG_INFO("process_pkg: [*] FLASH write successful, sending final confirmation...\n");
    if (send_status(client_sock, STATUS_WRITE_COMPLETE) != 0)
    {
        LOG_INFO("process_pkg: [!] Failed to send WRITE_COMPLETE (client may be disconnected)\n");
        /* * 此时已写入成功, 即使发送失败也返回 OK
         * (因为客户端只是没收到通知, 但更新已完成)
         */
    }

    return OK;
}

/*
 * =================================================================
 * eMMC A/B 交换升级写入逻辑
 *
 * 当前从 A 启动 -> 写 B；当前从 B 启动 -> 写 A。
 * 写入完成后才切换 boot_slot，并保留 boot_slot_prev 供回退。
 * =================================================================
 */
STATUS write_firmware_to_flash(
    const fw_package_header_t *header,
    const char *bit_data,
    const char *kernel_data,
    const char *rootfs_data,
    const char *app_data)
{
    FirmwareSlot current;
    FirmwareSlot target;
    char current_name[16] = {0};
    char target_name[16] = {0};

    if (detect_ab_slots(&current, &target,
                        current_name, sizeof(current_name),
                        target_name, sizeof(target_name)) != OK)
    {
        printf("write_flash: [X] Failed to detect current boot slot\n");
        return ERROR;
    }

    printf("write_flash: current slot=%s, target slot=%s\n", current_name, target_name);
    printf("write_flash: target image=%s, target rootfs=%s\n",
           target.image_dev, target.rootfs_dev);

    if (header->update_flags & UPDATE_FLAG_OS)
    {
        printf("write_flash: OS segment is reserved, skip writing\n");
    }

    if (header->update_flags & UPDATE_FLAG_BITSTREAM)
    {
        printf("write_flash: writing %u bytes bitstream to %s:%s\n",
               header->bit_length, target.image_dev, BITSTREAM_FILE_NAME);
        if (write_mem_to_mounted_file(bit_data, header->bit_length,
                                      target.image_dev, IMAGE_MOUNT_POINT,
                                      "vfat", BITSTREAM_FILE_NAME, 0644) != 0)
        {
            printf("write_flash: [X] failed to write bitstream\n");
            return ERROR;
        }
    }

    if (header->update_flags & UPDATE_FLAG_KERNEL)
    {
        printf("write_flash: writing %u bytes kernel to %s:%s\n",
               header->kernel_length, target.image_dev, KERNEL_FILE_NAME);
        if (write_mem_to_mounted_file(kernel_data, header->kernel_length,
                                      target.image_dev, IMAGE_MOUNT_POINT,
                                      "vfat", KERNEL_FILE_NAME, 0644) != 0)
        {
            printf("write_flash: [X] failed to write kernel\n");
            return ERROR;
        }
    }

    if (header->update_flags & UPDATE_FLAG_ROOTFS)
    {
        printf("write_flash: writing %u bytes rootfs to %s\n",
               header->rootfs_length, target.rootfs_dev);
        if (write_rootfs_to_target(rootfs_data, header->rootfs_length,
                                   target.rootfs_dev, ROOTFS_MOUNT_POINT) != 0)
        {
            printf("write_flash: [X] failed to write rootfs\n");
            return ERROR;
        }
    }

    if (header->update_flags & UPDATE_FLAG_APP)
    {
        printf("write_flash: writing %u bytes app to %s:%s\n",
               header->app_length, target.rootfs_dev, APP_INSTALL_PATH_IN_ROOTFS);
        if (write_mem_to_mounted_file(app_data, header->app_length,
                                      target.rootfs_dev, ROOTFS_MOUNT_POINT,
                                      "ext4", APP_INSTALL_PATH_IN_ROOTFS, 0755) != 0)
        {
            printf("write_flash: [X] failed to write app\n");
            return ERROR;
        }
    }

    if (finalize_upgrade(current_name, target_name) != OK)
    {
        printf("write_flash: [X] failed to update U-Boot environment\n");
        return ERROR;
    }

    printf("write_flash: [OK] slot %s updated, next boot slot=%s\n",
           target_name, target_name);
    return OK;
}

/*
 * =================================================================
 * 旧实现：MTD/QSPI flashcp 升级逻辑（保留但屏蔽）
 *
 * 该实现依赖 /dev/mtd5、/dev/mtd6 和 flashcp，
 * 仅适用于旧板卡。新方案默认使用上面的 eMMC A/B 交换升级逻辑。
 * 如需恢复旧方案，需启用本段并将上面的新实现屏蔽。
 * =================================================================
 */
#if 0

const char *mtd_dev_image_b = "/dev/mtd5";
const char *mtd_dev_rootfs_b = "/dev/mtd6";
#define TEMP_FILE "/tmp/image.ub"

static int write_mem_to_temp(const void *buf, size_t buf_len, const char *temp_path);
static int flashcp_write(const char *src_file, const char *mtd_device);
static int erase_mtd_partition(const char *mtd_device);

STATUS write_firmware_to_flash(
    const fw_package_header_t *header,
    const char *bit_data,
    const char *kernel_data,
    const char *rootfs_data,
    const char *app_data)
{
    char size_str[12]; /* 用于整数到字符串的转换 */

    /*
     * 【安全升级机制 - 容错保护】
     *
     * 重要原则：升级过程中任何时刻断电，系统都必须能正常启动！
     *
     * 断电场景分析：
     *   - 假设当前系统 bootpart=1，从 mtd7 启动
     *   - 如果正在升级 mtd7 时断电，mtd7 数据损坏
     *   - 如果 bootpart 仍然是 1，系统从损坏的 mtd7 启动 → 启动失败！
     *
     * 解决方案：
     *   1. 在写入任何固件之前，先设置 bootpart=0
     *   2. 整个升级过程中，bootpart=0 保证可以从安全分区 mtd6 启动
     *   3. 升级成功后才切换回 bootpart=1
     *
     * 完整流程：
     *   STEP0: 设置 bootpart=0（确保安全分区）
     *   STEP1: 写入固件到 mtd7
     *   STEP2: 设置环境变量到 mtd2
     *   STEP3: 擦除 mtd2，重新设置所有变量
     *   STEP4: 最后设置 bootpart=1（切换到新分区）
     */

    /* --- 步骤 0: 设置 bootpart=0 确保安全分区 --- */
    /* 注意：这个步骤必须在写入任何固件之前执行！ */
    LOG_INFO("write_flash: [STEP0] [SAFE-UPGRADE] Setting bootpart=0 to ensure safe boot partition...\n");
    LOG_INFO("write_flash: [STEP0] During upgrade, system will boot from safe partition (mtd6) if power fails.\n");
    if (set_uboot_env("bootpart", "0") != 0)
    {
        LOG_ERROR("write_flash: [STEP0] [ERROR] Failed to set bootpart=0! Cannot proceed with upgrade.\n");
        return -1;
    }
    LOG_INFO("write_flash: [STEP0] [OK] bootpart=0 set. Safe boot partition ensured.\n");

    /* --- 步骤 1: 写入 Bitstream 到分区 B (如果需要) --- */
    if (header->update_flags & UPDATE_FLAG_BITSTREAM)
    {
        LOG_INFO("write_flash: writing %u bytes Bitstream..\n", header->bit_length);

        if (write_mem_to_temp(bit_data, header->bit_length, TEMP_FILE) != 0)
        {
            LOG_ERROR("write_flash: [SAFE-UPGRADE] Write bit temp file failed! bootpart=0 (safe partition).\n");
            return -1;
        }

        LOG_INFO("write_flash: writing Bitstream to BitpartitionB...\n");
        if (flashcp_write(TEMP_FILE, mtd_dev_image_b) != 0)
        {
            LOG_INFO("write_flash: [SAFE-UPGRADE] Writing Bitstream B failed! bootpart=0 (safe partition).\n");
            return -1;
        }
        LOG_INFO("write_flash: Bitstream B writing succeed!\n");
    }
    else
    {
        LOG_INFO("write_flash: Bitstream NOT updated, skip writing\n");
    }

    /* --- 步骤 2: 写入KERNEL(如果需要) --- */
    if (header->update_flags & UPDATE_FLAG_KERNEL)
    {
        LOG_INFO("write_flash: Writing %u bytes KERNEL ...\n", header->kernel_length);

        if (write_mem_to_temp(kernel_data, header->kernel_length, TEMP_FILE) != 0)
        {
            LOG_ERROR("write_flash: [SAFE-UPGRADE] Write kernel temp file failed! bootpart=0 (safe partition).\n");
            return -1;
        }
        LOG_INFO("write_flash: writing imageb to partitionB...\n");
        if (flashcp_write(TEMP_FILE, mtd_dev_image_b) != 0)
        {
            LOG_INFO("write_flash: [SAFE-UPGRADE] Writing imageb to partitionB failed! bootpart=0 (safe partition).\n");
            return -1;
        }
        LOG_INFO("write_flash: writing imageb to partitionB OK!\n");
    }
    else
    {
        LOG_INFO("write_flash: kernel NOT updated, skip writing\n");
    }

    // 更新rootfs，如果需要
    if (header->update_flags & UPDATE_FLAG_ROOTFS)
    {
        LOG_INFO("write_falsh: writing %u bytes ROOTFS...\n", header->rootfs_length);

        if (write_mem_to_temp(rootfs_data, header->rootfs_length, TEMP_FILE) != 0)
        {
            LOG_ERROR("write_flash: [SAFE-UPGRADE] Write rootfs temp file failed! bootpart=0 (safe partition).\n");
            return -1;
        }
        LOG_INFO("write_flash: writing rootfs to partitionB...\n");
        if (flashcp_write(TEMP_FILE, mtd_dev_rootfs_b) != 0)
        {
            LOG_INFO("write_flash: [SAFE-UPGRADE] Writing rootfs to partitionB failed! bootpart=0 (safe partition).\n");
            return -1;
        }
        LOG_INFO("write_flash: writing rootfs to partitionB OK!\n");
    }
    else
    {
        LOG_INFO("write_flash: rootfs NOT updated, skip writing\n");
    }

    /* --- 步骤 3: 写入APP到文件系统 (如果需要) --- */
    if (header->update_flags & UPDATE_FLAG_APP)
    {
        LOG_INFO("write_flash: writing %u bytes APP to /tmp/app/app...\n", header->app_length);

        if (write_mem_to_temp(app_data, header->app_length, "/tmp/app/app") != 0)
        {
            LOG_ERROR("write_flash: [SAFE-UPGRADE] Write app file failed!\n");
            return -1;
        }

        if (chmod("/tmp/app/app", 0755) != 0)
        {
            LOG_ERROR("write_flash: [WARNING] chmod app failed!\n");
        }
        LOG_INFO("write_flash: APP writing succeed!\n");
    }
    else
    {
        LOG_INFO("write_flash: APP NOT updated, skip writing\n");
    }

    /* --- 步骤 4: 最后设置 bootpart=1 (切换到新分区) --- */
    LOG_INFO("write_flash: [STEP4] [SAFE-UPGRADE] Finalizing firmware upgrade...\n");

    if (erase_mtd_partition("/dev/mtd2") != 0)
    {
        LOG_ERROR("write_flash: [STEP4] [ERROR] Failed to erase mtd2! Upgrade failed, bootpart stays at safe value.\n");
        return -1;
    }
    LOG_INFO("write_flash: [STEP4a] mtd2 erased.\n");

    if (header->update_flags & UPDATE_FLAG_KERNEL)
    {
        sprintf(size_str, "%d", header->kernel_length);
        set_uboot_env("image_b_size", size_str);
    }

    if (header->update_flags & UPDATE_FLAG_BITSTREAM)
    {
        sprintf(size_str, "%u", header->bit_length);
        set_uboot_env("bitstream_b_size", size_str);
        set_uboot_env("fpgapart", "1");
    }

    if (set_uboot_env("bootpart", "1") != 0)
    {
        LOG_ERROR("write_flash: [STEP4e] [ERROR] Failed to set bootpart=1! System will boot from safe partition (mtd6).\n");
        return -1;
    }

    LOG_INFO("write_flash: [COMPLETE] All firmware upgrade completed successfully!\n");
    return OK;
}

/**
 * @brief 将内存数据写入临时文件（旧实现）
 */
static int write_mem_to_temp(const void *buf, size_t buf_len, const char *temp_path)
{
    int fd = open(temp_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        fprintf(stderr, "Failed to create temp file: %s, err=%s\n", temp_path, strerror(errno));
        return -1;
    }

    size_t ret = write(fd, buf, buf_len);
    if (ret != buf_len)
    {
        fprintf(stderr, "Failed to write temp file: Expected %lu B, Actual %zd B\n",
                (unsigned long)buf_len, ret);
        close(fd);
        unlink(temp_path);
        return -1;
    }

    close(fd);
    printf("Memory data written to temp file: %s (size: %lu B)\n", temp_path, (unsigned long)buf_len);
    return 0;
}

/**
 * @brief 封装 flashcp 命令，向 MTD 设备写入文件（旧实现）
 */
static int flashcp_write(const char *src_file, const char *mtd_device)
{
    char cmd[256];
    int ret;

    if (src_file == NULL || mtd_device == NULL)
    {
        fprintf(stderr, "Parameters cannot be NULL\n");
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "flashcp -v %s %s", src_file, mtd_device);

    printf("cmd: %s\n", cmd);
    ret = system(cmd);
    if (ret == -1)
    {
        fprintf(stderr, "Failed to execute flashcp: %s\n", strerror(errno));
        return -1;
    }
    else if (WEXITSTATUS(ret) != 0)
    {
        fprintf(stderr, "flashcp execution error, exit code: %d\n", WEXITSTATUS(ret));
        return -1;
    }

    printf("Data successfully written to %s\n", mtd_device);
    return 0;
}

/**
 * @brief 擦除 MTD 分区（旧实现）
 */
static int erase_mtd_partition(const char *mtd_device)
{
    if (mtd_device == NULL)
    {
        fprintf(stderr, "erase_mtd_partition: mtd_device is NULL\n");
        return -1;
    }

    int fd = open(mtd_device, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "erase_mtd_partition: Failed to open %s: %s\n",
                mtd_device, strerror(errno));
        return -1;
    }

    struct mtd_info_user mtd_info;
    if (ioctl(fd, MEMGETINFO, &mtd_info) < 0)
    {
        fprintf(stderr, "erase_mtd_partition: MEMGETINFO ioctl failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    struct erase_info_user erase;
    erase.start = 0;
    erase.length = mtd_info.size;

    printf("erase_mtd_partition: Erasing %s (%u bytes)...\n",
           mtd_device, mtd_info.size);

    if (ioctl(fd, MEMERASE, &erase) < 0)
    {
        fprintf(stderr, "erase_mtd_partition: MEMERASE ioctl failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    printf("erase_mtd_partition: Erase completed successfully\n");
    return 0;
}

#endif /* 旧 MTD/flashcp 实现 */


static int ensure_mount_point(const char *mnt)
{
    struct stat st = {0};

    if (mnt == NULL || mnt[0] == '\0')
    {
        return -1;
    }

    if (stat(mnt, &st) == 0)
    {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    if (mkdir(mnt, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "Failed to create mount point %s: %s\n", mnt, strerror(errno));
        return -1;
    }

    return 0;
}

static int ensure_parent_directory(const char *path)
{
    char tmp[512] = {0};
    char cmd[1024] = {0};
    char *slash;

    if (path == NULL || strlen(path) >= sizeof(tmp))
    {
        return -1;
    }

    strcpy(tmp, path);
    slash = strrchr(tmp, '/');
    if (slash == NULL || slash == tmp)
    {
        return 0;
    }
    *slash = '\0';

    snprintf(cmd, sizeof(cmd), "mkdir -p %s", tmp);
    if (system(cmd) != 0)
    {
        fprintf(stderr, "Failed to create directory %s\n", tmp);
        return -1;
    }

    return 0;
}

static int write_mem_to_file(const void *buf, size_t buf_len, const char *path, mode_t mode)
{
    int fd;
    size_t offset = 0;
    mode_t final_mode = mode ? mode : 0644;

    if (buf == NULL || path == NULL)
    {
        return -1;
    }

    if (ensure_parent_directory(path) != 0)
    {
        return -1;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, final_mode);
    if (fd < 0)
    {
        fprintf(stderr, "Failed to open %s for writing: %s\n", path, strerror(errno));
        return -1;
    }

    while (offset < buf_len)
    {
        ssize_t written = write(fd, (const char *)buf + offset, buf_len - offset);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "Failed to write %s: %s\n", path, strerror(errno));
            close(fd);
            return -1;
        }
        if (written == 0)
        {
            break;
        }
        offset += (size_t)written;
    }

    if (fsync(fd) != 0)
    {
        fprintf(stderr, "Warning: fsync %s failed: %s\n", path, strerror(errno));
    }
    close(fd);

    if (offset != buf_len)
    {
        fprintf(stderr, "Short write to %s: %zu/%zu bytes\n", path, offset, buf_len);
        return -1;
    }

    if (chmod(path, final_mode) != 0)
    {
        fprintf(stderr, "Warning: chmod %s failed: %s\n", path, strerror(errno));
    }

    return 0;
}

static int write_mem_to_block_device(const void *buf, size_t buf_len, const char *dev)
{
    int fd;
    size_t offset = 0;

    if (buf == NULL || dev == NULL)
    {
        return -1;
    }

    /* 目标分区不应被挂载；若已经挂载则先卸载。未挂载时 EINVAL 可忽略。 */
    if (umount(dev) != 0 && errno != EINVAL)
    {
        fprintf(stderr, "Warning: umount %s failed: %s\n", dev, strerror(errno));
    }

    fd = open(dev, O_WRONLY | O_SYNC);
    if (fd < 0)
    {
        fprintf(stderr, "Failed to open %s: %s\n", dev, strerror(errno));
        return -1;
    }

    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        fprintf(stderr, "Failed to seek %s: %s\n", dev, strerror(errno));
        close(fd);
        return -1;
    }

    while (offset < buf_len)
    {
        ssize_t written = write(fd, (const char *)buf + offset, buf_len - offset);
        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "Failed to write %s: %s\n", dev, strerror(errno));
            close(fd);
            return -1;
        }
        if (written == 0)
        {
            break;
        }
        offset += (size_t)written;
    }

    if (fsync(fd) != 0)
    {
        fprintf(stderr, "Warning: fsync %s failed: %s\n", dev, strerror(errno));
    }
    close(fd);
    sync();

    if (offset != buf_len)
    {
        fprintf(stderr, "Short write to %s: %zu/%zu bytes\n", dev, offset, buf_len);
        return -1;
    }

    return 0;
}

static int write_mem_to_mounted_file(const void *buf, size_t buf_len,
                                     const char *dev, const char *mnt,
                                     const char *fstype, const char *path_in_fs,
                                     mode_t mode)
{
    char full_path[512] = {0};
    const char *rel;
    int ret = -1;
    int n;

    if (buf == NULL || dev == NULL || mnt == NULL || fstype == NULL || path_in_fs == NULL)
    {
        return -1;
    }

    if (ensure_mount_point(mnt) != 0)
    {
        return -1;
    }

    if (umount(mnt) != 0 && errno != EINVAL)
    {
        fprintf(stderr, "Warning: umount %s failed: %s\n", mnt, strerror(errno));
    }

    if (mount(dev, mnt, fstype, 0, NULL) != 0)
    {
        fprintf(stderr, "Failed to mount %s (%s) at %s: %s\n",
                dev, fstype, mnt, strerror(errno));
        return -1;
    }

    rel = path_in_fs;
    while (*rel == '/')
    {
        rel++;
    }

    n = snprintf(full_path, sizeof(full_path), "%s/%s", mnt, rel);
    if (n < 0 || n >= (int)sizeof(full_path))
    {
        fprintf(stderr, "Path too long: %s/%s\n", mnt, rel);
        umount(mnt);
        return -1;
    }

    ret = write_mem_to_file(buf, buf_len, full_path, mode);
    if (ret == 0)
    {
        sync();
    }

    if (umount(mnt) != 0)
    {
        fprintf(stderr, "Failed to umount %s: %s\n", mnt, strerror(errno));
        ret = -1;
    }

    return ret;
}

static int is_ext4_image(const unsigned char *buf, size_t len)
{
    /*
     * ext2/ext3/ext4 superblock magic is 0xEF53.
     * On little-endian systems it is stored at offset 0x438 as 0x53, 0xEF.
     */
    return len > (0x438 + 2) && buf[0x438] == 0x53 && buf[0x439] == 0xEF;
}

static int is_gzip_data(const unsigned char *buf, size_t len)
{
    return len >= 2 && buf[0] == 0x1F && buf[1] == 0x8B;
}

static int is_tar_data(const unsigned char *buf, size_t len)
{
    return len > 262 && memcmp(buf + 257, "ustar", 5) == 0;
}

static int extract_tar_to_mounted_fs(const void *buf, size_t len,
                                     const char *dev, const char *mnt,
                                     int gzip)
{
    char cmd[256] = {0};
    FILE *fp;
    size_t offset = 0;
    int ret = -1;
    int close_ret;

    if (buf == NULL || dev == NULL || mnt == NULL)
    {
        return -1;
    }

    if (ensure_mount_point(mnt) != 0)
    {
        return -1;
    }

    if (umount(mnt) != 0 && errno != EINVAL)
    {
        fprintf(stderr, "Warning: umount %s failed: %s\n", mnt, strerror(errno));
    }

    if (mount(dev, mnt, "ext4", 0, NULL) != 0)
    {
        fprintf(stderr, "Failed to mount %s at %s: %s\n", dev, mnt, strerror(errno));
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "tar -%s -C %s", gzip ? "xz" : "x", mnt);
    fp = popen(cmd, "w");
    if (fp == NULL)
    {
        fprintf(stderr, "Failed to run %s: %s\n", cmd, strerror(errno));
        umount(mnt);
        return -1;
    }

    while (offset < len)
    {
        size_t written = fwrite((const char *)buf + offset, 1, len - offset, fp);
        if (written == 0)
        {
            break;
        }
        offset += written;
    }

    close_ret = pclose(fp);
    if (offset != len || close_ret != 0)
    {
        fprintf(stderr, "tar extract failed: written=%zu/%zu, ret=%d\n",
                offset, len, close_ret);
    }
    else
    {
        ret = 0;
        sync();
    }

    if (umount(mnt) != 0)
    {
        fprintf(stderr, "Failed to umount %s: %s\n", mnt, strerror(errno));
        ret = -1;
    }

    return ret;
}

static int write_rootfs_to_target(const void *buf, uint32_t len,
                                  const char *dev, const char *mnt)
{
    const unsigned char *data = (const unsigned char *)buf;

    if (data == NULL || dev == NULL || mnt == NULL)
    {
        return -1;
    }

    if (is_ext4_image(data, len))
    {
        printf("rootfs: detected raw ext2/ext3/ext4 image, writing to %s\n", dev);
        return write_mem_to_block_device(data, len, dev);
    }

    if (is_gzip_data(data, len) || is_tar_data(data, len))
    {
        int gzip = is_gzip_data(data, len);
        printf("rootfs: detected %s archive, extracting to %s\n",
               gzip ? "tar.gz" : "tar", mnt);
        return extract_tar_to_mounted_fs(data, len, dev, mnt, gzip);
    }

    fprintf(stderr, "Unsupported rootfs format. Use a raw ext2/ext3/ext4 image or tar/tar.gz archive.\n");
    return -1;
}

static int detect_ab_slots(FirmwareSlot *current, FirmwareSlot *target,
                           char *current_name, size_t current_name_len,
                           char *target_name, size_t target_name_len)
{
    char env_slot[32] = {0};
    int current_is_b = 0;

    if (current == NULL || target == NULL ||
        current_name == NULL || target_name == NULL ||
        current_name_len == 0 || target_name_len == 0)
    {
        return -1;
    }

    if (get_uboot_env("boot_slot", env_slot, sizeof(env_slot)) != 0)
    {
        fprintf(stderr, "Failed to read boot_slot. Refuse upgrade to avoid writing the running slot.\n");
        return -1;
    }

    if (env_slot[0] == 'b' || env_slot[0] == 'B' || strcmp(env_slot, "1") == 0)
    {
        current_is_b = 1;
    }
    else if (env_slot[0] == 'a' || env_slot[0] == 'A' || strcmp(env_slot, "0") == 0)
    {
        current_is_b = 0;
    }
    else
    {
        fprintf(stderr, "Unknown boot_slot=%s. Refuse upgrade.\n", env_slot);
        return -1;
    }

    if (current_is_b)
    {
        current->name = "b";
        current->image_dev = IMAGE_B_DEV;
        current->rootfs_dev = ROOTFS_B_DEV;
        target->name = "a";
        target->image_dev = IMAGE_A_DEV;
        target->rootfs_dev = ROOTFS_A_DEV;
    }
    else
    {
        current->name = "a";
        current->image_dev = IMAGE_A_DEV;
        current->rootfs_dev = ROOTFS_A_DEV;
        target->name = "b";
        target->image_dev = IMAGE_B_DEV;
        target->rootfs_dev = ROOTFS_B_DEV;
    }

    snprintf(current_name, current_name_len, "%s", current->name);
    snprintf(target_name, target_name_len, "%s", target->name);
    return 0;
}

static STATUS finalize_upgrade(const char *current_slot, const char *target_slot)
{
    if (current_slot == NULL || target_slot == NULL)
    {
        return ERROR;
    }

    if (set_uboot_env("boot_slot_prev", current_slot) != 0)
    {
        fprintf(stderr, "Failed to set boot_slot_prev\n");
        return ERROR;
    }

    if (set_uboot_env("boot_attempts", "3") != 0)
    {
        fprintf(stderr, "Failed to set boot_attempts\n");
        return ERROR;
    }

    /* 最后切换 boot_slot，保证试运行标志先于新槽生效 */
    if (set_uboot_env("boot_slot", target_slot) != 0)
    {
        fprintf(stderr, "Failed to set boot_slot\n");
        return ERROR;
    }

    sync();
    return OK;
}
