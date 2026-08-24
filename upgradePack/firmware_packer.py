#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
固件打包工具 (Firmware Packer)

支持独立打包 Bitstream、OS、App、Kernel、RootFS 五个组件，并生成符合协议的固件包。

使用方法:
    python firmware_packer.py --help

示例:
    # 打包所有组件
    python firmware_packer.py --bitstream bitstream.bin --os os.bin --app app.bin --kernel image.ub --rootfs rootfs.jffs2 -o update_full.pkg

    # 只打包 Bitstream
    python firmware_packer.py --bitstream bitstream.bin -o bitstream_only.pkg

    # 只打包 OS
    python firmware_packer.py --os os.bin -o os_only.pkg

    # 只打包 App
    python firmware_packer.py --app app.bin -o app_only.pkg

    # 只打包 Kernel (Linux 内核)
    python firmware_packer.py --kernel image.ub -o kernel_only.pkg

    # 只打包 RootFS (根文件系统)
    python firmware_packer.py --rootfs rootfs.jffs2 -o rootfs_only.pkg

    # 打包 Kernel 和 RootFS (Linux 完整固件)
    python firmware_packer.py --kernel image.ub --rootfs rootfs.jffs2 -o update.img

    # 打包 Bitstream 和 OS
    python firmware_packer.py --bitstream bitstream.bin --os os.bin -o bit_os.pkg
