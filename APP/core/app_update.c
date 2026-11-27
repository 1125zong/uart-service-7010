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
#include <dirent.h>      /* 用于扫描 apps 目录，清理旧的带版本备份 */
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

#include "app_version_info.h"


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

/*
 * eMMC A/B 分区。按当前板卡分区表填写，可用编译参数覆盖。
 *
 * 当前布局：
 *   p1 = A 区，rootfs 与内核镜像 /boot/image_emmc.u 在同一个分区
 *   p2 = B 区，rootfs 与内核镜像 /boot/image_emmc.u 在同一个分区
 *   p3 = 数据分区，开机挂载到 /mnt，保存 app 与配置文件
 *   p4 = 默认/恢复 rootfs，U-Boot 环境变量回落到默认值时从这里启动，
 *        从该分区发起升级时固定写回 A 区（p1）
 *
 * 注意：image_dev 与 rootfs_dev 现在指向同一个分区，
 *       升级时必须先写 rootfs，再往该分区的 /boot 目录写内核，
 *       否则内核会被 rootfs 整体覆盖。
 */
#ifndef IMAGE_A_DEV
#define IMAGE_A_DEV     "/dev/mmcblk0p1"
#endif
#ifndef ROOTFS_A_DEV
#define ROOTFS_A_DEV    "/dev/mmcblk0p1"
#endif
#ifndef IMAGE_B_DEV
#define IMAGE_B_DEV     "/dev/mmcblk0p2"
#endif
#ifndef ROOTFS_B_DEV
#define ROOTFS_B_DEV    "/dev/mmcblk0p2"
#endif

/*
 * p4 = 默认（恢复）rootfs。U-Boot 环境变量丢失、回落到默认值时
 * 从这个分区启动，保证 A/B 两槽都不可信时设备仍能起来并重新升级。
 */
#ifndef ROOTFS_DEFAULT_DEV
#define ROOTFS_DEFAULT_DEV  "/dev/mmcblk0p4"
#endif

/*
 * U-Boot 默认 env 里用于引导 ROOTFS_DEFAULT_DEV 的 boot_slot 取值，
 * 必须与 U-Boot 源码 boot_select 的判断保持一致。
 */
#ifndef FW_ENV_SLOT_RECOVERY
#define FW_ENV_SLOT_RECOVERY    "default"
#endif

/* 切换槽位时写入的重试次数：FW_ENV_BOOT_ATTEMPTS 定义在 app_update_env.h */

/*
 * 升级期间的临时挂载点。
 * 放在 /run（Buildroot 启动时挂载的 tmpfs），不占用 flash：
 *   - 不放 /mnt：/mnt 已被 p3 数据分区占用，挂到 /mnt/xxx
 *     会在数据分区里创建临时目录并留下残留；
 *   - 不放 /tmp：/tmp 同样是 tmpfs，但常被应用当临时目录使用，
 *     容易被清理逻辑误删。
 */
#ifndef IMAGE_MOUNT_POINT
#define IMAGE_MOUNT_POINT   "/run/fw_image"
#endif
#ifndef ROOTFS_MOUNT_POINT
#define ROOTFS_MOUNT_POINT  "/run/fw_rootfs"
#endif

/* 内核镜像位于 rootfs 分区的 boot 目录下 */
#define KERNEL_FILE_NAME        "boot/image_emmc.u"
#ifndef BITSTREAM_FILE_NAME
#define BITSTREAM_FILE_NAME     "boot/top_7015.bit"
#endif
/* p3 数据分区常驻挂载点（开机由 /etc/fstab 挂载到 /mnt） */
#ifndef P3_MOUNT_POINT
#define P3_MOUNT_POINT      "/mnt"
#endif

/*
 * app 不再写入 rootfs，统一写到 p3 数据分区的 apps 目录下。
 * p3 为 A/B 共享数据分区，升级 app 不影响启动槽。
 */
#ifndef APP_INSTALL_PATH_ON_P3
#define APP_INSTALL_PATH_ON_P3  P3_MOUNT_POINT "/apps/uart_service"
#endif

