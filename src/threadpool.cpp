#include "threadpool.h"

threadpool_t* threadpool_create(int thread_count,int queue_size,int flags)
{
    threadpool_t* pool;
    int i;
    do
    {
        if(thread_count <= 0 || thread_count > MAX_THREADS || queue_size <= 0 || queue_size > MAX_QUEUE)
            return NULL;
        if((pool = (threadpool_t*)malloc(sizeof(threadpool_t))) == NULL)
            break;
        pool->thread_count = 0;
        pool->queue_size = queue_size;
        pool->head = pool->tail = pool->count = 0;
        pool->shutdown = pool->started = 0;

        pool->threads = (pthread_t*)malloc(sizeof(pthread_t)*thread_count);
        pool->queue = (threadpool_task_t*)malloc(sizeof(threadpool_task_t)*queue_size);

        if((pthread_mutex_init(&(pool->lock),NULL) != 0) ||
           (pthread_cond_init(&(pool->notify),NULL) != 0) ||
           (pool->threads == NULL) ||
           (pool->queue == NULL))
        {
            break;
        }
        
        for(i = 0;i<thread_count;i++)
        {
            if(pthread_create(&(pool->threads[i]),NULL,threadpool_thread,(void*)pool) != 0)
            {
                threadpool_destroy(pool,0);
                return NULL;
            }
            pool->thread_count++;
            pool->started++;
        }
        return pool;
    }while(false);

    //初始化参数环节出问题会跳到这
    if(pool != NULL)
        threadpool_free(pool);
    return NULL;
}

int threadpool_add(threadpool_t* pool,void(*function)(void*),void* arg,int flags)
{
    int err = 0;
    int next;

    if(pool == NULL || function == NULL)
    {
        return THREADPOOL_INVALID;
    }
    if(pthread_mutex_lock(&(pool->lock)) != 0)
    {
        return THREADPOOL_LOCK_FAILURE;
    }
    next = (pool->tail+1)%pool->queue_size;
    do
    {
        if(pool->count == pool->queue_size)
        {
            err = THREADPOOL_QUEUE_FULL;
            break;
        }
        if(pool->shutdown)
        {
            err = THREADPOOL_SHUTDOWN;
            break;
        }

        pool->queue[pool->tail].function = function;
        pool->queue[pool->tail].arg = arg;
        pool->tail = next;
        pool->count+=1;

        if(pthread_cond_signal(&(pool->notify)) != 0)
        {
            err = THREADPOOL_LOCK_FAILURE;
            break;
        }
    }while(false);

    if(pthread_mutex_unlock(&pool->lock) != 0)
    {
        err = THREADPOOL_LOCK_FAILURE;
    }

    return err;
}

int threadpool_destroy(threadpool_t* pool,int flags)
{
    printf("Thread pool destroy!\n");
    int i,err = 0;
    
    if(pool == NULL)
    {
        return THREADPOOL_INVALID;
    }

    if(pthread_mutex_lock(&(pool->lock)) != 0)
    {
        return THREADPOOL_LOCK_FAILURE;
    }

    do
    {
        if(pool->shutdown)
        {
            err = THREADPOOL_SHUTDOWN;
            break;
        }

        pool->shutdown = (flags & THREADPOOL_GRACEFIL)?graceful_shutdown:immediate_shutdown;

        if((pthread_cond_broadcast(&(pool->notify)) != 0) ||
           (pthread_mutex_unlock(&(pool->lock)) != 0))
           {
               err = THREADPOOL_LOCK_FAILURE;
               break;
           }

        for(i = 0;i<pool->thread_count;i++)
        {
            if(pthread_join(pool->threads[i],NULL) != 0)
            {
                err = THREADPOOL_THREAD_FAILURE;
            }
        }
    }while(false);

    if(!err)
        threadpool_free(pool);
    return err;
}

int threadpool_free(threadpool_t* pool)
{
    if(pool == NULL || pool->started > 0)
        return -1;
    
    if(pool->threads)
    {
        free(pool->threads);
        free(pool->queue);
        pthread_mutex_lock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_cond_destroy(&(pool->notify));
    }
    free(pool);
    return 0;
}

void* threadpool_thread(void* threadpool)
{
    threadpool_t* pool = (threadpool_t*)threadpool;
    threadpool_task_t task;

    while(1)
    {
        pthread_mutex_lock(&(pool->lock));

        while((pool->count == 0)&&(!pool->shutdown))
            pthread_cond_wait(&(pool->notify),&(pool->lock));

        if((pool->shutdown == immediate_shutdown) ||
          ((pool->shutdown == graceful_shutdown) && 
          (pool->count == 0)))
          {
              break;
          }

        task.function = pool->queue[pool->head].function;
        task.arg = pool->queue[pool->head].arg;
        pool->head = (pool->head+1)%pool->queue_size;
        pool->count-=1;

        pthread_mutex_unlock(&(pool->lock));

        //调用回调 
        (*(task.function))(task.arg);
    }

    --pool->started;
    pthread_mutex_unlock(&(pool->lock));
    pthread_exit(NULL);
    return(NULL);
}