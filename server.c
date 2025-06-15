#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h> 
#include <net/if.h>
#include "server.h"
#include "mtk.h"
#include <errno.h>

#define PKT_DATA_MAX 60000  // 推荐1400字节，避免分片

// 全局序列号（带互斥锁）
static pthread_mutex_t seq_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t global_seq = 0;

void handleMulticastTask(void* arg) {
    MulticastTask* task = (MulticastTask*)arg;
    packet_header_t header;

    while (1) {
        void *data_buf = malloc(PKT_DATA_MAX);
        if (!data_buf) {
            fprintf(stderr, "分配数据缓冲区失败\n");
            return;
        }
        // 只读取最多 PKT_DATA_MAX 字节
        int bytes_read = media_lib_read_data(task->chnid, data_buf, PKT_DATA_MAX);
        if (bytes_read <= 0) {
            free(data_buf);
            usleep(20000);
            continue;
        }
        if (bytes_read > PKT_DATA_MAX) {
            fprintf(stderr, "读取数据超长，丢弃\n");
            free(data_buf);
            continue;
        }

        pthread_mutex_lock(&seq_mutex);
        header.channel_id = htons(task->chnid);
        header.seq_num = htonl(global_seq++);
        header.data_len = htonl(bytes_read);
        pthread_mutex_unlock(&seq_mutex);

        size_t send_len = sizeof(header) + bytes_read;
        if (send_len > 61000) { // 保险起见再检查一次
            fprintf(stderr, "发送包过大，丢弃\n");
            free(data_buf);
            continue;
        }

        void *send_buf = malloc(send_len);
        if (!send_buf) {
            fprintf(stderr, "分配发送缓冲区失败\n");
            free(data_buf);
            return;
        }
        memcpy(send_buf, &header, sizeof(header));
        memcpy((char*)send_buf + sizeof(header), data_buf, bytes_read);

        ssize_t sent = sendto(task->sockfd, send_buf, send_len, 0,
                              (struct sockaddr*)&task->mcast_addr, sizeof(task->mcast_addr));
        if (sent < 0) {
            fprintf(stderr, "[Server] 发送失败 频道%d: %s\n", task->chnid, strerror(errno));
        } else {
            printf("[Server] 发送成功 频道%d: 序列%u 大小%zu\n",
                   task->chnid, ntohl(header.seq_num), sent);
        }
        free(send_buf);
        free(data_buf);
        usleep(5000); // 控制发送速率
    }
}

int main() {
    // 让 syslog 输出到终端
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("multicast_server", LOG_PID|LOG_CONS | LOG_PERROR, LOG_DAEMON);
    syslog(LOG_INFO, "服务器启动初始化...");
    
    if (media_lib_init() != 0) {
        syslog(LOG_ERR, "媒体库初始化失败");
        closelog();
        return -1;
    }
    
    // 2. 创建组播套接字
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        syslog(LOG_ERR, "创建套接字失败: %s", strerror(errno));
        goto cleanup;
    }

    // 设置套接字选项
    int ttl = 1;
    if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        syslog(LOG_ERR, "设置TTL失败: %s", strerror(errno));
        goto cleanup;
    }

    // 设置网络接口
    struct in_addr local_interface;
    local_interface.s_addr = INADDR_ANY;
    if (setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, &local_interface, sizeof(local_interface)) < 0) {
        syslog(LOG_WARNING, "设置组播接口失败: %s", strerror(errno));
    }

    // 3. 创建线程池
    ThreadPool* pool = threadPoolCreate(5, 10, 20);
    if (!pool) {
        syslog(LOG_ERR, "创建线程池失败");
        goto cleanup;
    }

    // 4. 获取频道列表
    mlib_list_entry *chn_list = NULL;
    int chn_count = 0;
    if (media_lib_get_chn_list(&chn_list, &chn_count) != 0) {
        syslog(LOG_ERR, "获取频道列表失败");
        goto cleanup;
    }

    // 5. 设置组播地址
    struct sockaddr_in mcast_addr = {0};
    mcast_addr.sin_family = AF_INET;
    mcast_addr.sin_port = htons(RCV_PORT);
    inet_pton(AF_INET, GROUP_IP, &mcast_addr.sin_addr);

    // 6. 添加任务
    syslog(LOG_INFO, "开始添加 %d 个频道任务", chn_count);
    for (int i = 0; i < chn_count; i++) {
        MulticastTask* task = malloc(sizeof(MulticastTask));
        if (!task) {
            syslog(LOG_ERR, "分配任务内存失败");
            continue;
        }
        
        task->chnid = chn_list[i].chnid;
        task->mcast_addr = mcast_addr;
        task->sockfd = sockfd;
        
        threadPoolAdd(pool, handleMulticastTask, task);
        usleep(10000); // 10ms间隔
    }

    // 7. 主循环
    syslog(LOG_INFO, "服务器运行中...");
    while(1) {
    sleep(1);

}

cleanup:
    // 8. 清理资源
    syslog(LOG_INFO, "服务器关闭中...");
    if (pool) threadPoolDestroy(pool);
    if (sockfd >= 0) close(sockfd);
    if (chn_list) free(chn_list);
    media_lib_deinit();
    closelog();
    return 0;
}
