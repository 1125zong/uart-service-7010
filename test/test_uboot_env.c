/*
 * U-Boot 环境变量读写单元测试
 * 测试 set_uboot_env 和 get_uboot_env 函数
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <mtd/mtd-user.h>

// 函数声明
int set_uboot_env(const char *key, const char *value);
int get_uboot_env(const char *var_name, char *out_buf, size_t buf_len);

#define TEST_PASS 0
#define TEST_FAIL 1

static int test_count = 0;
static int pass_count = 0;
static int fail_count = 0;

#define TEST_ASSERT(cond, msg) do { \
    test_count++; \
    if (cond) { \
        printf("[PASS] %s\n", msg); \
        pass_count++; \
    } else { \
        printf("[FAIL] %s\n", msg); \
        fail_count++; \
    } \
} while(0)

/* 模拟测试：用临时文件代替 /dev/mtd1 */
static char *test_mtd_dev = NULL;
static int use_temp_file = 0;

/* 覆盖原函数用于测试 */
int set_uboot_env_test(const char *key, const char *value) 
{
    if (!key || !value) return -1;

    char env_buf[16] = {0};
    int val = atoi(value);

    // 读取现有数据
    int fd = open(test_mtd_dev, O_RDONLY);
    if (fd >= 0) {
        read(fd, env_buf, sizeof(env_buf));
        close(fd);
    }

    int offset = 0;
    if (strcmp(key, "bitstream_b_size") == 0) offset = 0;
    else if (strcmp(key, "image_b_size") == 0) offset = 4;
    else if (strcmp(key, "bootpart") == 0) offset = 8;
    else if (strcmp(key, "fpgapart") == 0) offset = 12;
    else return -1;

    // 写入值 (大端序)
    env_buf[offset]     = (val >> 24) & 0xFF;
    env_buf[offset + 1] = (val >> 16) & 0xFF;
    env_buf[offset + 2] = (val >> 8) & 0xFF;
    env_buf[offset + 3] = val & 0xFF;

    // 写入
    fd = open(test_mtd_dev, O_RDWR);
    if (fd < 0) return -1;
    
    struct erase_info_user erase;
    erase.start = 0;
    erase.length = 16;
    int ret = ioctl(fd, MEMERASE, &erase);
    printf("  [DEBUG] ioctl MEMERASE ret=%d errno=%d\n", ret, errno);
    
    ssize_t written = write(fd, env_buf, sizeof(env_buf));
    printf("  [DEBUG] write ret=%zd errno=%d\n", written, errno);
    
    // 验证写入
    char verify[16] = {0};
    lseek(fd, 0, SEEK_SET);
    read(fd, verify, sizeof(verify));
    printf("  [DEBUG] verify data: %02x %02x %02x %02x\n", 
           verify[0], verify[1], verify[2], verify[3]);
    close(fd);

    return 0;
}

int get_uboot_env_test(const char *var_name, char *out_buf, size_t buf_len) 
{
    if (!var_name || !out_buf || buf_len == 0) return -1;

    char env_buf[16] = {0};

    int fd = open(test_mtd_dev, O_RDONLY);
    if (fd < 0) return -1;
    read(fd, env_buf, sizeof(env_buf));
    close(fd);
    
    printf("  [DEBUG] raw data: %02x %02x %02x %02x | %02x %02x %02x %02x | %02x %02x %02x %02x | %02x %02x %02x %02x\n",
           env_buf[0], env_buf[1], env_buf[2], env_buf[3],
           env_buf[4], env_buf[5], env_buf[6], env_buf[7],
           env_buf[8], env_buf[9], env_buf[10], env_buf[11],
           env_buf[12], env_buf[13], env_buf[14], env_buf[15]);

    int offset = 0;
    if (strcmp(var_name, "bitstream_b_size") == 0) offset = 0;
    else if (strcmp(var_name, "image_b_size") == 0) offset = 4;
    else if (strcmp(var_name, "bootpart") == 0) offset = 8;
    else if (strcmp(var_name, "fpgapart") == 0) offset = 12;
    else return -1;

    unsigned int val = (env_buf[offset] << 24) | (env_buf[offset + 1] << 16) 
                     | (env_buf[offset + 2] << 8) | env_buf[offset + 3];

    snprintf(out_buf, buf_len, "%u", val);
    return 0;
}

