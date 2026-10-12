#ifndef __APP_CRASH_HANDLER_H__
#define __APP_CRASH_HANDLER_H__

/**
 * @brief 注册崩溃信号处理函数
 * @details 捕获 SIGSEGV/SIGABRT/SIGFPE/SIGBUS/SIGILL
 * 等致命信号，生成崩溃日志文件。
 * 在 main() 或其他初始化函数中调用一次即可。
 */
void crash_handler_init(void);

#endif /* __APP_CRASH_HANDLER_H__ */
