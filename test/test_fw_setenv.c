/*
 * fw_setenv 测试程序
 * 测试 fw_setenv 命令在直接调用和 system() 调用时的行为
 * 编译: gcc -o test_fw_setenv test_fw_setenv.c
 * 运行: sudo ./test_fw_setenv [设备路径]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#define DEFAULT_MTD_DEV  "/dev/mtdblock1"

/* ============================================================================
 * 诊断信息输出
 * ============================================================================ */
void print_diagnostics(const char *mtd_dev)
{
    printf("\n");
    printf("========== 系统诊断 ==========\n");

    /* 用户信息 */
    printf("\n[1] 当前用户信息:\n");
    system("id");

    /* 设备信息 */
    printf("\n[2] MTD 设备信息:\n");
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ls -la %s 2>&1", mtd_dev);
    system(cmd);

    /* 设备详情 */
    printf("\n[3] 设备详情:\n");
    snprintf(cmd, sizeof(cmd), "cat /proc/mtd 2>&1 | head -20");
    system(cmd);

    /* 尝试写入测试 */
    printf("\n[4] 写入测试 (echo >):\n");
    snprintf(cmd, sizeof(cmd), "echo 'test' > %s 2>&1 || echo '写入失败 (预期行为)'", mtd_dev);
    system(cmd);

    /* fw_setenv 可用性 */
    printf("\n[5] fw_setenv 命令检查:\n");
    system("which fw_setenv 2>&1");
    system("fw_setenv --help 2>&1 | head -5");

    printf("\n==============================\n");
}

/* ============================================================================
 * 直接调用 fw_setenv (不使用 system())
 * ============================================================================ */
int call_fw_setenv_direct(const char *key, const char *value, const char *mtd_dev)
{
    printf("\n[TEST] 直接调用 fw_setenv\n");
    printf("  key:   %s\n", key);
    printf("  value: %s\n", value);

    /* 构建命令 */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "fw_setenv %s %s 2>&1", key, value);

    printf("  cmd:   %s\n", cmd);

    /* 执行命令 */
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        printf("  [FAIL] popen 失败: %s\n", strerror(errno));
        return -1;
    }

    /* 读取输出 */
    char output[1024] = {0};
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        strncat(output, line, sizeof(output) - strlen(output) - 1);
    }

    int status = pclose(fp);

    printf("  output: %s\n", output);
    printf("  status: %d\n", status);

    if (status == 0) {
        printf("  [PASS] 直接调用成功\n");
        return 0;
    } else {
        printf("  [FAIL] 直接调用失败\n");
        return -1;
    }
}

/* ============================================================================
 * 通过 system() 调用 fw_setenv
 * ============================================================================ */
int call_fw_setenv_via_system(const char *key, const char *value, const char *mtd_dev)
{
    printf("\n[TEST] 通过 system() 调用 fw_setenv\n");
    printf("  key:   %s\n", key);
    printf("  value: %s\n", value);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "fw_setenv %s %s", key, value);
    printf("  cmd:   %s\n", cmd);

    int ret = system(cmd);

    if (ret == 0) {
        printf("  [PASS] system() 调用成功\n");
        return 0;
    } else {
        printf("  [FAIL] system() 调用失败 (ret=%d)\n", ret);
        if (WIFEXITED(ret)) {
            printf("        退出码: %d\n", WEXITSTATUS(ret));
        }
        return -1;
    }
}

/* ============================================================================
 * 带 sudo 的 fw_setenv 调用
 * ============================================================================ */
int call_fw_setenv_with_sudo(const char *key, const char *value)
{
    printf("\n[TEST] 通过 sudo 调用 fw_setenv\n");
    printf("  key:   %s\n", key);
    printf("  value: %s\n", value);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sudo fw_setenv %s %s 2>&1", key, value);
    printf("  cmd:   %s\n", cmd);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        printf("  [FAIL] popen 失败\n");
        return -1;
    }

    char output[1024] = {0};
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        strncat(output, line, sizeof(output) - strlen(output) - 1);
    }

    int status = pclose(fp);
    printf("  output: %s\n", output);

    if (status == 0) {
        printf("  [PASS] sudo 调用成功\n");
        return 0;
    } else {
        printf("  [FAIL] sudo 调用失败\n");
        return -1;
    }
}

/* ============================================================================
 * 安全的 set_uboot_env 函数 (带 fallback)
 * ============================================================================ */
int safe_set_uboot_env(const char *key, const char *value, const char *mtd_dev)
{
    printf("\n[TEST] safe_set_uboot_env (带 fallback)\n");
    printf("  key:   %s\n", key);
    printf("  value: %s\n", value);

    /* 尝试直接调用 */
    printf("  步骤1: 尝试直接调用...\n");
    if (call_fw_setenv_direct(key, value, mtd_dev) == 0) {
        printf("  [OK] 直接调用成功!\n");
        return 0;
    }

    /* 尝试通过 system() 调用 */
    printf("  步骤2: 尝试 system() 调用...\n");
    if (call_fw_setenv_via_system(key, value, mtd_dev) == 0) {
        printf("  [OK] system() 调用成功!\n");
        return 0;
    }

    /* 尝试带 sudo */
    printf("  步骤3: 尝试 sudo 调用...\n");
    if (call_fw_setenv_with_sudo(key, value) == 0) {
        printf("  [OK] sudo 调用成功!\n");
        return 0;
    }

    printf("  [FAIL] 所有方法均失败\n");
    return -1;
}

/* ============================================================================
 * 读取环境变量
 * ============================================================================ */
