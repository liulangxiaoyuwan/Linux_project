#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include<string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include<sys/time.h>

typedef struct
{
    void (*fun)(void *);
    void *arg;
} Task;

typedef struct ThreadPool
{
    Task *tasks;
    int task_capcity;  // 容量
    int cur_task_size; // 当前任务量
    int front;
    int rear;

    pthread_t manager_mutex; // 管理者线程
    pthread_t *tasks_mutex;   // 工作任务线程
    int min_thread;
    int max_thread;
    int live_thread;
    int busy_thread;
    int exit_thread;
    pthread_mutex_t pool_mutex; // 线程池锁
    pthread_mutex_t busy_mutex; //busy变量锁
    pthread_mutex_t notFull;//任务队列不是空
    pthread_mutex_t notEmpty;

    int shutdown;//线程是否关闭


}ThreadPool;

//创建线程池
ThreadPool *thread_pool_init(int min_thread, int max_thread, int task_capcity);
//销毁线程池

//添加任务到线程池

//获取线程池中工作的线程数

//获取线程池中活着的线程数


#endif