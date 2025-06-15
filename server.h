#ifndef __SERVER_H__
#define __SERVER_H__

#include "mtk.h"
#include "threadpool.h"

// 组播相关宏定义
#define GROUP_IP  "226.5.2.1"
#define RCV_PORT  5210

// 数据包头部结构 (与客户端一致)
typedef struct {
    uint16_t channel_id; // 频道ID
    uint32_t seq_num;    // 序列号
    uint32_t data_len;   // 数据长度
} packet_header_t;

// 组播任务结构体
typedef struct {
    chnid_t chnid;
    void *buffer;
    size_t size;
    struct sockaddr_in mcast_addr;
    int sockfd;
} MulticastTask;

// 函数声明
void handleMulticastTask(void* arg);

#endif /* __SERVER_H__ */
