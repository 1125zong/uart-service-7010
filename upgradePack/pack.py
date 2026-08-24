#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import argparse

def main():
    parser = argparse.ArgumentParser(description='将 image.ub 和 rootfs.jffs2 打包为单一系统镜像')
    parser.add_argument('--image', required=True, help='image.ub 文件路径')
    parser.add_argument('--rootfs', required=True, help='rootfs.jffs2 文件路径')
    parser.add_argument('--output', default='system.img', help='输出镜像文件名 (默认: system.img)')
    parser.add_argument('--offset-file', default='rootoffset.txt', help='输出 rootoffset 值的文件 (默认: rootoffset.txt)')
    args = parser.parse_args()

    # 读取文件
    with open(args.image, 'rb') as f:
        image_data = f.read()
    with open(args.rootfs, 'rb') as f:
        rootfs_data = f.read()

    # 计算偏移量（字节）
    offset = len(image_data)

    # 写入输出文件
    with open(args.output, 'wb') as f:
        f.write(image_data)
        f.write(rootfs_data)

    # 将偏移量写入文件（以十六进制格式，方便 U-Boot 使用）
    with open(args.offset_file, 'w') as f:
        f.write(f"0x{offset:X}")

    print(f"打包完成：{args.output}")
    print(f"image.ub 大小: {offset} 字节")
    print(f"rootfs 大小: {len(rootfs_data)} 字节")
    print(f"rootoffset 值: 0x{offset:X} (已保存至 {args.offset_file})")

if __name__ == '__main__':
    main()