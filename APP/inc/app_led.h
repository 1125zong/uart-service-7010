#ifndef APP_LED_H
#define APP_LED_H

/* start/stop 由主线程调用。成功返回0，失败返回错误码。 */
int app_led_start(void);
int app_led_stop(void);

/* port 使用内部索引：0～NUM_PORTS-1。
 * 可以在线程启动前调用，状态会缓存。
 */
void app_led_set_port_lock(int port, int locked);

#endif