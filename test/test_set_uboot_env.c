/*
 * set_uboot_env 函数测试程序
 * 用于验证环境变量写入功能
 * 编译: gcc -o test_set_env test_set_uboot_env.c
 * 运行: ./test_set_env [mtd_device]
 *       例如: ./test_set_env /dev/mtdblock1
 *       或:   ./test_set_env /tmp/test_mtd.bin
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* 环境变量偏移定义 (与 app_update_env.c 保持一致) */
typedef enum {
    ENV_OFFSET_BITSTREAM_B = 0,   /* bitstream_b_size: 偏移0, 4字节 */
    ENV_OFFSET_IMAGE_B     = 4,   /* image_b_size: 偏移4, 4字节 */
    ENV_OFFSET_BOOTPART    = 8,   /* bootpart: 偏移8, 4字节 */
    ENV_OFFSET_FPGAPART    = 12   /* fpgapart: 偏移12, 4字节 */
} env_offset_t;

/* 测试变量列表 */
typedef struct {
    const char *name;
    int offset;
} env_var_t;

static const env_var_t env_vars[] = {
    {"bitstream_b_size", ENV_OFFSET_BITSTREAM_B},
    {"image_b_size",     ENV_OFFSET_IMAGE_B},
    {"bootpart",         ENV_OFFSET_BOOTPART},
    {"fpgapart",         ENV_OFFSET_FPGAPART}
};

/* ============================================================================
 * set_uboot_env - 写入环境变量 (复制自 app_update_env.c)
 * ============================================================================ */
int set_uboot_env(const char *mtd_dev, const char *key, const char *value)
{
    if (!key || !value) {
        fprintf(stderr, "[ERROR] 参数无效: key=%p, value=%p\n", key, value);
        return -1;
    }

    char env_buf[16] = {0};
    int val = atoi(value);

    printf("[INFO] 写入: key=%s, value=%s, val=%d\n", key, value, val);

    /* 读取现有数据 */
    int fd = open(mtd_dev, O_RDONLY);
    if (fd >= 0) {
        read(fd, env_buf, sizeof(env_buf));
        close(fd);
    }

    /* 打印写入前的数据 */
    printf("[BEFORE] %02x %02x %02x %02x | %02x %02x %02x %02x | %02x %02x %02x %02x | %02x %02x %02x %02x\n",
           env_buf[0],  env_buf[1],  env_buf[2],  env_buf[3],
           env_buf[4],  env_buf[5],  env_buf[6],  env_buf[7],
           env_buf[8],  env_buf[9],  env_buf[10], env_buf[11],
           env_buf[12], env_buf[13], env_buf[14], env_buf[15]);

    /* 根据key确定偏移 */
    int offset = -1;
    if (strcmp(key, "bitstream_b_size") == 0)      offset = ENV_OFFSET_BITSTREAM_B;
    else if (strcmp(key, "image_b_size") == 0)     offset = ENV_OFFSET_IMAGE_B;
    else if (strcmp(key, "bootpart") == 0)         offset = ENV_OFFSET_BOOTPART;
    else if (strcmp(key, "fpgapart") == 0)         offset = ENV_OFFSET_FPGAPART;
    else {
        fprintf(stderr, "[ERROR] 不支持的变量: %s\n", key);
        return -1;
    }

    /* 写入值 (大端序, 4字节) */
    env_buf[offset]     = (val >> 24) & 0xFF;
    env_buf[offset + 1] = (val >> 16) & 0xFF;
    env_buf[offset + 2] = (val >> 8) & 0xFF;
    env_buf[offset + 3] = val & 0xFF;

    /* 写入设备 */
    fd = open(mtd_dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] 打开设备 %s 失败: %s\n", mtd_dev, strerror(errno));
        return -1;
    }

    ssize_t written = write(fd, env_buf, sizeof(env_buf));
    if (written < 0) {
        fprintf(stderr, "[ERROR] 写入失败: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    printf("[DEBUG] write 返回: %zd 字节\n", written);

    /* 验证写入 */
    char verify_buf[16] = {0};
    lseek(fd, 0, SEEK_SET);
    read(fd, verify_buf, sizeof(verify_buf));

    /* 打印写入后的数据 */
    printf("[AFTER ] %02x %02x %02x %02x | %02x %02x %02x %02x | %02x %02x %02x %02x | %02x %02x %02x %02x\n",
           verify_buf[0],  verify_buf[1],  verify_buf[2],  verify_buf[3],
           verify_buf[4],  verify_buf[5],  verify_buf[6],  verify_buf[7],
           verify_buf[8],  verify_buf[9],  verify_buf[10], verify_buf[11],
           verify_buf[12], verify_buf[13], verify_buf[14], verify_buf[15]);

    /* 验证写入值是否正确 */
    int verify_val = (verify_buf[offset] << 24) | (verify_buf[offset + 1] << 16) |
                     (verify_buf[offset + 2] << 8) | verify_buf[offset + 3];

    close(fd);

    if (verify_val != val) {
        fprintf(stderr, "[ERROR] 验证失败: 期望=%d, 实际=%d\n", val, verify_val);
        return -1;
    }

    printf("[PASS ] %s = %d 写入成功\n", key, val);
    return 0;
}

/* ============================================================================
 * 读取并显示设备数据
 * ============================================================================ */
void dump_mtd_data(const char *mtd_dev)
{
    int fd = open(mtd_dev, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] 无法打开 %s: %s\n", mtd_dev, strerror(errno));
        return;
    }

    unsigned char buf[16] = {0};
    read(fd, buf, sizeof(buf));
    close(fd);

    printf("\n========== %s 数据 dump ==========\n", mtd_dev);
    printf("偏移 0-3  (bitstream_b_size):  %02x %02x %02x %02x  -> %d\n",
           buf[0], buf[1], buf[2], buf[3],
           (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3]);
    printf("偏移 4-7  (image_b_size):      %02x %02x %02x %02x  -> %d\n",
           buf[4], buf[5], buf[6], buf[7],
           (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7]);
    printf("偏移 8-11 (bootpart):         %02x %02x %02x %02x  -> %d\n",
           buf[8], buf[9], buf[10], buf[11],
           (buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11]);
    printf("偏移 12-15(fpgapart):         %02x %02x %02x %02x  -> %d\n",
           buf[12], buf[13], buf[14], buf[15],
           (buf[12] << 24) | (buf[13] << 16) | (buf[14] << 8) | buf[15]);
    printf("十六进制: ");
    for (int i = 0; i < 16; i++) printf("%02x ", buf[i]);
    printf("\n==================================\n\n");
}

