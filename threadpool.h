#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include <pthread.h>
//任务结构体
typedef struct Task{
	void (*function)(void* arg);
	void* arg;
}Task;


//线程池结构体
typedef struct ThreadPool{
	//任务队列
	Task* taskQ;
	int queueCapacity; //容量
	int queueSize; //当前任务个数
	int queueFront; //队头->取数据
	int queueRear;//队尾->放数据

	pthread_t managerID;  //管理者线程ID
	pthread_t *threadIDs;  //工作线程ID
	int minNum;   //最小线程数量
	int maxNum;    //最大线程数量
	int busyNum;  //忙的线程的个数
	int liveNum; //存活的线程的个数
	int exitNum; //要销毁的线程个数
	pthread_mutex_t mutexPool;   //锁整个的线程池
	pthread_mutex_t mutexBusy;  //锁busyNum变量
	pthread_cond_t notFull;  //任务队列是不是满了
	pthread_cond_t notEmpty;  //任务队列是不是空了

	int shutdown;  //是不是要销毁线程池，销毁为1,不销毁为0
}ThreadPool;

//创建线程池并初始化
ThreadPool *threadPoolCreate(int min,int max,int queueSize);
//销毁线程池
int threadPoolDestroy(ThreadPool* pool);
//给线程池添加任务
int threadPoolAdd(ThreadPool* pool,void(*func)(void*),void* arg);
//获取线程池中工作的线程的个数
int threadPoolBusyNum(ThreadPool* pool);
//获取线程池中活着的线程的个数
int threadPoolAliveNum(ThreadPool* pool);
void *worker(void *arg);
//管理者监管增加或者销毁线程
void *manager(void *arg);
//r线程推出
void threadExit(ThreadPool* pool);
#endif
