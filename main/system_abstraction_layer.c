#include "system_abstraction_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

/*-----------------------------*/
/* Internal Data Structures    */
/*-----------------------------*/
typedef struct 
{
    pthread_mutex_t mutex;
} sem_internal_t;

typedef struct msg_queue 
{
    char** messages;
    int* message_sizes;
    int front;
    int rear;
    int count;
    int max_msgs;
    int max_msg_length;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} msg_queue_t;

/*-----------------------------*/
/* Task Functions              */
/*-----------------------------*/

TASK_ID task_spawn_wrapper(void* (*task_func)(void*), void* arg, int stack_size)
{
    pthread_t thread;
    pthread_attr_t attr;
    
    pthread_attr_init(&attr);
    if (stack_size > 0) {
        pthread_attr_setstacksize(&attr, stack_size);
    }
    
    if (pthread_create(&thread, &attr, task_func, arg) != 0) {
        return (TASK_ID)ERROR;
    }
    
    pthread_attr_destroy(&attr);
    return thread;
}

int task_delay_wrapper(unsigned int ticks)
{
    // Assuming system tick is 10ms (common in many systems)
    // Adjust this value based on your system's tick rate
    struct timespec req, rem;
    req.tv_sec = 0;
    req.tv_nsec = ticks * 10000000; // Convert ticks to nanoseconds (assuming 10ms per tick)
    
    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;  // Retry with remaining time
    }
    
    return OK;
}

int task_suspend_wrapper(TASK_ID task_id)
{
    // In standard POSIX threads, there's no direct equivalent to task suspend
    // This would typically be implemented using condition variables by the application
    // For now, return ERROR as this is not directly supported
    (void)task_id;
    return ERROR;
}

int task_resume_wrapper(TASK_ID task_id)
{
    // In standard POSIX threads, there's no direct equivalent to task resume
    // This would typically be implemented using condition variables by the application
    // For now, return ERROR as this is not directly supported
    (void)task_id;
    return ERROR;
}

TASK_ID task_id_self(void)
{
    return pthread_self();
}

/*-----------------------------*/
/* Semaphore Functions         */
/*-----------------------------*/

// 移除二值信号量相关函数，只保留互斥量功能
// 以下函数不再需要：
// SEM_ID sem_b_create(int options, int initial_state)

SEM_ID sem_m_create(void)
{
    sem_internal_t* sem = malloc(sizeof(sem_internal_t));
    if (!sem) {
        return NULL;
    }
    
    int result = pthread_mutex_init(&sem->mutex, NULL);
    
    if (result != 0) {
        free(sem);
        return NULL;
    }
    
    return (SEM_ID)sem;
}

int sem_take(SEM_ID sem, int timeout)
{
    if (!sem) {
        return ERROR;
    }
    
    sem_internal_t* sem_int = (sem_internal_t*)sem;
    
    if (timeout == NO_WAIT) {
        int result = pthread_mutex_trylock(&sem_int->mutex);
        if (result == EBUSY) {
            return ERROR;  // Would block
        }
        return (result == 0) ? OK : ERROR;
    } else {
        // 对于WAIT_FOREVER或其他值，都执行标准锁定（无超时）
        return pthread_mutex_lock(&sem_int->mutex) == 0 ? OK : ERROR;
    }
}

int sem_give(SEM_ID sem)
{
    if (!sem) {
        return ERROR;
    }
    
    sem_internal_t* sem_int = (sem_internal_t*)sem;
    
    return pthread_mutex_unlock(&sem_int->mutex) == 0 ? OK : ERROR;
}

int sem_delete(SEM_ID sem)
{
    if (!sem) 
    {
        return ERROR;
    }
    
    sem_internal_t* sem_int = (sem_internal_t*)sem;
    
    pthread_mutex_destroy(&sem_int->mutex);
    
    free(sem_int);
    return OK;
}

/*-----------------------------*/
/* Message Queue Functions     */
/*-----------------------------*/

MSG_Q_ID msg_q_create(int max_msgs, int max_msg_length, int options)
{
    msg_queue_t* mq = malloc(sizeof(msg_queue_t));
    if (!mq) {
        return NULL;
    }
    
    mq->max_msgs = max_msgs;
    mq->max_msg_length = max_msg_length;
    mq->front = 0;
    mq->rear = 0;
    mq->count = 0;
    
    mq->messages = malloc(max_msgs * sizeof(char*));
    if (!mq->messages) {
        free(mq);
        return NULL;
    }
    
    for (int i = 0; i < max_msgs; i++) {
        mq->messages[i] = malloc(max_msg_length);
        if (!mq->messages[i]) {
            // Clean up previously allocated messages
            for (int j = 0; j < i; j++) {
                free(mq->messages[j]);
            }
            free(mq->messages);
            free(mq);
            return NULL;
        }
    }
    
    mq->message_sizes = malloc(max_msgs * sizeof(int));
    if (!mq->message_sizes) {
        // Clean up
        for (int i = 0; i < max_msgs; i++) {
            free(mq->messages[i]);
        }
        free(mq->messages);
        free(mq);
        return NULL;
    }
    
    if (pthread_mutex_init(&mq->mutex, NULL) != 0) {
        // Clean up
        free(mq->message_sizes);
        for (int i = 0; i < max_msgs; i++) {
            free(mq->messages[i]);
        }
        free(mq->messages);
        free(mq);
        return NULL;
    }
    
    if (pthread_cond_init(&mq->not_empty, NULL) != 0 ||
        pthread_cond_init(&mq->not_full, NULL) != 0) {
        // Clean up
        pthread_mutex_destroy(&mq->mutex);
        free(mq->message_sizes);
        for (int i = 0; i < max_msgs; i++) {
            free(mq->messages[i]);
        }
        free(mq->messages);
        free(mq);
        return NULL;
    }
    
    (void)options;  // Unused for now
    
    return (MSG_Q_ID)mq;
}

