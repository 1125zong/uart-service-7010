/*
 * =================================================================
 * firmware_server_task_v2.c
 * =================================================================
 */

#include <stdio.h>
#include <stdlib.h>      /* 用于 malloc, free */
#include <string.h>      /* 用于 memcpy, memset */
#include <unistd.h>      
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>        /* sockaddr_in, IPPROTO_TCP 等 */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <mtd/mtd-abi.h>

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
 * 固件包最大允许大小 (例如 20MB)
 * (防止恶意客户端耗尽 RAM)
 */
#define MAX_PACKAGE_SIZE (20 * 1024 * 1024)

/* 任务 ID */
static int serverTaskId = 0;
static pthread_t server_thread_id = 0;  // POSIX 线程 ID

const char *mtd_dev_bit_b = "/dev/mtd5";  //bitstream_b
const char *mtd_dev_image_b = "/dev/mtd7";  //image_b
#define TEMP_FILE "/tmp/image.ub"  // 临时文件（写入后自动删除）

/* --- 3. 函数原型 --- */
void *update_server_main(void *arg);
STATUS handle_client(int client_sock);
STATUS process_firmware_package(int client_sock, char *buffer, long size);


static int flashcp_write(const char *src_file, const char *mtd_device) ;
static int write_mem_to_temp(const void *buf, size_t buf_len, const char *temp_path);
static int erase_mtd_partition(const char *mtd_device);

/* 【修正】: 函数原型现在需要 header 指针和三个数据指针 */
STATUS write_firmware_to_flash(
    const fw_package_header_t *header,
    const char *bit_data,
    const char *kernel_data,
    const char *rootfs_data
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
    if (file_size == 0 || file_size > MAX_PACKAGE_SIZE)
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
    if (write_firmware_to_flash(header, bit_data, kernel_data, rootfs_data) != OK)
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
 * FLASH 写入逻辑 (V-Final 方案 - 安全升级版)
 * =================================================================
 */
STATUS write_firmware_to_flash(
    const fw_package_header_t *header,
    const char *bit_data,
    const char *kernel_data,
    const char *rootfs_data)
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

        // 1. 将内存数据写入临时文件
        if (write_mem_to_temp(bit_data, header->bit_length, TEMP_FILE) != 0)
        {
            LOG_ERROR("write_flash: [SAFE-UPGRADE] Write bit temp file failed! bootpart=0 (safe partition).\n");
            return -1;
        }
   
        LOG_INFO("write_flash: writing Bitstream to BitpartitionB...\n");
        if (flashcp_write(TEMP_FILE, mtd_dev_bit_b) != 0)
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

        // 将内存数据写入临时文件
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

    //更新rootfs，如果需要
    if(header->update_flags & UPDATE_FLAG_ROOTFS)
    {
        LOG_INFO("write_falsh: writing %u bytes ROOTFS...\n", header->rootfs_length);

        if (write_mem_to_temp(rootfs_data, header->rootfs_length, TEMP_FILE) != 0)
        {
            LOG_ERROR("write_flash: [SAFE-UPGRADE] Write rootfs temp file failed! bootpart=0 (safe partition).\n");
            return -1;
        }    
        LOG_INFO("write_flash: writing rootfs to partitionB...\n");
        if (flashcp_write(TEMP_FILE, mtd_dev_image_b) != 0)
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
  
    /* --- 步骤 4: 最后设置 bootpart=1 (切换到新分区) --- */
    /* 
     * 【安全升级机制 - 容错保护】
     * 
     * 注意：由于 Nor Flash 物理特性（只能 1→0，不能 0→1），
     * 设置 bootpart=1 之前必须先擦除 mtd2，这会导致所有环境变量丢失！
     * 
     * 因此步骤 4 的目的是：
     * 1. 擦除 mtd2（为了后续能设置 bootpart=1）
     * 2. 重新设置所有环境变量
     * 3. 最后切换到新分区
     * 
     * 【重要】即使升级完全失败（返回 -1），系统仍然可以从 mtd6 启动，因为：
     * - STEP0 已经设置了 bootpart=0（虽然被擦除了，但本次升级过程中断电不会回到那个状态）
     * - STEP4.2 会重新设置 bootpart=0（保证安全）
     * - 只有 STEP4.5 成功才会切换到 bootpart=1
     */
    LOG_INFO("write_flash: [STEP4] [SAFE-UPGRADE] Finalizing firmware upgrade...\n");
    
    /* 4.1 擦除 mtd2 分区 */
    if (erase_mtd_partition("/dev/mtd2") != 0)
    {
        LOG_ERROR("write_flash: [STEP4] [ERROR] Failed to erase mtd2! Upgrade failed, bootpart stays at safe value.\n");
        return -1;
    }
    LOG_INFO("write_flash: [STEP4a] mtd2 erased.\n");
    
    
    /* 4.3 设置新分区环境变量 (目标分区 mtd7) */
    LOG_INFO("write_flash: [STEP4d] Setting environment variables for new partition (mtd7)...\n");
    /* 设置 image_b_size (新分区) */
    if (header->update_flags & UPDATE_FLAG_KERNEL)
    {
        sprintf(size_str, "%d", header->kernel_length);
        set_uboot_env("image_b_size", size_str);
    }
    
    /* 设置 bitstream_b_size 和 fpgapart=1 (新分区) */
    if (header->update_flags & UPDATE_FLAG_BITSTREAM)
    {
        sprintf(size_str, "%u", header->bit_length);
        set_uboot_env("bitstream_b_size", size_str);
        set_uboot_env("fpgapart", "1");
    }
    
    /* 4.4 【最后步骤】设置 bootpart=1 切换到新分区 */
    /* 只有所有环境变量都设置成功后才会执行这一步 */
    LOG_INFO("write_flash: [STEP4e] All variables set successfully. Switching to new partition (bootpart=1)...\n");
    if (set_uboot_env("bootpart", "1") != 0)
    {
        LOG_ERROR("write_flash: [STEP4e] [ERROR] Failed to set bootpart=1! System will boot from safe partition (mtd6).\n");
        /* 注意：此时 bootpart=0，系统从安全分区启动，这是预期行为 */
        return -1;
    }
    else
    {
        LOG_INFO("write_flash: [STEP4] [SUCCESS] bootpart=1 set successfully!\n");
        LOG_INFO("write_flash: [STEP4] System will boot from new partition (mtd7) on next reboot.\n");
    }

    LOG_INFO("write_flash: [COMPLETE] All firmware upgrade completed successfully!\n");
    return OK;
}


/**
 * @brief 将内存数据写入临时文件
 * @param buf: 内存数据缓冲区
 * @param buf_len: 数据长度
 * @param temp_path: 临时文件路径
 * @return 成功返回 0，失败返回 -1
 */
static int write_mem_to_temp(const void *buf, size_t buf_len, const char *temp_path)
{
    int fd = open(temp_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "Failed to create temp file: %s, err=%s\n", temp_path, strerror(errno));
        return -1;
    }

    // 写入内存数据到临时文件
    size_t ret = write(fd, buf, buf_len);
    if (ret != buf_len)
    {
        fprintf(stderr, "Failed to write temp file: Expected %lu B, Actual %zd B\n",
                (unsigned long)buf_len, ret);
        close(fd);
        unlink(temp_path);  // 清理临时文件
        return -1;
    }

    close(fd);
    printf("Memory data written to temp file: %s (size: %lu B)\n", temp_path, (unsigned long)buf_len);
    return 0;
}