/* ============================================================================
 * 清空设备数据
 * ============================================================================ */
void clear_mtd(const char *mtd_dev)
{
    int fd = open(mtd_dev, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        fprintf(stderr, "[ERROR] 无法打开 %s: %s\n", mtd_dev, strerror(errno));
        return;
    }
    unsigned char buf[16] = {0};
    write(fd, buf, sizeof(buf));
    close(fd);
    printf("[INFO] 已清空 %s\n", mtd_dev);
}

/* ============================================================================
 * 测试用例
 * ============================================================================ */
int test_single_var(const char *mtd_dev, const char *key, const char *value)
{
    printf("\n[TEST] 写入单个变量: %s = %s\n", key, value);
    int ret = set_uboot_env(mtd_dev, key, value);
    if (ret != 0) {
        printf("[FAIL] 测试失败!\n");
        return -1;
    }
    printf("[PASS] 测试通过!\n");
    return 0;
}

int test_all_vars(const char *mtd_dev)
{
    printf("\n========== 测试所有变量同时写入 ==========\n");

    clear_mtd(mtd_dev);
    dump_mtd_data(mtd_dev);

    /* 依次写入所有变量 */
    if (test_single_var(mtd_dev, "bitstream_b_size", "1048576") != 0) return -1;
    if (test_single_var(mtd_dev, "image_b_size", "2097152") != 0) return -1;
    if (test_single_var(mtd_dev, "bootpart", "1") != 0) return -1;
    if (test_single_var(mtd_dev, "fpgapart", "2") != 0) return -1;

    printf("\n========== 最终数据 dump ==========\n");
    dump_mtd_data(mtd_dev);

    /* 验证所有值 */
    int fd = open(mtd_dev, O_RDONLY);
    unsigned char buf[16] = {0};
    read(fd, buf, sizeof(buf));
    close(fd);

    int errors = 0;
    unsigned int expected[] = {1048576, 2097152, 1, 2};
    for (int i = 0; i < 4; i++) {
        unsigned int val = (buf[i*4] << 24) | (buf[i*4+1] << 16) |
                          (buf[i*4+2] << 8) | buf[i*4+3];
        if (val != expected[i]) {
            printf("[FAIL] %s: 期望=%u, 实际=%u\n", env_vars[i].name, expected[i], val);
            errors++;
        } else {
            printf("[PASS] %s = %u\n", env_vars[i].name, val);
        }
    }

    return errors > 0 ? -1 : 0;
}

