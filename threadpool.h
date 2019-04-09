#ifndef THREADPOOL
#define THREADPOOL
#include "requestData.h"
#include <pthread.h>

const int THREADPOOL_INVALID = -1;      //线程池非法操作
const int THREADPOOL_LOCK_FAILURE = -2;  //获取锁出错
const int THREADPOOL_QUEUE_FULL = -3;
const int THREADPOOL_SHUTDOWN = -4;
const int THREADPOOL_THREAD_FAILURE = -5;
const int THREADPOOL_GRACEFIL = 1;

const int MAX_THREADS = 1024; //线程池允许的最大线程数
const int MAX_QUEUE = 65535;  //任务队列的最大值

//关闭的方式
typedef enum
{
    immediate_shutdown = 1,
    graceful_shutdown = 2
}threadpool_shutdown_t;

typedef struct 
{
    void (*function)(void*);
    void* arg;
}threadpool_task_t;

struct threadpool_t
{
    pthread_mutex_t lock;
    pthread_cond_t notify;
    pthread_t* threads;
    threadpool_task_t* queue;
    int thread_count;    //线程池实时的线程数
    int queue_size;
    int head;
    int tail;
    int count;          //记录实时任务队列数量
    int shutdown;
    int started;
};

threadpool_t* threadpool_create(int thread_count,int queue_size,int flags);
int threadpool_add(threadpool_t* pool,void(*function)(void*),void* arg,int flags);
int threadpool_destroy(threadpool_t* pool,int flags);
int threadpool_free(threadpool_t* pool);
void* threadpool_thread(void* threadpool); //消费者

#endif