/*
 * 需要写入 A/B 系统分区并切换启动槽的升级类型。
 * app 位于 p3 共享数据分区，不属于该类，升级 app 时不必读取 boot_slot。
 */
#define UPDATE_FLAG_AB_WRITE    (UPDATE_FLAG_ROOTFS | UPDATE_FLAG_KERNEL | UPDATE_FLAG_BITSTREAM)

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
static int is_path_mounted(const char *path);
static int write_app_to_p3(const void *buf, size_t buf_len, const char *path);
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
    int need_slot;

    memset(&current, 0, sizeof(current));
    memset(&target, 0, sizeof(target));

    /*
     * 只有要写 A/B 系统分区时才需要读 boot_slot。
     * 单独升级 app 时跳过，避免 fw_printenv/fw_env.config 异常
     * 导致与 A/B 无关的 app 升级被误判为失败。
     */
    need_slot = (header->update_flags & UPDATE_FLAG_AB_WRITE) != 0;

    if (need_slot)
    {
        if (detect_ab_slots(&current, &target,
                            current_name, sizeof(current_name),
                            target_name, sizeof(target_name)) != OK)
        {
            printf("write_flash: [X] Failed to detect current boot slot\n");
            return ERROR;
        }

        printf("write_flash: current slot=%s, target slot=%s\n", current_name, target_name);
        printf("write_flash: target slot dev=%s (rootfs + kernel in one partition)\n",
               target.rootfs_dev);
    }
    else
    {
        printf("write_flash: no A/B system partition update, skip slot detection\n");
    }

    if (header->update_flags & UPDATE_FLAG_OS)
    {
        printf("write_flash: OS segment is reserved, skip writing\n");
    }

    /*
     * 顺序很重要：内核镜像位于 rootfs 分区的 /boot 下，
     * 因此必须先写 rootfs（raw 镜像会整分区覆盖），再写内核和 bitstream。
     */
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

    if (header->update_flags & UPDATE_FLAG_KERNEL)
    {
        printf("write_flash: writing %u bytes kernel to %s:%s\n",
               header->kernel_length, target.rootfs_dev, KERNEL_FILE_NAME);
        if (write_mem_to_mounted_file(kernel_data, header->kernel_length,
                                      target.rootfs_dev, ROOTFS_MOUNT_POINT,
                                      "ext4", KERNEL_FILE_NAME, 0644) != 0)
        {
            printf("write_flash: [X] failed to write kernel\n");
            return ERROR;
        }
    }

    if (header->update_flags & UPDATE_FLAG_BITSTREAM)
    {
        printf("write_flash: writing %u bytes bitstream to %s:%s\n",
               header->bit_length, target.rootfs_dev, BITSTREAM_FILE_NAME);
        if (write_mem_to_mounted_file(bit_data, header->bit_length,
                                      target.rootfs_dev, ROOTFS_MOUNT_POINT,
                                      "ext4", BITSTREAM_FILE_NAME, 0644) != 0)
        {
            printf("write_flash: [X] failed to write bitstream\n");
            return ERROR;
        }
    }

    if (header->update_flags & UPDATE_FLAG_APP)
    {
        printf("write_flash: writing %u bytes app to %s\n",
               header->app_length, APP_INSTALL_PATH_ON_P3);
        if (write_app_to_p3(app_data, header->app_length,
                            APP_INSTALL_PATH_ON_P3) != 0)
        {
            printf("write_flash: [X] failed to write app to p3\n");
            return ERROR;
        }

        /* 校验包内声明的版本与二进制自带版本是否一致（统一版本号） */
        {
            char pkg_ver[FW_VERSION_STRING_LEN + 1] = {0};
            char bin_ver[64] = {0};

            memcpy(pkg_ver, header->app_version, FW_VERSION_STRING_LEN);
            if (pkg_ver[0] != '\0')
            {
                if (app_version_read_binary(APP_INSTALL_PATH_ON_P3,
                                            bin_ver, sizeof(bin_ver)) == 0 &&
                    strcmp(pkg_ver, bin_ver) != 0)
                {
                    printf("write_flash: [WARN] package app_version=%s, "
                           "binary marker=%s, versions not unified\n",
                           pkg_ver, bin_ver);
                }
                else
                {
                    printf("write_flash: app version unified: %s\n",
                           (bin_ver[0] != '\0') ? bin_ver : pkg_ver);
                }
            }
        }

        printf("write_flash: app written, restart to take effect\n");
    }

    /*
     * 只有写入了 A/B 系统分区才需要切换启动槽。
     * app 位于 p3 共享数据分区，单独升级 app 不切槽。
     */
    if (need_slot)
    {
        /*
         * 从恢复分区(p4)升级时没有“上一个运行槽”，不写 boot_slot_prev，
         * 保留默认 env 里的回退目标（p4），新槽启动失败还能回到恢复系统。
         */
        const char *prev_slot = (strcmp(current_name, "recovery") == 0)
                                ? NULL : current_name;

        if (finalize_upgrade(prev_slot, target_name) != OK)
        {
            printf("write_flash: [X] failed to update U-Boot environment\n");
            return ERROR;
        }
        printf("write_flash: [OK] slot %s updated, next boot slot=%s\n",
               target_name, target_name);
    }
    else
    {
        printf("write_flash: [OK] data partition only update, boot slot unchanged\n");
    }

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