"""

import argparse
import struct
import zlib
import os
import sys
from datetime import datetime

# 固件包常量定义
FW_PACKAGE_MAGIC = 0x55504454  # 'UPDT' 的 ASCII 码
FW_PACKAGE_HEADER_SIZE = 256
FW_VERSION_STRING_LEN = 16

# 升级标志位
UPDATE_FLAG_BITSTREAM = 0x01
UPDATE_FLAG_OS = 0x02
UPDATE_FLAG_APP = 0x04
UPDATE_FLAG_KERNEL = 0x08
UPDATE_FLAG_ROOTFS = 0x10


def calculate_crc32(data):
    """
    计算 CRC32 校验和

    Args:
        data: 字节数据

    Returns:
        uint32_t: CRC32 校验和
    """
    return zlib.crc32(data) & 0xFFFFFFFF


def create_header(args, bit_length, os_length, app_length, kernel_length, rootfs_length):
    """
    创建固件包头部

    Args:
        args: 命令行参数
        bit_length: Bitstream 长度
        os_length: OS 长度
        app_length: App 长度
        kernel_length: Kernel 长度
        rootfs_length: RootFS 长度

    Returns:
        bytes: 256 字节的头部数据
    """
    header = bytearray(FW_PACKAGE_HEADER_SIZE)

    # 偏移量 0: Magic Number
    struct.pack_into('<I', header, 0, FW_PACKAGE_MAGIC)

    # 偏移量 4: Header CRC32 (稍后计算)
    struct.pack_into('<I', header, 4, 0)

    # 偏移量 8: 升级标志位
    update_flags = 0
    if args.bitstream:
        update_flags |= UPDATE_FLAG_BITSTREAM
    if args.os:
        update_flags |= UPDATE_FLAG_OS
    if args.app:
        update_flags |= UPDATE_FLAG_APP
    if args.kernel:
        update_flags |= UPDATE_FLAG_KERNEL
    if args.rootfs:
        update_flags |= UPDATE_FLAG_ROOTFS
    struct.pack_into('<B', header, 8, update_flags)
    struct.pack_into('<B', header, 9, 0)  # reserved1[0]
    struct.pack_into('<B', header, 10, 0)  # reserved1[1]
    struct.pack_into('<B', header, 11, 0)  # reserved1[2]

    # 偏移量 12-107: 版本信息 (每个版本 16 字节，共 5 个版本 = 80 字节)
    pkg_version = (args.pkg_version or 'v1.0.0').encode('utf-8')[:FW_VERSION_STRING_LEN]
    bit_version = (args.bit_version or 'v1.0.0').encode('utf-8')[:FW_VERSION_STRING_LEN]
    os_version = (args.os_version or 'v1.0.0').encode('utf-8')[:FW_VERSION_STRING_LEN]
    app_version = (args.app_version or 'v1.0.0').encode('utf-8')[:FW_VERSION_STRING_LEN]
    kernel_version = (args.kernel_version or 'v1.0.0').encode('utf-8')[:FW_VERSION_STRING_LEN]
    rootfs_version = (args.rootfs_version or 'v1.0.0').encode('utf-8')[:FW_VERSION_STRING_LEN]

    struct.pack_into(f'<{FW_VERSION_STRING_LEN}s', header, 12, pkg_version)
    struct.pack_into(f'<{FW_VERSION_STRING_LEN}s', header, 28, bit_version)
    struct.pack_into(f'<{FW_VERSION_STRING_LEN}s', header, 44, os_version)
    struct.pack_into(f'<{FW_VERSION_STRING_LEN}s', header, 60, app_version)
    struct.pack_into(f'<{FW_VERSION_STRING_LEN}s', header, 76, kernel_version)
    struct.pack_into(f'<{FW_VERSION_STRING_LEN}s', header, 92, rootfs_version)

    # 偏移量 108: 时间戳
    struct.pack_into('<I', header, 108, int(datetime.now().timestamp()))

    # 偏移量 112: Bitstream 描述
    struct.pack_into('<I', header, 112, bit_length)
    struct.pack_into('<I', header, 116, 0)  # bit_crc32 (稍后计算)

    # 偏移量 120: OS 描述
    struct.pack_into('<I', header, 120, os_length)
    struct.pack_into('<I', header, 124, 0)  # os_crc32 (稍后计算)

    # 偏移量 128: App 描述
    struct.pack_into('<I', header, 128, app_length)
    struct.pack_into('<I', header, 132, 0)  # app_crc32 (稍后计算)

    # 偏移量 136: Kernel 描述
    struct.pack_into('<I', header, 136, kernel_length)
    struct.pack_into('<I', header, 140, 0)  # kernel_crc32 (稍后计算)

    # 偏移量 144: RootFS 描述
    struct.pack_into('<I', header, 144, rootfs_length)
    struct.pack_into('<I', header, 148, 0)  # rootfs_crc32 (稍后计算)

    # 偏移量 152-255: 保留字段 (104 字节)
    # 已经初始化为 0

    return header


def update_crcs(header, bit_data, os_data, app_data, kernel_data, rootfs_data):
    """
    更新头部中的 CRC32 值

    Args:
        header: 头部数据 (bytearray)
        bit_data: Bitstream 数据
        os_data: OS 数据
        app_data: App 数据
        kernel_data: Kernel 数据
        rootfs_data: RootFS 数据
    """
    # 更新 Bitstream CRC
    if bit_data:
        bit_crc = calculate_crc32(bit_data)
        struct.pack_into('<I', header, 116, bit_crc)

    # 更新 OS CRC
    if os_data:
        os_crc = calculate_crc32(os_data)
        struct.pack_into('<I', header, 124, os_crc)

    # 更新 App CRC
    if app_data:
        app_crc = calculate_crc32(app_data)
        struct.pack_into('<I', header, 132, app_crc)

    # 更新 Kernel CRC
    if kernel_data:
        kernel_crc = calculate_crc32(kernel_data)
        struct.pack_into('<I', header, 140, kernel_crc)

    # 更新 RootFS CRC
    if rootfs_data:
        rootfs_crc = calculate_crc32(rootfs_data)
        struct.pack_into('<I', header, 148, rootfs_crc)

    # 更新 Header CRC (从偏移量 8 到 255)
    header_data = header[8:FW_PACKAGE_HEADER_SIZE]
    header_crc = calculate_crc32(header_data)
    struct.pack_into('<I', header, 4, header_crc)


def validate_file(filepath):
    """
    验证文件是否存在并可读

    Args:
        filepath: 文件路径

    Returns:
        bool: 文件是否有效
    """
    if not filepath:
        return False
    if not os.path.exists(filepath):
        print(f"错误: 文件不存在: {filepath}")
        return False
    if not os.path.isfile(filepath):
        print(f"错误: 不是文件: {filepath}")
        return False
    return True


def load_file(filepath):
    """
    加载文件内容

    Args:
        filepath: 文件路径

    Returns:
        bytes: 文件内容，失败返回 None
    """
    try:
        with open(filepath, 'rb') as f:
            return f.read()
    except Exception as e:
        print(f"错误: 无法读取文件 {filepath}: {e}")
        return None


def main():
    parser = argparse.ArgumentParser(
        description='固件打包工具 - 打包 Bitstream、OS、App 组件',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 打包所有组件
  %(prog)s --bitstream bitstream.bin --os os.bin --app app.bin --kernel image.ub --rootfs rootfs.jffs2 -o update_full.pkg

  # 只打包 Bitstream
  %(prog)s --bitstream bitstream.bin -o bitstream_only.pkg

  # 只打包 OS
  %(prog)s --os os.bin -o os_only.pkg

  # 只打包 App
  %(prog)s --app app.bin -o app_only.pkg

  # 只打包 Kernel (Linux 内核)
  %(prog)s --kernel image.ub -o kernel_only.pkg

  # 只打包 RootFS (根文件系统)
  %(prog)s --rootfs rootfs.jffs2 -o rootfs_only.pkg

  # 打包 Kernel 和 RootFS (Linux 完整固件)
  %(prog)s --kernel image.ub --rootfs rootfs.jffs2 -o update.img

  # 打包 Bitstream 和 OS
  %(prog)s --bitstream bitstream.bin --os os.bin -o bit_os.pkg
        """
    )

    parser.add_argument('--bitstream', '-b', type=str,
                        help='Bitstream 文件路径 (FPGA 配置文件)')
    parser.add_argument('--os', type=str,
                        help='OS 文件路径 (VxWorks 操作系统)')
    parser.add_argument('--app', '-a', type=str,
                        help='App 文件路径 (应用程序)')
    parser.add_argument('--kernel', '-k', type=str,
                        help='Kernel 文件路径 (Linux 内核镜像 image.ub)')
    parser.add_argument('--rootfs', '-r', type=str,
                        help='RootFS 文件路径 (根文件系统 rootfs.jffs2)')
    parser.add_argument('--output', '-o', type=str, required=True,
                        help='输出固件包文件路径')

    # 版本信息
    parser.add_argument('--pkg-version', type=str, default='v1.0.0',
                        help='固件包版本号 (默认: v1.0.0)')
    parser.add_argument('--bit-version', type=str, default='v1.0.0',
                        help='Bitstream 版本号 (默认: v1.0.0)')
    parser.add_argument('--os-version', type=str, default='v1.0.0',
                        help='OS 版本号 (默认: v1.0.0)')
    parser.add_argument('--app-version', type=str, default='v1.0.0',
                        help='App 版本号 (默认: v1.0.0)')
    parser.add_argument('--kernel-version', type=str, default='v1.0.0',
                        help='Kernel 版本号 (默认: v1.0.0)')
    parser.add_argument('--rootfs-version', type=str, default='v1.0.0',
                        help='RootFS 版本号 (默认: v1.0.0)')

    args = parser.parse_args()

    # 检查至少指定了一个组件
    if not args.bitstream and not args.os and not args.app and not args.kernel and not args.rootfs:
        print("错误: 必须至少指定一个组件 (--bitstream, --os, --app, --kernel, 或 --rootfs)")
        parser.print_help()
        sys.exit(1)

    # 验证文件
    bit_data = None
    os_data = None
    app_data = None
    kernel_data = None
    rootfs_data = None

    if args.bitstream:
        if validate_file(args.bitstream):
            bit_data = load_file(args.bitstream)
            if bit_data is None:
                sys.exit(1)
        else:
            sys.exit(1)

    if args.os:
        if validate_file(args.os):
            os_data = load_file(args.os)
            if os_data is None:
                sys.exit(1)
        else:
            sys.exit(1)

    if args.app:
        if validate_file(args.app):
            app_data = load_file(args.app)
            if app_data is None:
                sys.exit(1)
        else:
            sys.exit(1)

    if args.kernel:
        if validate_file(args.kernel):
            kernel_data = load_file(args.kernel)
            if kernel_data is None:
                sys.exit(1)
        else:
            sys.exit(1)

    if args.rootfs:
        if validate_file(args.rootfs):
            rootfs_data = load_file(args.rootfs)
            if rootfs_data is None:
                sys.exit(1)
        else:
            sys.exit(1)

    # 获取各组件长度
    bit_length = len(bit_data) if bit_data else 0
    os_length = len(os_data) if os_data else 0
    app_length = len(app_data) if app_data else 0
    kernel_length = len(kernel_data) if kernel_data else 0
    rootfs_length = len(rootfs_data) if rootfs_data else 0

    # 打印信息
    print("=" * 60)
    print("固件打包工具")
    print("=" * 60)
    print(f"包版本: {args.pkg_version}")
    print(f"时间戳: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("-" * 60)
    if bit_data:
        print(f"Bitstream: {args.bitstream} ({bit_length:,} / 0x{bit_length:X} 字节)")
        print(f"  版本: {args.bit_version}")
    if os_data:
        print(f"OS: {args.os} ({os_length:,} / 0x{os_length:X} 字节)")
        print(f"  版本: {args.os_version}")
    if app_data:
        print(f"App: {args.app} ({app_length:,} / 0x{app_length:X} 字节)")
        print(f"  版本: {args.app_version}")
    if kernel_data:
        print(f"Kernel: {args.kernel} ({kernel_length:,} / 0x{kernel_length:X} 字节)")
        print(f"  版本: {args.kernel_version}")
    if rootfs_data:
        print(f"RootFS: {args.rootfs} ({rootfs_length:,} / 0x{rootfs_length:X} 字节)")
        print(f"  版本: {args.rootfs_version}")
    print("-" * 60)

    # 创建头部
    header = create_header(args, bit_length, os_length, app_length, kernel_length, rootfs_length)

    # 更新 CRC
    update_crcs(header, bit_data, os_data, app_data, kernel_data, rootfs_data)

    # 打印 CRC 信息
    if bit_data:
        bit_crc = struct.unpack_from('<I', header, 116)[0]
        print(f"Bitstream CRC32: 0x{bit_crc:08X}")
    if os_data:
        os_crc = struct.unpack_from('<I', header, 124)[0]
        print(f"OS CRC32: 0x{os_crc:08X}")
    if app_data:
        app_crc = struct.unpack_from('<I', header, 132)[0]
        print(f"App CRC32: 0x{app_crc:08X}")
    if kernel_data:
        kernel_crc = struct.unpack_from('<I', header, 140)[0]
        print(f"Kernel CRC32: 0x{kernel_crc:08X}")
    if rootfs_data:
        rootfs_crc = struct.unpack_from('<I', header, 148)[0]
        print(f"RootFS CRC32: 0x{rootfs_crc:08X}")

    header_crc = struct.unpack_from('<I', header, 4)[0]
    print(f"Header CRC32: 0x{header_crc:08X}")
    print("-" * 60)

    # 组装固件包
    package = bytearray()
    package.extend(header)
    if bit_data:
        package.extend(bit_data)
    if os_data:
        package.extend(os_data)
    if app_data:
        package.extend(app_data)
    if kernel_data:
        package.extend(kernel_data)
    if rootfs_data:
        package.extend(rootfs_data)

    # 写入文件
    try:
        with open(args.output, 'wb') as f:
            f.write(package)
        print(f"✓ 固件包已生成: {args.output}")
        print(f"  总大小: {len(package):,} (0x{len(package):X}) 字节 ({len(package)/1024/1024:.2f} MB)")
        print("=" * 60)
    except Exception as e:
        print(f"错误: 无法写入输出文件 {args.output}: {e}")
        sys.exit(1)


if __name__ == '__main__':
    main()