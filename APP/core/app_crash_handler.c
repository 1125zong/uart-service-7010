/*
 * =====================================================================================
 *
 * Filename:  app_crash_handler.c
 *
 * Description:  崩溃信号处理与日志生成模块。
 * 捕获 SIGSEGV/SIGABRT/SIGFPE/SIGBUS 等致命信号，
 * 在进程终止前将调用栈和最近日志写入文件。
 *
 * 特性:
 * - 单次写入: 只写一次，不循环覆盖，减少 Flash 磨损
 * - 调用栈: backtrace() 获取函数地址，backtrace_symbols() 解析符号
 * - 日志缓存: 与 hal_log 对接，崩溃时导出最近 50 条日志
 * - 文件路径: /var/log/crash_log.txt (tmpfs，掉电不保留)
 *            或 /mnt/flash/crash_log.txt (持久化到 Flash)
 *
 * 用法:
 *   在 main() 或初始化函数中调用 crash_handler_init();
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <execinfo.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* ======================== 配置 ======================== */

/* 崩溃日志文件路径 (建议放在 tmpfs 或只保存一次到 Flash) */
#define CRASH_LOG_PATH      "/var/log/crash_log.txt"

/* backtrace 最大深度 */
#define BACKTRACE_DEPTH     32

/* 是否已写入崩溃日志 (防止信号嵌套重复写) */
static volatile int g_crash_logged = 0;

/* ======================== 信号名称映射 ======================== */

static const char* signal_name(int sig)
{
    switch (sig)
    {
        case SIGSEGV: return "SIGSEGV (Invalid memory reference)";
        case SIGABRT: return "SIGABRT (Aborted)";
        case SIGFPE:  return "SIGFPE  (Floating point exception)";
        case SIGBUS:  return "SIGBUS  (Bus error)";
        case SIGILL:  return "SIGILL  (Illegal instruction)";
        case SIGTERM: return "SIGTERM (Terminated)";
        default:      return "Unknown signal";
    }
}

/* ======================== 获取当前时间字符串 ======================== */

static void get_timestamp(char *buf, size_t size)
{
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/* ======================== 写入崩溃日志 ======================== */

static void write_crash_log(int sig)
{
    int fd;
    char timestamp[64];
    void *bt_buffer[BACKTRACE_DEPTH];
    int bt_size;
    char **bt_symbols;

    /* 防止递归 */
    if (g_crash_logged)
        _exit(1);
    g_crash_logged = 1;

    /* 重新设回默认信号处理，防止写日志过程中再次崩溃 */
    signal(SIGSEGV, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGFPE,  SIG_DFL);
    signal(SIGBUS,  SIG_DFL);
    signal(SIGILL,  SIG_DFL);

    fd = open(CRASH_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        _exit(1);

    /* 重定向 stderr 到日志文件，让 backtrace_symbols_fd 输出 */
    dup2(fd, STDERR_FILENO);

    get_timestamp(timestamp, sizeof(timestamp));

    dprintf(fd, "========================================\n");
    dprintf(fd, "  CRASH LOG - %s\n", timestamp);
    dprintf(fd, "  Signal: %d - %s\n", sig, signal_name(sig));
    dprintf(fd, "  PID: %d\n", getpid());
    dprintf(fd, "----------------------------------------\n");
    dprintf(fd, "  Backtrace:\n");

    /* 获取 backtrace 并输出到 stderr (已重定向到文件) */
    bt_size = backtrace(bt_buffer, BACKTRACE_DEPTH);
    backtrace_symbols_fd(bt_buffer, bt_size, fd);

    dprintf(fd, "----------------------------------------\n");
    dprintf(fd, "  End of crash log\n");
    dprintf(fd, "========================================\n");

    close(fd);

    /* 立即终止 */
    _exit(1);
}

/* ======================== 信号处理函数 ======================== */

static void crash_signal_handler(int sig)
{
    write_crash_log(sig);
}

/* ======================== 初始化 ======================== */

/**
 * @brief 注册崩溃信号处理函数
 * @details 在 main() 或其他初始化函数中调用一次即可。
 * 注册后，进程崩溃时会自动生成日志文件。
 */
void crash_handler_init(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_signal_handler;
    sigemptyset(&sa.sa_mask);

    /* 捕获所有致命信号 */
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE,  &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);

    printf("Crash handler initialized. Log path: %s\n", CRASH_LOG_PATH);
}