int test_update_var(const char *mtd_dev)
{
    printf("\n========== 测试更新已存在的变量 ==========\n");

    /* 设置初始值 */
    set_uboot_env(mtd_dev, "bootpart", "1");
    dump_mtd_data(mtd_dev);

    /* 更新值 */
    set_uboot_env(mtd_dev, "bootpart", "99");
    dump_mtd_data(mtd_dev);

    /* 验证 */
    int fd = open(mtd_dev, O_RDONLY);
    unsigned char buf[16] = {0};
    read(fd, buf, sizeof(buf));
    close(fd);

    unsigned int val = (buf[8] << 24) | (buf[9] << 16) | (buf[10] << 8) | buf[11];
    if (val == 99) {
        printf("[PASS] 更新测试通过: bootpart = 99\n");
        return 0;
    } else {
        printf("[FAIL] 更新测试失败: bootpart = %u (期望 99)\n", val);
        return -1;
    }
}

/* ============================================================================
 * 主函数
 * ============================================================================ */
void print_usage(const char *prog)
{
    printf("用法: %s [mtd_device] [选项]\n", prog);
    printf("\n选项:\n");
    printf("  dump              - 显示设备数据\n");
    printf("  clear             - 清空设备数据\n");
    printf("  test              - 运行所有测试\n");
    printf("  set <key> <value> - 写入指定变量\n");
    printf("\n示例:\n");
    printf("  %s /dev/mtdblock1 dump\n", prog);
    printf("  %s /dev/mtdblock1 set bootpart 1\n", prog);
    printf("  %s /dev/mtdblock1 test\n", prog);
    printf("\n支持的变量: bitstream_b_size, image_b_size, bootpart, fpgapart\n");
}

int main(int argc, char *argv[])
{
    const char *mtd_dev;

    /* 检查参数 */
    if (argc < 2) {
        /* 使用默认测试文件 */
        mtd_dev = "/tmp/test_mtd_env.bin";
        printf("[INFO] 未指定设备, 使用默认: %s\n", mtd_dev);

        /* 创建测试文件 */
        int fd = open(mtd_dev, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            unsigned char buf[16] = {0};
            write(fd, buf, sizeof(buf));
            close(fd);
            printf("[INFO] 已创建测试文件\n");
        }
    } else {
        mtd_dev = argv[1];
    }

    printf("========================================\n");
    printf("set_uboot_env 函数测试\n");
    printf("设备: %s\n", mtd_dev);
    printf("========================================\n");

    /* 处理命令 */
    if (argc >= 3 && strcmp(argv[2], "dump") == 0) {
        dump_mtd_data(mtd_dev);
    }
    else if (argc >= 3 && strcmp(argv[2], "clear") == 0) {
        clear_mtd(mtd_dev);
    }
    else if (argc >= 3 && strcmp(argv[2], "test") == 0) {
        test_all_vars(mtd_dev);
        test_update_var(mtd_dev);
    }
    else if (argc >= 5 && strcmp(argv[2], "set") == 0) {
        const char *key = argv[3];
        const char *value = argv[4];
        set_uboot_env(mtd_dev, key, value);
        dump_mtd_data(mtd_dev);
    }
    else {
        /* 默认: 运行所有测试 */
        test_all_vars(mtd_dev);
        test_update_var(mtd_dev);
    }

    return 0;
}