/**
 * @brief 封装 flashcp 命令，向 MTD 设备写入文件
 * @param src_file 待写入的源文件路径（如 "./test.bin"）
 * @param mtd_device MTD 设备路径（如 "/dev/mtd3"）
 * @return 成功返回 0，失败返回 -1
 */
static int flashcp_write(const char *src_file, const char *mtd_device)
{
    char cmd[256];
    int ret;
    if (src_file == NULL || mtd_device == NULL) {
        fprintf(stderr, "Parameters cannot be NULL\n");
        return -1;
    }

    snprintf(cmd, sizeof(cmd), "flashcp -v %s %s", src_file, mtd_device);

    // 执行命令并检查返回值
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
 * @brief 擦除 MTD 分区
 * @param mtd_device MTD 设备路径（如 "/dev/mtd2"）
 * @return 成功返回 0，失败返回 -1
 * 
 * 注意：NAND Flash 必须先擦除才能写入新数据
 * 擦除后所有字节会变成 0xFF
 */
static int erase_mtd_partition(const char *mtd_device)
{
    if (mtd_device == NULL) {
        fprintf(stderr, "erase_mtd_partition: mtd_device is NULL\n");
        return -1;
    }

    int fd = open(mtd_device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "erase_mtd_partition: Failed to open %s: %s\n", 
                mtd_device, strerror(errno));
        return -1;
    }

    /* 获取 MTD 分区信息 */
    struct mtd_info_user mtd_info;
    if (ioctl(fd, MEMGETINFO, &mtd_info) < 0) {
        fprintf(stderr, "erase_mtd_partition: MEMGETINFO ioctl failed: %s\n", 
                strerror(errno));
        close(fd);
        return -1;
    }

    /* 设置擦除参数 */
    struct erase_info_user erase;
    erase.start = 0;
    erase.length = mtd_info.size;

    printf("erase_mtd_partition: Erasing %s (%u bytes)...\n", 
           mtd_device, mtd_info.size);

    /* 执行擦除 */
    if (ioctl(fd, MEMERASE, &erase) < 0) {
        fprintf(stderr, "erase_mtd_partition: MEMERASE ioctl failed: %s\n", 
                strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    printf("erase_mtd_partition: Erase completed successfully\n");
    return 0;
}