/*
 * 判断 path 是否为当前真正挂载的挂载点。
 * 用于确认 p3 数据分区已挂到 /mnt，避免 p3 挂载失败时
 * app 被写进 rootfs 里的 /mnt 目录。
 */
static int is_path_mounted(const char *path)
{
    FILE *fp;
    char line[512] = {0};
    char target[512] = {0};

    if (path == NULL || path[0] == '\0')
    {
        return 0;
    }

    fp = fopen("/proc/mounts", "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Failed to open /proc/mounts: %s\n", strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        target[0] = '\0';
        if (sscanf(line, "%*s %511s", target) != 1)
        {
            continue;
        }

        if (strcmp(target, path) == 0)
        {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

/*
 * 删除同目录下以 "<name><mid>" 开头的旧目录项，例如：
 *   mid = ".bak"  ->  uart_service.bak、uart_service.bak.V1.0.0.0
 *   mid = ".V"    ->  uart_service.V1.0.0.1
 * 每次升级只保留本次新建的那一个，避免目录里越积越多。
 * 不会误删 "<name>" 本身，也不会误删 "<name>.new"。
 */
static void remove_stale_links(const char *path, const char *mid)
{
    char dir[512] = {0};
    char name_prefix[600] = {0};
    const char *base;
    const char *slash;
    size_t dir_len;
    size_t prefix_len;
    DIR *d;
    struct dirent *de;
    int n;

    if (path == NULL || mid == NULL || mid[0] == '\0')
    {
        return;
    }

    slash = strrchr(path, '/');
    base = (slash != NULL) ? (slash + 1) : path;
    dir_len = (size_t)(base - path);

    if (dir_len == 0)
    {
        snprintf(dir, sizeof(dir), ".");
    }
    else if (dir_len >= sizeof(dir))
    {
        return;
    }
    else
    {
        memcpy(dir, path, dir_len);
        dir[dir_len] = '\0';
    }

    n = snprintf(name_prefix, sizeof(name_prefix), "%s%s", base, mid);
    if (n < 0 || (size_t)n >= sizeof(name_prefix))
    {
        return;
    }
    prefix_len = strlen(name_prefix);

    d = opendir(dir);
    if (d == NULL)
    {
        return;
    }

    while ((de = readdir(d)) != NULL)
    {
        char full[1024] = {0};

        if (strncmp(de->d_name, name_prefix, prefix_len) != 0)
        {
            continue;
        }

        n = snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
        if (n > 0 && (size_t)n < sizeof(full))
        {
            unlink(full);
        }
    }

    closedir(d);
}

/*
 * 把 app 写到 p3 数据分区。
 *
 * 先写 <path>.new，再 rename 覆盖原文件：
 * 直接覆盖正在运行的可执行文件会返回 ETXTBSY；
 * rename 是原子操作，运行中的进程继续持有旧 inode，
 * 新程序在下次启动时生效。
 *
 * 升级完成后目录里固定只有两个目录项（同一 inode，不占额外空间）：
 *   /mnt/apps/uart_service            新版本，启动脚本用
 *   /mnt/apps/uart_service.V1.0.0.1   被替换下来的旧版本
 * 旧程序没有版本标记时（历史遗留二进制）命名为 uart_service.unknown。
 * 历史遗留的 .bak / .bak.<ver> 会在升级时清掉。
 *
 * 回滚到上一版本：
 *   cp /mnt/apps/uart_service.V1.0.0.1 /mnt/apps/uart_service.new
 *   mv /mnt/apps/uart_service.new /mnt/apps/uart_service
 */
static int write_app_to_p3(const void *buf, size_t buf_len, const char *path)
{
    char tmp_path[512] = {0};
    char old_ver[64] = {0};
    char new_ver[64] = {0};
    char prev_link[600] = {0};
    int old_exists = 0;
    int old_has_ver = 0;
    int n;

    if (buf == NULL || path == NULL)
    {
        return -1;
    }

    if (!is_path_mounted(P3_MOUNT_POINT))
    {
        fprintf(stderr, "p3 data partition is not mounted at %s, "
                        "refuse to write app\n", P3_MOUNT_POINT);
        return -1;
    }

    {
        int rc = app_version_read_binary(path, old_ver, sizeof(old_ver));

        if (rc == APP_VERSION_OK)
        {
            old_exists = 1;
            old_has_ver = 1;
            printf("write_app_to_p3: current on-disk app version: %s\n", old_ver);
        }
        else if (rc == APP_VERSION_ERR_NO_FILE)
        {
            printf("write_app_to_p3: no app installed on p3 yet (first install)\n");
        }
        else
        {
            old_exists = 1;
            printf("write_app_to_p3: current on-disk app version: unknown "
                   "(old binary has no version marker)\n");
        }
    }

    n = snprintf(tmp_path, sizeof(tmp_path), "%s.new", path);
    if (n < 0 || n >= (int)sizeof(tmp_path))
    {
        fprintf(stderr, "app temp path too long\n");
        return -1;
    }

    unlink(tmp_path);

    if (write_mem_to_file(buf, buf_len, tmp_path, 0755) != 0)
    {
        unlink(tmp_path);
        return -1;
    }

    /*
     * 把即将被替换掉的旧文件挂成 <path>.<旧版本>（硬链接，同一 inode）。
     * 必须在 rename 之前建立：此刻 path 仍指向旧 inode，rename 之后
     * 旧 inode 就只剩这一个别名了。
     * 同时清掉历史遗留的 .bak* 和上一轮的 .V*，目录只保留两项。
     */
    remove_stale_links(path, ".bak");
    remove_stale_links(path, ".V");

    if (old_exists)
    {
        n = snprintf(prev_link, sizeof(prev_link), "%s.%s", path,
                     old_has_ver ? old_ver : "unknown");
        if (n > 0 && (size_t)n < sizeof(prev_link))
        {
            unlink(prev_link);
            if (link(path, prev_link) != 0)
            {
                fprintf(stderr, "Warning: failed to create %s: %s\n",
                        prev_link, strerror(errno));
                prev_link[0] = '\0';
            }
        }
        else
        {
            prev_link[0] = '\0';
        }
    }

    if (rename(tmp_path, path) != 0)
    {
        fprintf(stderr, "Failed to rename %s -> %s: %s\n",
                tmp_path, path, strerror(errno));
        unlink(tmp_path);
        if (prev_link[0] != '\0')
        {
            /* 旧文件还留在 path 上，撤掉刚才多建的别名 */
            unlink(prev_link);
        }
        return -1;
    }

    sync();

    if (app_version_read_binary(path, new_ver, sizeof(new_ver)) == APP_VERSION_OK)
    {
        if (prev_link[0] != '\0')
        {
            printf("write_app_to_p3: app updated to %s, previously installed "
                   "version kept at %s\n", new_ver, prev_link);
        }
        else
        {
            printf("write_app_to_p3: app updated to %s (no previous version)\n",
                   new_ver);
        }
    }
    else
    {
        printf("write_app_to_p3: app updated, but version marker not found "
               "in new binary\n");
    }

    app_version_dump_all(path);

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

/*
 * 从 /proc/cmdline 的 root= 参数推断运行槽，作为 stat 判定的兜底手段。
 * 返回 1 = B，0 = A，2 = 默认/恢复分区(p4)，-1 = 无法判定。
 */
static int running_slot_from_cmdline(void)
{
    FILE *fp;
    char line[512] = {0};
    int ret = -1;

    fp = fopen("/proc/cmdline", "r");
    if (fp == NULL)
    {
        return -1;
    }

    if (fgets(line, sizeof(line), fp) != NULL)
    {
        if (strstr(line, "root=" ROOTFS_B_DEV) != NULL)
        {
            ret = 1;
        }
        else if (strstr(line, "root=" ROOTFS_A_DEV) != NULL)
        {
            ret = 0;
        }
        else if (strstr(line, "root=" ROOTFS_DEFAULT_DEV) != NULL)
        {
            ret = 2;
        }
    }

    fclose(fp);
    return ret;
}

/*
 * 判断当前真正的运行槽：
 *   0 = A(ROOTFS_A_DEV)，1 = B(ROOTFS_B_DEV)，
 *   2 = 默认/恢复分区(ROOTFS_DEFAULT_DEV)，-1 = 无法判定。
 *
 * 只能用“正在运行的根文件系统”判断。env 里的 boot_slot 在掉电后可能
 * 回落到默认值，若据此决定升级目标，会把正在运行的 rootfs 整块覆盖。
 */
static int running_slot_id(void)
{
    struct stat root_st;
    struct stat a_st;
    struct stat b_st;
    struct stat d_st;

    if (stat("/", &root_st) == 0 &&
        stat(ROOTFS_A_DEV, &a_st) == 0 &&
        stat(ROOTFS_B_DEV, &b_st) == 0)
    {
        if (root_st.st_dev == b_st.st_rdev)
        {
            return 1;
        }

        if (root_st.st_dev == a_st.st_rdev)
        {
            return 0;
        }

        /* 从默认/恢复分区启动：env 丢过、正在 p4 上重新升级 */
        if (stat(ROOTFS_DEFAULT_DEV, &d_st) == 0 &&
            root_st.st_dev == d_st.st_rdev)
        {
            return 2;
        }

        fprintf(stderr, "running_slot_id: root dev %lu matches neither %s, %s nor %s, "
                        "falling back to /proc/cmdline\n",
                (unsigned long)root_st.st_dev, ROOTFS_A_DEV, ROOTFS_B_DEV,
                ROOTFS_DEFAULT_DEV);
    }
    else
    {
        fprintf(stderr, "running_slot_id: stat failed: %s, "
                        "falling back to /proc/cmdline\n", strerror(errno));
    }

    return running_slot_from_cmdline();
}

static int detect_ab_slots(FirmwareSlot *current, FirmwareSlot *target,
                           char *current_name, size_t current_name_len,
                           char *target_name, size_t target_name_len)
{
    char env_slot[32] = {0};
    int current_is_b = 0;
    int running_id;
    int running_is_b = 0;
    int need_fix_env = 0;

    if (current == NULL || target == NULL ||
        current_name == NULL || target_name == NULL ||
        current_name_len == 0 || target_name_len == 0)
    {
        return -1;
    }

    /*
     * 以“正在运行的根分区”为准，env 只作参考。
     * env 掉电损坏后 boot_slot 会回落到默认值，若据此决定目标槽，
     * 会把正在运行的 rootfs 整块覆盖，设备当场报废。
     */
    running_id = running_slot_id();
    if (running_id < 0)
    {
        fprintf(stderr, "detect_ab_slots: cannot determine running slot "
                        "(root dev matches none of %s, %s, %s), refuse upgrade\n",
                ROOTFS_A_DEV, ROOTFS_B_DEV, ROOTFS_DEFAULT_DEV);
        return -1;
    }

    /*
     * 从默认/恢复分区(p4)启动：说明 env 丢过或者两个槽都不再可信。
     * 此时不覆盖任何运行中的 A/B 分区也无从判断，统一回写到 A 槽，
     * 并把 boot_slot 复位成 a；boot_slot_prev 由调用方置空，
     * 失败时仍回到 U-Boot 默认 env 引导的恢复分区。
     */
    if (running_id == 2)
    {
        if (get_uboot_env("boot_slot", env_slot, sizeof(env_slot)) != 0)
        {
            printf("detect_ab_slots: [WARN] boot_slot unreadable, "
                   "restore it to %s\n", FW_ENV_SLOT_RECOVERY);

            if (set_uboot_env("boot_slot", FW_ENV_SLOT_RECOVERY) != 0)
            {
                fprintf(stderr, "detect_ab_slots: failed to restore boot_slot, "
                                "refuse upgrade\n");
                return -1;
            }
        }

        current->name = "recovery";
        current->image_dev = ROOTFS_DEFAULT_DEV;
        current->rootfs_dev = ROOTFS_DEFAULT_DEV;
        target->name = "a";
        target->image_dev = IMAGE_A_DEV;
        target->rootfs_dev = ROOTFS_A_DEV;

        printf("detect_ab_slots: [WARN] running on recovery rootfs %s, "
               "target slot forced to %s\n", ROOTFS_DEFAULT_DEV, target->name);

        snprintf(current_name, current_name_len, "%s", current->name);
        snprintf(target_name, target_name_len, "%s", target->name);
        return 0;
    }

    running_is_b = (running_id == 1);
    current_is_b = running_is_b;

    if (get_uboot_env("boot_slot", env_slot, sizeof(env_slot)) != 0)
    {
        printf("detect_ab_slots: [WARN] boot_slot unreadable, "
               "restore it to %s from running rootfs\n",
               running_is_b ? "b" : "a");
        need_fix_env = 1;
    }
    else
    {
        int env_is_b = -1;

        if (env_slot[0] == 'b' || env_slot[0] == 'B' || strcmp(env_slot, "1") == 0)
        {
            env_is_b = 1;
        }
        else if (env_slot[0] == 'a' || env_slot[0] == 'A' || strcmp(env_slot, "0") == 0)
        {
            env_is_b = 0;
        }

        if (env_is_b < 0)
        {
            printf("detect_ab_slots: [WARN] unknown boot_slot=%s, "
                   "correcting to %s\n", env_slot, running_is_b ? "b" : "a");
            need_fix_env = 1;
        }
        else if (env_is_b != running_is_b)
        {
            printf("detect_ab_slots: [WARN] env boot_slot=%s but running on %s, "
                   "correcting env before upgrade\n",
                   env_slot, running_is_b ? "b" : "a");
            need_fix_env = 1;
        }
    }

    if (need_fix_env)
    {
        /* 纠正失败就拒绝升级：宁可这次不升，也不能写坏正在运行的槽 */
        if (set_uboot_env("boot_slot", running_is_b ? "b" : "a") != 0)
        {
            fprintf(stderr, "detect_ab_slots: failed to correct boot_slot, "
                            "refuse upgrade\n");
            return -1;
        }
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

    printf("detect_ab_slots: running rootfs = %s, current slot = %s, "
           "target slot = %s\n",
           current->rootfs_dev, current->name, target->name);

    snprintf(current_name, current_name_len, "%s", current->name);
    snprintf(target_name, target_name_len, "%s", target->name);
    return 0;
}

/* finalize_upgrade() 批量写 env 时使用的临时文件与备份路径 */
#define FW_ENV_ALL_TMP_FILE     "/tmp/fw_env_all.txt"
#define FW_ENV_NEW_TMP_FILE     "/tmp/fw_env_new.txt"
#define FW_ENV_BACKUP_PATH      P3_MOUNT_POINT "/env.backup"

/*
 * 把整份 U-Boot 环境变量导出到 path（每行 key=value）。
 * 走 fw_printenv，不自己解析 flash 布局，避免与 U-Boot 的 env 格式脱节。
 */
static int env_export_all(const char *path)
{
    char cmd[512] = {0};

    if (path == NULL || path[0] == '\0')
    {
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "fw_printenv > %s 2>/dev/null", path);
    if (system(cmd) != 0)
    {
        fprintf(stderr, "env_export_all: fw_printenv failed (%s)\n", path);
        return -1;
    }

    return 0;
}

/*
 * 以 src 为基础生成 dst：删掉 keys[] 里已有的行，再追加 keys=vals。
 * 生成的文件交给 fw_setenv -s 一次写入，把多次擦写 env 扇区压成一次。
 */
static int env_build_merged_file(const char *src, const char *dst,
                                 const char *const *keys,
                                 const char *const *vals, int count)
{
    FILE *in;
    FILE *out;
    char line[2048] = {0};
    int i;

    if (src == NULL || dst == NULL || keys == NULL || vals == NULL || count <= 0)
    {
        return -1;
    }

    in = fopen(src, "r");
    if (in == NULL)
    {
        fprintf(stderr, "env_build_merged_file: open %s failed: %s\n",
                src, strerror(errno));
        return -1;
    }

    out = fopen(dst, "w");
    if (out == NULL)
    {
        fprintf(stderr, "env_build_merged_file: open %s failed: %s\n",
                dst, strerror(errno));
        fclose(in);
        return -1;
    }

    while (fgets(line, sizeof(line), in) != NULL)
    {
        int skip = 0;

        for (i = 0; i < count; i++)
        {
            size_t klen = strlen(keys[i]);

            if (strncmp(line, keys[i], klen) == 0 && line[klen] == '=')
            {
                skip = 1;
                break;
            }
        }

        if (!skip)
        {
            fputs(line, out);
        }
    }

    for (i = 0; i < count; i++)
    {
        fprintf(out, "%s=%s\n", keys[i], vals[i]);
    }

    fclose(in);

    if (fclose(out) != 0)
    {
        fprintf(stderr, "env_build_merged_file: write %s failed: %s\n",
                dst, strerror(errno));
        return -1;
    }

    return 0;
}

/* 判断导出的 env 文本里是否含有某个 key= 行，用于识别“导出的是默认值” */
static int env_file_contains(const char *path, const char *key)
{
    FILE *fp;
    char line[2048] = {0};
    size_t klen;
    int found = 0;

    if (path == NULL || key == NULL)
    {
        return 0;
    }

    fp = fopen(path, "r");
    if (fp == NULL)
    {
        return 0;
    }

    klen = strlen(key);

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        if (strncmp(line, key, klen) == 0)
        {
            found = 1;
            break;
        }
    }

    fclose(fp);
    return found;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in;
    FILE *out;
    char buf[1024];
    size_t n;
    int ret = 0;

    if (src == NULL || dst == NULL)
    {
        return -1;
    }

    in = fopen(src, "rb");
    if (in == NULL)
    {
        return -1;
    }

    out = fopen(dst, "wb");
    if (out == NULL)
    {
        fclose(in);
        return -1;
    }

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    {
        if (fwrite(buf, 1, n, out) != n)
        {
            ret = -1;
            break;
        }
    }

    fclose(in);
    if (fclose(out) != 0)
    {
        ret = -1;
    }

    return ret;
}

/*
 * 写 U-Boot 启动槽信息：boot_slot_prev / boot_attempts / boot_slot。
 *
 * 与旧实现（连续 3 次 fw_setenv）不同，这里把三个变量合并成一次写入：
 *   - 只擦写一次 env 扇区，掉电窗口从 3 个降到 1 个；
 *   - 三个变量同时生效，不存在“只切了一半”的中间态。
 *
 * 写入前会把完整 env 备份到 p3（/mnt/env.backup），写坏后可直接
 *   fw_setenv -s /mnt/env.backup
 * 恢复。写完回读校验，三个值全部正确才算成功。
 */
static STATUS finalize_upgrade(const char *current_slot, const char *target_slot)
{
    const char *env_keys[3] = {0};
    const char *env_vals[3] = {0};
    char got[64] = {0};
    int count = 0;
    int i;

    if (target_slot == NULL)
    {
        return ERROR;
    }

    /*
     * current_slot == NULL 表示“从恢复分区升级”：不写 boot_slot_prev，
     * 保留 U-Boot 默认 env 里的回退目标（恢复分区），
     * 这样新槽启动失败时还能回到 p4。
     */
    if (current_slot != NULL)
    {
        env_keys[count] = "boot_slot_prev";
        env_vals[count] = current_slot;
        count++;
    }

    env_keys[count] = "boot_attempts";
    env_vals[count] = FW_ENV_BOOT_ATTEMPTS;
    count++;

    env_keys[count] = "boot_slot";
    env_vals[count] = target_slot;
    count++;

    /* 1) 导出当前 env；内容看着完整才备份，避免坏 env 覆盖上一份好备份 */
    if (env_export_all(FW_ENV_ALL_TMP_FILE) != 0)
    {
        fprintf(stderr, "finalize_upgrade: failed to dump current env\n");
        return ERROR;
    }

    if (env_file_contains(FW_ENV_ALL_TMP_FILE, "bootcmd="))
    {
        if (is_path_mounted(P3_MOUNT_POINT) &&
            copy_file(FW_ENV_ALL_TMP_FILE, FW_ENV_BACKUP_PATH) == 0)
        {
            printf("finalize_upgrade: env backed up to %s\n", FW_ENV_BACKUP_PATH);
        }
        else
        {
            printf("finalize_upgrade: [WARN] failed to back up env to %s\n",
                   FW_ENV_BACKUP_PATH);
        }
    }
    else
    {
        printf("finalize_upgrade: [WARN] dumped env looks incomplete (no bootcmd), "
               "keep previous backup at %s\n", FW_ENV_BACKUP_PATH);
    }

    /* 2) 替换三个变量 → 一次写回 */
    if (env_build_merged_file(FW_ENV_ALL_TMP_FILE, FW_ENV_NEW_TMP_FILE,
                              env_keys, env_vals, count) != 0)
    {
        remove(FW_ENV_ALL_TMP_FILE);
        return ERROR;
    }

    {
        char cmd[512] = {0};

        snprintf(cmd, sizeof(cmd), "fw_setenv -s %s", FW_ENV_NEW_TMP_FILE);
        printf("finalize_upgrade: %s\n", cmd);

        if (system(cmd) != 0)
        {
            /* 老版本 fw_setenv 可能不支持 -s，退回逐个写入 */
            printf("finalize_upgrade: [WARN] fw_setenv -s failed, "
                   "fall back to sequential setenv\n");

            for (i = 0; i < count; i++)
            {
                if (set_uboot_env(env_keys[i], env_vals[i]) != 0)
                {
                    fprintf(stderr, "finalize_upgrade: failed to set %s\n",
                            env_keys[i]);
                    remove(FW_ENV_ALL_TMP_FILE);
                    remove(FW_ENV_NEW_TMP_FILE);
                    return ERROR;
                }
            }
        }
    }

    remove(FW_ENV_ALL_TMP_FILE);
    remove(FW_ENV_NEW_TMP_FILE);

    /* 3) 回读校验，写入的每个变量都必须生效 */
    for (i = 0; i < count; i++)
    {
        memset(got, 0, sizeof(got));

        if (get_uboot_env(env_keys[i], got, sizeof(got)) != 0 ||
            strcmp(got, env_vals[i]) != 0)
        {
            fprintf(stderr, "finalize_upgrade: verify %s failed "
                            "(got '%s', want '%s')\n",
                    env_keys[i], got, env_vals[i]);
            return ERROR;
        }

        printf("finalize_upgrade: %s=%s verified\n", env_keys[i], got);
    }

    sync();
    return OK;
}