int read_uboot_env(const char *key)
{
    printf("\n[TEST] 读取环境变量 %s\n", key);

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "fw_printenv %s 2>&1", key);

    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        printf("  [FAIL] 无法执行 fw_printenv\n");
        return -1;
    }

    char output[512] = {0};
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        strncat(output, line, sizeof(output) - strlen(output) - 1);
    }

    int status = pclose(fp);

    if (status == 0 && strlen(output) > 0) {
        printf("  [OK] %s\n", output);
        return 0;
    } else {
        printf("  [FAIL] 变量不存在或读取失败\n");
        return -1;
    }
}

/* ============================================================================
 * 测试用例
 * ============================================================================ */
void test_basic_write(const char *mtd_dev)
{
    printf("\n");
    printf("========================================\n");
    printf("测试1: 基本写入测试\n");
    printf("========================================\n");

    const char *test_key = "bootpart";
    const char *test_value = "1";

    printf("写入 %s = %s\n", test_key, test_value);
    safe_set_uboot_env(test_key, test_value, mtd_dev);
    read_uboot_env(test_key);
}

void test_multiple_vars(const char *mtd_dev)
{
    printf("\n");
    printf("========================================\n");
    printf("测试2: 多变量写入测试\n");
    printf("========================================\n");

    struct {
        const char *key;
        const char *value;
    } tests[] = {
        {"bitstream_b_size", "1048576"},
        {"image_b_size",     "2097152"},
        {"bootpart",         "2"},
        {"fpgapart",         "3"}
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        printf("\n--- 写入 %s = %s ---\n", tests[i].key, tests[i].value);
        safe_set_uboot_env(tests[i].key, tests[i].value, mtd_dev);
    }
}

void test_update_existing(const char *mtd_dev)
{
    printf("\n");
    printf("========================================\n");
    printf("测试3: 更新已存在变量\n");
    printf("========================================\n");

    const char *key = "bootpart";

    /* 读取当前值 */
    printf("读取当前值:\n");
    read_uboot_env(key);

    /* 更新值 */
    printf("\n更新为新值 (99):\n");
    safe_set_uboot_env(key, "99", mtd_dev);

    /* 验证 */
    printf("\n验证新值:\n");
    read_uboot_env(key);
}

void test_permission_issues(void)
{
    printf("\n");
    printf("========================================\n");
    printf("测试4: 权限问题诊断\n");
    printf("========================================\n");

    /* 检查是否是 root */
    if (geteuid() == 0) {
        printf("[INFO] 当前以 root 用户运行\n");
    } else {
        printf("[WARN] 当前用户 UID: %d (非 root)\n", geteuid());
        printf("       建议使用 sudo 运行测试\n");
    }

    /* 检查 fw_setenv 权限 */
    printf("\n检查 fw_setenv 权限:\n");
    system("ls -la $(which fw_setenv)");
}

/* ============================================================================
 * 主函数
 * ============================================================================ */
void print_usage(const char *prog)
{
    printf("用法: %s [设备路径] [选项]\n", prog);
    printf("\n设备路径: 默认为 %s\n", DEFAULT_MTD_DEV);
    printf("\n选项:\n");
    printf("  diag    - 运行诊断测试\n");
    printf("  basic   - 基本写入测试\n");
    printf("  multi   - 多变量测试\n");
    printf("  update  - 更新测试\n");
    printf("  all     - 运行所有测试\n");
    printf("  set <key> <val> - 写入指定变量\n");
    printf("\n示例:\n");
    printf("  sudo %s                    # 运行所有测试\n", prog);
    printf("  sudo %s /dev/mtdblock1 all # 使用指定设备\n", prog);
    printf("  sudo %s diag               # 仅诊断\n", prog);
    printf("  sudo %s set bootpart 1    # 写入单个变量\n", prog);
}

int main(int argc, char *argv[])
{
    const char *mtd_dev = DEFAULT_MTD_DEV;
    int run_all = 1;

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "diag") == 0) {
            print_diagnostics(mtd_dev);
            return 0;
        }
        else if (strcmp(argv[i], "basic") == 0) {
            test_basic_write(mtd_dev);
            return 0;
        }
        else if (strcmp(argv[i], "multi") == 0) {
            test_multiple_vars(mtd_dev);
            return 0;
        }
        else if (strcmp(argv[i], "update") == 0) {
            test_update_existing(mtd_dev);
            return 0;
        }
        else if (strcmp(argv[i], "all") == 0) {
            run_all = 1;
        }
        else if (strcmp(argv[i], "set") == 0 && i + 2 < argc) {
            const char *key = argv[++i];
            const char *value = argv[++i];
            safe_set_uboot_env(key, value, mtd_dev);
            read_uboot_env(key);
            return 0;
        }
        else if (argv[i][0] != '-') {
            mtd_dev = argv[i];
        }
    }

    /* 打印标题 */
    printf("\n");
    printf("########################################\n");
    printf("#      fw_setenv 功能测试程序          #\n");
    printf("########################################\n");
    printf("MTD 设备: %s\n", mtd_dev);
    printf("进程 UID: %d\n", getuid());
    printf("进程 EUID: %d\n", geteuid());

    if (geteuid() != 0) {
        printf("\n[警告] 建议使用 sudo 运行以获得完整测试\n");
    }

    /* 运行所有测试 */
    if (run_all) {
        test_permission_issues();
        test_basic_write(mtd_dev);
        test_multiple_vars(mtd_dev);
        test_update_existing(mtd_dev);
    }

    printf("\n");
    printf("########################################\n");
    printf("#           测试完成                   #\n");
    printf("########################################\n");

    return 0;
}