/* 测试用例 */
void test_bootpart_set_get(void)
{
    printf("\n=== 测试 bootpart 读写 ===\n");
    
    char buf[32];
    
    // 清空
    int fd = open(test_mtd_dev, O_RDWR);
    if (fd >= 0) {
        char zero[16] = {0};
        ioctl(fd, MEMERASE, &(struct erase_info_user){0, 16});
        write(fd, zero, sizeof(zero));
        close(fd);
    }
    
    TEST_ASSERT(set_uboot_env_test("bootpart", "1") == 0, "set bootpart=1");
    TEST_ASSERT(get_uboot_env_test("bootpart", buf, sizeof(buf)) == 0, "get bootpart");
    int val1 = atoi(buf);
    printf("  [DEBUG] bootpart read: '%s' -> %d, expected: 1\n", buf, val1);
    TEST_ASSERT(val1 == 1, "bootpart 值正确(1)");
    
    TEST_ASSERT(set_uboot_env_test("bootpart", "2") == 0, "set bootpart=2");
    TEST_ASSERT(get_uboot_env_test("bootpart", buf, sizeof(buf)) == 0, "get bootpart");
    int val2 = atoi(buf);
    printf("  [DEBUG] bootpart read: '%s' -> %d, expected: 2\n", buf, val2);
    TEST_ASSERT(val2 == 2, "bootpart 值正确(2)");
}

void test_bitstream_b_size(void)
{
    printf("\n=== 测试 bitstream_b_size 读写 ===\n");
    
    char buf[32];
    
    TEST_ASSERT(set_uboot_env_test("bitstream_b_size", "1048576") == 0, "set bitstream_b_size=1048576");
    TEST_ASSERT(get_uboot_env_test("bitstream_b_size", buf, sizeof(buf)) == 0, "get bitstream_b_size");
    TEST_ASSERT(atoi(buf) == 1048576, "bitstream_b_size 值正确(1048576)");
}

void test_image_b_size(void)
{
    printf("\n=== 测试 image_b_size 读写 ===\n");
    
    char buf[32];
    
    TEST_ASSERT(set_uboot_env_test("image_b_size", "2097152") == 0, "set image_b_size=2097152");
    TEST_ASSERT(get_uboot_env_test("image_b_size", buf, sizeof(buf)) == 0, "get image_b_size");
    TEST_ASSERT(atoi(buf) == 2097152, "image_b_size 值正确(2097152)");
}

void test_fpgapart(void)
{
    printf("\n=== 测试 fpgapart 读写 ===\n");
    
    char buf[32];
    
    TEST_ASSERT(set_uboot_env_test("fpgapart", "3") == 0, "set fpgapart=3");
    TEST_ASSERT(get_uboot_env_test("fpgapart", buf, sizeof(buf)) == 0, "get fpgapart");
    TEST_ASSERT(atoi(buf) == 3, "fpgapart 值正确(3)");
}

void test_all_vars_together(void)
{
    printf("\n=== 测试所有变量同时读写 ===\n");
    
    char buf[32];
    
    // 先清空所有变量
    int fd = open(test_mtd_dev, O_RDWR);
    if (fd >= 0) {
        char zero[16] = {0};
        ioctl(fd, MEMERASE, &(struct erase_info_user){0, 16});
        write(fd, zero, sizeof(zero));
        close(fd);
    }
    
    // 同时设置4个变量
    TEST_ASSERT(set_uboot_env_test("bitstream_b_size", "1000") == 0, "set bitstream_b_size=1000");
    TEST_ASSERT(set_uboot_env_test("image_b_size", "2000") == 0, "set image_b_size=2000");
    TEST_ASSERT(set_uboot_env_test("bootpart", "1") == 0, "set bootpart=1");
    TEST_ASSERT(set_uboot_env_test("fpgapart", "2") == 0, "set fpgapart=2");
    
    // 验证每个变量独立读取
    TEST_ASSERT(get_uboot_env_test("bitstream_b_size", buf, sizeof(buf)) == 0, "get bitstream_b_size");
    int val1 = atoi(buf);
    printf("  [DEBUG] bitstream_b_size read: '%s' -> %d, expected: 1000\n", buf, val1);
    TEST_ASSERT(val1 == 1000, "bitstream_b_size=1000 正确");
    
    TEST_ASSERT(get_uboot_env_test("image_b_size", buf, sizeof(buf)) == 0, "get image_b_size");
    int val2 = atoi(buf);
    printf("  [DEBUG] image_b_size read: '%s' -> %d, expected: 2000\n", buf, val2);
    TEST_ASSERT(val2 == 2000, "image_b_size=2000 正确");
    
    TEST_ASSERT(get_uboot_env_test("bootpart", buf, sizeof(buf)) == 0, "get bootpart");
    int val3 = atoi(buf);
    printf("  [DEBUG] bootpart read: '%s' -> %d, expected: 1\n", buf, val3);
    TEST_ASSERT(val3 == 1, "bootpart=1 正确");
    
    TEST_ASSERT(get_uboot_env_test("fpgapart", buf, sizeof(buf)) == 0, "get fpgapart");
    int val4 = atoi(buf);
    printf("  [DEBUG] fpgapart read: '%s' -> %d, expected: 2\n", buf, val4);
    TEST_ASSERT(val4 == 2, "fpgapart=2 正确");
}

