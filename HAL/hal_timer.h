#ifndef HAL_TIMER_H
#define HAL_TIMER_H

//#include<timer.h>

//#include <vxWorks.h>

#ifdef __cplusplus
extern "C" {
#endif


/* �����û���̬ע��������������� */
typedef void (*APP_TASK_CALLBACK)(void *arg);


void app_start_hz(int unit, int hz);
//int app_register_task(AcoreOs_timer_service_func func, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIMER_H */
