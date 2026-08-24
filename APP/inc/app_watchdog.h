#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

/**
 * @brief 根据Flash配置自动初始化看门狗
 * @details 系统上电时调用，检查看门狗配置，如使能则启动喂狗
 */
void watchdog_init_from_config(void);

/**
 * @brief 配置看门狗
 * @param enable 1=使能, 0=禁用
 * @return 0成功, -1失败
 */
int watchdog_config(unsigned char enable);

#endif /* APP_WATCHDOG_H */