int msg_q_send(MSG_Q_ID msg_q, char* buffer, int n_bytes, int timeout, int priority)
{
    if (!msg_q || !buffer || n_bytes <= 0) {
        return ERROR;
    }
    if( priority < 0)
    {
        printf("Priority must be non-negative\n");
    }
    
    msg_queue_t* mq = (msg_queue_t*)msg_q;
    
    struct timespec abs_timeout;
    if (timeout != WAIT_FOREVER) {
        struct timeval now;
        gettimeofday(&now, NULL);
        abs_timeout.tv_sec = now.tv_sec + timeout / 1000;  // Assuming timeout is in ms
        abs_timeout.tv_nsec = (now.tv_usec + (timeout % 1000) * 1000) * 1000;
        
        if (abs_timeout.tv_nsec >= 1000000000) {
            abs_timeout.tv_sec++;
            abs_timeout.tv_nsec -= 1000000000;
        }
    }
    
    pthread_mutex_lock(&mq->mutex);
    
    // Wait until queue is not full
    int result = OK;
    if (timeout == NO_WAIT && mq->count >= mq->max_msgs) 
    {
        result = ERROR;
    } 
    else if (timeout == WAIT_FOREVER) 
    {
        while (mq->count >= mq->max_msgs) 
        {
            pthread_cond_wait(&mq->not_full, &mq->mutex);
        }
    } 
    else if (timeout != NO_WAIT) 
    {
        while (mq->count >= mq->max_msgs) 
        {
            if (pthread_cond_timedwait(&mq->not_full, &mq->mutex, &abs_timeout) == ETIMEDOUT) 
            {
                result = ERROR;
                break;
            }
        }
    } 
    else 
    {
        // NO_WAIT case where queue is full
        if (mq->count >= mq->max_msgs) 
        {
            result = ERROR;
        }
    }
    
    if (result == OK && mq->count < mq->max_msgs) 
    {
        int insert_pos = mq->rear;
        memcpy(mq->messages[insert_pos], buffer, n_bytes > mq->max_msg_length ? mq->max_msg_length : n_bytes);
        mq->message_sizes[insert_pos] = n_bytes > mq->max_msg_length ? mq->max_msg_length : n_bytes;
        
        mq->rear = (mq->rear + 1) % mq->max_msgs;
        mq->count++;
        
        // Signal that queue is not empty
        pthread_cond_signal(&mq->not_empty);
    }
    
    pthread_mutex_unlock(&mq->mutex);
    
    return result == OK ? result : ERROR;
}

int msg_q_receive(MSG_Q_ID msg_q, char* buffer, int max_n_bytes, int timeout)
{
    if (!msg_q || !buffer || max_n_bytes <= 0) {
        return ERROR;
    }
    
    msg_queue_t* mq = (msg_queue_t*)msg_q;
    
    struct timespec abs_timeout;
    if (timeout != WAIT_FOREVER) {
        struct timeval now;
        gettimeofday(&now, NULL);
        abs_timeout.tv_sec = now.tv_sec + timeout / 1000;  // Assuming timeout is in ms
        abs_timeout.tv_nsec = (now.tv_usec + (timeout % 1000) * 1000) * 1000;
        
        if (abs_timeout.tv_nsec >= 1000000000) {
            abs_timeout.tv_sec++;
            abs_timeout.tv_nsec -= 1000000000;
        }
    }
    
    pthread_mutex_lock(&mq->mutex);
    
    // Wait until queue is not empty
    int result = OK;
    if (timeout == NO_WAIT && mq->count <= 0) {
        result = ERROR;
    } else if (timeout == WAIT_FOREVER) {
        while (mq->count <= 0) {
            pthread_cond_wait(&mq->not_empty, &mq->mutex);
        }
    } else if (timeout != NO_WAIT) {
        while (mq->count <= 0) {
            if (pthread_cond_timedwait(&mq->not_empty, &mq->mutex, &abs_timeout) == ETIMEDOUT) {
                result = ERROR;
                break;
            }
        }
    } else {
        // NO_WAIT case where queue is empty
        if (mq->count <= 0) {
            result = ERROR;
        }
    }
    
    int bytes_received = 0;
    if (result == OK && mq->count > 0) {
        int remove_pos = mq->front;
        int copy_size = mq->message_sizes[remove_pos] > max_n_bytes ? max_n_bytes : mq->message_sizes[remove_pos];
        memcpy(buffer, mq->messages[remove_pos], copy_size);
        bytes_received = copy_size;
        
        mq->front = (mq->front + 1) % mq->max_msgs;
        mq->count--;
        
        // Signal that queue is not full
        pthread_cond_signal(&mq->not_full);
    }
    
    pthread_mutex_unlock(&mq->mutex);
    
    return result == OK ? bytes_received : ERROR;
}

int msg_q_delete(MSG_Q_ID msg_q)
{
    if (!msg_q) {
        return ERROR;
    }
    
    msg_queue_t* mq = (msg_queue_t*)msg_q;
    
    pthread_mutex_lock(&mq->mutex);
    
    // Free all message buffers
    for (int i = 0; i < mq->max_msgs; i++) {
        free(mq->messages[i]);
    }
    
    free(mq->messages);
    free(mq->message_sizes);
    
    pthread_mutex_unlock(&mq->mutex);
    pthread_mutex_destroy(&mq->mutex);
    pthread_cond_destroy(&mq->not_empty);
    pthread_cond_destroy(&mq->not_full);
    
    free(mq);
    
    return OK;
}