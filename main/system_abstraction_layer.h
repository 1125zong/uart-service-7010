#ifndef SYSTEM_ABSTRACTION_LAYER_H
#define SYSTEM_ABSTRACTION_LAYER_H

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>  // 添加stdint.h以支持uint8_t, uint32_t等类型
#include <stdbool.h>

/*-----------------------------*/
/* Type Definitions            */
/*-----------------------------*/
typedef pthread_t TASK_ID;
typedef sem_t* SEM_ID;
typedef struct msg_queue* MSG_Q_ID;
typedef int32_t STATUS;
typedef bool BOOL;

typedef void* (*task_func)(void*) ;

/*-----------------------------*/
/* Status Definitions          */
/*-----------------------------*/
#define OK      0
#define ERROR  -1
#define TRUE    1
#define FALSE   0

/*-----------------------------*/
/* Mutex Options (保留用于兼容性) */
/*-----------------------------*/
#define SEM_Q_FIFO          0x0000
#define SEM_Q_PRIORITY      0x0001
#define SEM_DELETE_SAFE     0x0004
#define SEM_INVERSION_SAFE  0x0008

/*-----------------------------*/
/* Semaphore States            */
/*-----------------------------*/
#define SEM_EMPTY   0
#define SEM_FULL    1

/*-----------------------------*/
/* Message Queue Options       */
/*-----------------------------*/
#define MSG_Q_FIFO      0x0000
#define MSG_Q_PRIORITY  0x0001

/*-----------------------------*/
/* Timeout Definitions         */
/*-----------------------------*/
#define NO_WAIT         0
#define WAIT_FOREVER    -1

/*-----------------------------*/
/* Error Codes                 */
/*-----------------------------*/
#ifndef ERROR
#define ERROR -1
#endif

#ifndef OK
#define OK 0
#endif

/*-----------------------------*/
/* Function Declarations       */
/*-----------------------------*/

// Task functions
TASK_ID task_spawn_wrapper(void* (*task_func)(void*), void* arg, int stack_size);
int task_delay_wrapper(unsigned int ticks);
int task_suspend_wrapper(TASK_ID task_id);
int task_resume_wrapper(TASK_ID task_id);
TASK_ID task_id_self(void);

/*-----------------------------*/
/* Mutex Functions (只保留互斥量功能) */
/*-----------------------------*/
SEM_ID sem_m_create(void);
int sem_take(SEM_ID sem, int timeout);
int sem_give(SEM_ID sem);
int sem_delete(SEM_ID sem);

// Message queue functions
MSG_Q_ID msg_q_create(int max_msgs, int max_msg_length, int options);
int msg_q_send(MSG_Q_ID msg_q, char* buffer, int n_bytes, int timeout, int priority);
int msg_q_receive(MSG_Q_ID msg_q, char* buffer, int max_n_bytes, int timeout);
int msg_q_delete(MSG_Q_ID msg_q);

/*-----------------------------*/
/* Macro Definitions           */
/*-----------------------------*/

// Task macros
#define taskSpawn(name, priority, options, stack_size, func, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10) \
    task_spawn_wrapper((void*(*)(void*))func, (void*)arg1, stack_size)

#define taskDelay(ticks) task_delay_wrapper(ticks)

// Semaphore macros
// 由于我们只实现了互斥量功能，对semBCreate也映射到互斥量创建函数
#define semMCreate(options) sem_m_create()  // 互斥量不再需要参数
#define semTake(sem, timeout) sem_take(sem, timeout)
#define semGive(sem) sem_give(sem)
#define semDelete(sem) sem_delete(sem)

// Message queue macros
#define msgQCreate(max_msgs, max_msg_length, options) msg_q_create(max_msgs, max_msg_length, options)
#define msgQSend(mq, buffer, nBytes, timeout, priority) msg_q_send(mq, buffer, nBytes, timeout, priority)
#define msgQReceive(mq, buffer, max_nBytes, timeout) msg_q_receive(mq, buffer, max_nBytes, timeout)
#define msgQDelete(msg_q) msg_q_delete(msg_q)

#endif /* SYSTEM_ABSTRACTION_LAYER_H */