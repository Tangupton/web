#pragma once
#include "requestData.h"
//#include "condition.hpp"
#include <pthread.h>
#include <functional>
#include <memory>
#include <vector>

const int THREADPOOL_INVALID = -1;              //对线程池进行非法操作
const int THREADPOOL_LOCK_FAILURE = -2;         //获取锁出错
const int THREADPOOL_QUEUE_FULL = -3;           //任务队列已经满了
const int THREADPOOL_SHUTDOWN = -4;
const int THREADPOOL_THREAD_FAILURE = -5;
const int THREADPOOL_GRACEFUL = 1;

const int MAX_THREADS = 1024;                   //线程池允许的最大线程数
const int MAX_QUEUE = 65535;                    //任务队列的最大值


//dafualt graceful_shutdown
typedef enum
{
    immediate_shutdown = 1,
    graceful_shutdown  = 2
} ShutDownOption;

struct ThreadPoolTask
{
    std::function<void(std::shared_ptr<void>)> fun;
    std::shared_ptr<void> args;
};

void myHandler(std::shared_ptr<void> req);  //默认执行的回调函数

class ThreadPool
{
private:
    static pthread_mutex_t lock;
    static pthread_cond_t notify;

    static std::vector<pthread_t> threads;
    static std::vector<ThreadPoolTask> queue;
    static int thread_count;                //线程池实时的线程数
    static int queue_size;                  //我们给的队列大小
    static int head;
    // tail 指向最后一个任务的下一节点
    static int tail;
    static int count;                      //记录实时任务队列数量
    static int shutdown;                   //是否关闭了
    static int started;
public:
    static int threadpool_create(int _thread_count, int _queue_size);
    static int threadpool_add(std::shared_ptr<void> args, std::function<void(std::shared_ptr<void>)> fun = myHandler);
    static int threadpool_destroy(ShutDownOption shutdown_option = graceful_shutdown);
    static int threadpool_free();
    static void *threadpool_thread(void *args);
};
