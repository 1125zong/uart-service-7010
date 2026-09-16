#ifndef FIRMWARE_HEADER_H
#define FIRMWARE_HEADER_H

#include <stdint.h> // 使用标准整数类型

// (点 1) Magic Number
// 使用 'UPDT' 对应的 ASCII 码 (0x55 0x50 0x44 0x54) 作为魔术字。
#define FW_PACKAGE_MAGIC 0x55504454 

// Header 总大小
#define FW_PACKAGE_HEADER_SIZE 256

// 版本字符串固定长度
#define FW_VERSION_STRING_LEN 16

// 升级标志位定义
#define UPDATE_FLAG_BITSTREAM       0x01  // bit0: 更新 Bitstream
#define UPDATE_FLAG_OS              0x02  // bit1: 更新 OS
#define UPDATE_FLAG_APP             0x04  // bit2: 更新 App
#define UPDATE_FLAG_KERNEL          0x08  // bit2: 更新 内核
#define UPDATE_FLAG_ROOTFS          0x10  // bit2: 更新 文件系统

/**
 * @brief 固件包文件头结构体 (256 字节)
 * @note 这个结构体被设计为在 x86/ARM (小端序) 平台上直接从文件映射。
 * 使用 __attribute__((packed)) 确保没有编译器填充。
 * @note 支持独立升级 Bitstream、OS、App 三个组件
 */
typedef struct __attribute__((packed)) {

    // ---------------------------------
    // 偏移量 0: Magic 和 Header CRC
    // ---------------------------------
    uint32_t magic_number;  // 必须等于 FW_PACKAGE_MAGIC
    uint32_t header_crc32;  // 偏移量 8 到 255 的 CRC32

    // ---------------------------------
    // 偏移量 8: 升级标志位
    // ---------------------------------
    uint8_t  update_flags;  // 升级标志位: bit0=Bitstream, bit1=OS, bit2=App
    uint8_t  reserved1[3];  // 保留字段（对齐）

    // ---------------------------------
    // 偏移量 12: 版本信息
    // ---------------------------------
    char pkg_version[FW_VERSION_STRING_LEN];     // 包版本
    char bit_version[FW_VERSION_STRING_LEN];     // Bitstream 版本
    char os_version[FW_VERSION_STRING_LEN];      // OS 版本
    char app_version[FW_VERSION_STRING_LEN];     // App 版本
    char kernel_version[FW_VERSION_STRING_LEN];  // kernel 版本
    char rootfs_version[FW_VERSION_STRING_LEN];  // rootfs 版本

    // ---------------------------------
    // 偏移量 140: 时间戳
    // ---------------------------------
    uint32_t timestamp;     // Unix 时间戳

    // ---------------------------------
    // 偏移量 144: Bitstream 描述
    // ---------------------------------
    uint32_t bit_length;    // Bitstream 字节长度（0表示不更新）
    uint32_t bit_crc32;     // Bitstream CRC32

    // ---------------------------------
    // 偏移量 152: OS 描述
    // ---------------------------------
    uint32_t os_length;     // OS 字节长度（0表示不更新）
    uint32_t os_crc32;      // OS CRC32

    // ---------------------------------
    // 偏移量 160: Application 描述
    // ---------------------------------
    uint32_t app_length;    // Application 字节长度（0表示不更新）
    uint32_t app_crc32;     // Application CRC32


    uint32_t kernel_length; // kernel 字节长度（0表示不更新）
    uint32_t kernel_crc32;  // kernel CRC32

    uint32_t rootfs_length; // rootfs 字节长度（0表示不更新）
    uint32_t rootfs_crc32;  // rootfs CRC32

    // ---------------------------------
    // 偏移量 168: 保留
    // ---------------------------------
    //char reserved2[88];     // 保留字段（扩展空间）

} fw_package_header_t;



#endif // FIRMWARE_HEADER_H

