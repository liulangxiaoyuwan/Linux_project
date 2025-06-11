#include "thread_pool.h"

ThreadPool *thread_pool_init(int min_thread, int max_thread, int task_capcity)
{
    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));

    do
    {
        if (pool == NULL)
        {
            printf("malloc error\n");
            break;
        }

        pool->tasks_mutex = (pthread_t *)malloc(sizeof(pthread_t));
        if (pool->tasks_mutex == NULL)
        {
            printf("malloc error\n");
            break;
        }
        memset(pool->tasks_mutex, 0, sizeof(pthread_t) * max_thread);
        pool->min_thread = min_thread;
        pool->max_thread = max_thread;
        pool->live_thread = min_thread;
        pool->busy_thread = 0;
        pool->exit_thread = 0;

        if (pthread_mutex_init(&pool->busy_mutex, NULL != NULL ||
                                                      pthread_mutex_init(&pool->pool_mutex, NULL) != NULL ||
                                                      pthread_mutex_init(&pool->notFull, NULL) != NULL ||
                                                      pthread_mutex_init(&pool->notEmpty, NULL) != NULL))
        {
            printf("mutex init error\n");
            break;
        }

        pool->task_capcity = task_capcity;
        pool->tasks = (Task *)malloc(sizeof(Task) * task_capcity);
        if (pool->tasks == NULL)
        {
            printf("malloc error\n");
            break;
        }
        pool->cur_task_size = 0;
        pool->front = 0;
        pool->rear = 0;

        pool->shutdown = 0;

        pthread_create(&pool->manager_mutex, NULL, manager_thread, NULL);
        for (int i = 0; i < min_thread; i++)
        {
            pthread_create(&pool->tasks_mutex[i], NULL, worker_thread, NULL);
        }

        return pool;
    } while (0);
    if (pool && pool->tasks_mutex)
        free(pool->tasks_mutex);
    if (pool && pool->tasks)
        free(pool->tasks);
    if (pool)
        free(pool);
    return NULL;
}

void *worker_thread(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;

    while (1)
    {
        pthread_mutex_lock(&pool->pool_mutex);
        // 等待当前任务队列是否为空
        while (pool->cur_task_size == 0 && !pool->shutdown)
        {
            // 如果为空 阻塞工作进程
            pthread_cond_wait(&pool->notEmpty, &pool->pool_mutex);
        }
        // 判断线程池是否关闭
        if (pool->shutdown)
        {
            pthread_mutex_unlock(&pool->pool_mutex);
            pthread_exit(NULL);
        }

        // 获取任务
        Task task;
        task.fun = pool->tasks[pool->front].fun;
        task.arg = pool->tasks[pool->front].arg;

        pool->front= (pool->front + 1) % pool->task_capcity;
        pool->cur_task_size--;

        pthread_mutex_unlock(&pool->pool_mutex);

        printf("Thread %ld is working on task\n", syscall(SYS_gettid));

        pthread_mutex_lock(&pool->busy_mutex);
        pool->busy_thread++;
        pthread_mutex_unlock(&pool->busy_mutex);
        task.fun(task.arg);

        free(task.arg);
        task.arg=NULL;

        printf("Thread %ld finished task\n", syscall(SYS_gettid));
        pthread_mutex_lock(&pool->busy_mutex);
        pool->busy_thread--;
        pthread_mutex_unlock(&pool->busy_mutex);

    }
}

void *manager_thread(void *arg)
{
    
}