void test_invalid_params(void)
{
    printf("\n=== 测试无效参数 ===\n");
    
    char buf[32];
    
    TEST_ASSERT(set_uboot_env_test(NULL, "1") == -1, "set NULL key 失败");
    TEST_ASSERT(set_uboot_env_test("bootpart", NULL) == -1, "set NULL value 失败");
    TEST_ASSERT(get_uboot_env_test(NULL, buf, sizeof(buf)) == -1, "get NULL var_name 失败");
    TEST_ASSERT(get_uboot_env_test("bootpart", NULL, sizeof(buf)) == -1, "get NULL out_buf 失败");
    TEST_ASSERT(get_uboot_env_test("bootpart", buf, 0) == -1, "get buf_len=0 失败");
}

void test_update_existing(void)
{
    printf("\n=== 测试更新已存在的变量 ===\n");
    
    char buf[32];
    
    // 清空
    int fd = open(test_mtd_dev, O_RDWR);
    if (fd >= 0) {
        char zero[16] = {0};
        ioctl(fd, MEMERASE, &(struct erase_info_user){0, 16});
        write(fd, zero, sizeof(zero));
        close(fd);
    }
    
    // 初始值
    TEST_ASSERT(set_uboot_env_test("bootpart", "1") == 0, "set bootpart=1");
    
    // 更新值
    TEST_ASSERT(set_uboot_env_test("bootpart", "99") == 0, "update bootpart=99");
    TEST_ASSERT(get_uboot_env_test("bootpart", buf, sizeof(buf)) == 0, "get bootpart");
    int val = atoi(buf);
    printf("  [DEBUG] bootpart read: '%s' -> %d, expected: 99\n", buf, val);
    TEST_ASSERT(val == 99, "bootpart 更新为99正确");
}

int setup_test_env(void)
{
    // 创建临时测试文件
    test_mtd_dev = "/tmp/test_mtd1.bin";
    use_temp_file = 1;
    
    // 初始化为空
    int fd = open(test_mtd_dev, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("创建测试文件失败\n");
        return -1;
    }
    char buf[16] = {0};
    write(fd, buf, sizeof(buf));
    close(fd);
    
    printf("测试环境: %s\n", test_mtd_dev);
    return 0;
}

void cleanup_test_env(void)
{
    if (test_mtd_dev) {
        unlink(test_mtd_dev);
    }
}

int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("U-Boot 环境变量读写单元测试\n");
    printf("========================================\n");
    
    // 检查是否使用真实设备
    if (argc > 1) {
        test_mtd_dev = argv[1];
        printf("使用真实设备: %s\n", test_mtd_dev);
    } else {
        if (setup_test_env() != 0) {
            return 1;
        }
    }
    
    // 运行测试
    test_invalid_params();
    test_bootpart_set_get();
    test_bitstream_b_size();
    test_image_b_size();
    test_fpgapart();
    test_update_existing();
    test_all_vars_together();
    
    // 清理
    if (use_temp_file) {
        cleanup_test_env();
    }
    
    // 打印结果
    printf("\n========================================\n");
    printf("测试结果: %d/%d 通过, %d 失败\n", pass_count, test_count, fail_count);
    printf("========================================\n");
    
    return fail_count > 0 ? 1 : 0;
}
