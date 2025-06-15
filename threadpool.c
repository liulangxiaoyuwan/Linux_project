#include <pthread.h>
#include <time.h>
#include "threadpool.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#define NUMBER 2  // 每次增减线程数
#define TASK_TIMEOUT 2  // 任务超时(秒)

ThreadPool* threadPoolCreate(int min, int max, int queueSize) {
    ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    if (!pool) {
        perror("malloc ThreadPool failed");
        return NULL;
    }

    // 初始化线程ID数组
    pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
    if (!pool->threadIDs) {
        perror("malloc threadIDs failed");
        free(pool);
        return NULL;
    }
    memset(pool->threadIDs, 0, sizeof(pthread_t) * max);

    // 初始化任务队列
    pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
    if (!pool->taskQ) {
        perror("malloc taskQ failed");
        free(pool->threadIDs);
        free(pool);
        return NULL;
    }

    // 初始化参数
    pool->minNum = min;
    pool->maxNum = max;
    pool->busyNum = 0;
    pool->liveNum = min;
    pool->exitNum = 0;
    pool->queueCapacity = queueSize;
    pool->queueSize = 0;
    pool->queueFront = 0;
    pool->queueRear = 0;
    pool->shutdown = 0;

    // 初始化锁和条件变量
    if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
        pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
        pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
        pthread_cond_init(&pool->notFull, NULL) != 0) {
        printf("mutex/cond init failed\n");
        free(pool->taskQ);
        free(pool->threadIDs);
        free(pool);
        return NULL;
    }

    // 创建管理者和工作线程
    pthread_create(&pool->managerID, NULL, manager, pool);
    for (int i = 0; i < min; i++) {
        pthread_create(&pool->threadIDs[i], NULL, worker, pool);
    }

    return pool;
}

void* worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    
    while (1) {
        pthread_mutex_lock(&pool->mutexPool);
        
        // 等待任务或关闭信号
        while (pool->queueSize == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);
            
            // 检查是否需要销毁线程
            if (pool->exitNum > 0) {
                pool->exitNum--;
                if (pool->liveNum > pool->minNum) {
                    pool->liveNum--;
                    pthread_mutex_unlock(&pool->mutexPool);
                    threadExit(pool);
                }
            }
        }
        
        // 检查线程池是否关闭
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->mutexPool);
            threadExit(pool);
        }
        
        // 取出任务
        Task task;
        task.function = pool->taskQ[pool->queueFront].function;
        task.arg = pool->taskQ[pool->queueFront].arg;
        
        // 更新队列
        pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
        pool->queueSize--;
        
        // 通知可以添加新任务
        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexPool);
        
        // 执行任务
        printf("[Thread %ld] start task\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);
        
        task.function(task.arg);
        
        // 清理资源
        if (task.arg) {
            free(task.arg);
        }
        
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusy);
        
        printf("[Thread %ld] finish task\n", pthread_self());
    }
    return NULL;
}

void* manager(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    
    while (!pool->shutdown) {
        sleep(3); // 每3秒检查一次
        
        // 获取线程池状态
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize = pool->queueSize;
        int liveNum = pool->liveNum;
        pthread_mutex_unlock(&pool->mutexPool);
        
        pthread_mutex_lock(&pool->mutexBusy);
        int busyNum = pool->busyNum;
        pthread_mutex_unlock(&pool->mutexBusy);
        
        printf("[Manager] status: %d/%d threads, %d tasks\n", 
              busyNum, liveNum, queueSize);
        
        // 动态增加线程
        if (queueSize > liveNum && liveNum < pool->maxNum) {
            pthread_mutex_lock(&pool->mutexPool);
            int add = 0;
            for (int i = 0; i < pool->maxNum && add < NUMBER; i++) {
                if (pool->threadIDs[i] == 0) {
                    pthread_create(&pool->threadIDs[i], NULL, worker, pool);
                    add++;
                    pool->liveNum++;
                    printf("[Manager] add thread %ld\n", pool->threadIDs[i]);
                }
            }
            pthread_mutex_unlock(&pool->mutexPool);
        }
        
        // 动态减少线程
        if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
            pthread_mutex_lock(&pool->mutexPool);
            pool->exitNum = NUMBER;
            pthread_mutex_unlock(&pool->mutexPool);
            
            for (int i = 0; i < NUMBER; i++) {
                pthread_cond_signal(&pool->notEmpty);
            }
            printf("[Manager] remove %d threads\n", NUMBER);
        }
    }
    return NULL;
}

void threadExit(ThreadPool* pool) {
    pthread_t tid = pthread_self();
    for (int i = 0; i < pool->maxNum; i++) {
        if (pool->threadIDs[i] == tid) {
            pool->threadIDs[i] = 0;
            printf("[Thread %ld] exiting...\n", tid);
            break;
        }
    }
    pthread_exit(NULL);
}

int threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg) {
    pthread_mutex_lock(&pool->mutexPool);
    
    // 带超时的等待
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += TASK_TIMEOUT;
    
    while (pool->queueSize == pool->queueCapacity && !pool->shutdown) {
        int err = pthread_cond_timedwait(&pool->notFull, &pool->mutexPool, &ts);
        if (err == ETIMEDOUT) {
            pthread_mutex_unlock(&pool->mutexPool);
            printf("[Pool] add task timeout\n");
            return -1;
        }
    }
    
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutexPool);
        return -1;
    }
    
    // 添加任务
    pool->taskQ[pool->queueRear].function = func;
    pool->taskQ[pool->queueRear].arg = arg;
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;
    
    pthread_cond_signal(&pool->notEmpty);
    pthread_mutex_unlock(&pool->mutexPool);
    return 0;
}

int threadPoolDestroy(ThreadPool* pool) {
    if (!pool) return -1;
    
    printf("[Pool] destroying...\n");
    pool->shutdown = 1;
    
    // 等待管理者线程
    pthread_join(pool->managerID, NULL);
    
    // 唤醒所有工作线程
    pthread_cond_broadcast(&pool->notEmpty);
    
    // 等待工作线程退出
    for (int i = 0; i < pool->maxNum; i++) {
        if (pool->threadIDs[i] != 0) {
            pthread_join(pool->threadIDs[i], NULL);
        }
    }
    
    // 释放资源
    free(pool->taskQ);
    free(pool->threadIDs);
    
    // 销毁锁和条件变量
    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);
    
    free(pool);
    printf("[Pool] destroyed\n");
    return 0;
}

int threadPoolBusyNum(ThreadPool* pool) {
    pthread_mutex_lock(&pool->mutexBusy);
    int busyNum = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool) {
    pthread_mutex_lock(&pool->mutexPool);
    int aliveNum = pool->liveNum;
    pthread_mutex_unlock(&pool->mutexPool);
    return aliveNum;
}
