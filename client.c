#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdint.h>
#include "client.h"

channel_info_t channels[MAX_CHANNELS];
int current_channel = -1;
volatile int ui_running = 1;
int media_sockfd = -1;
pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;

// mpg123相关全局变量
int mpg123_pipefd = -1;
pid_t mpg123_pid = -1;

void dump_hex(const char* data, size_t len) {
    printf("[HEX DUMP] ");
    for (size_t i = 0; i < (len > 16 ? 16 : len); i++) {
        printf("%02X ", (unsigned char)data[i]);
    }
    if (len > 16) printf("...");
    printf("\n");
}

void parse_channel_list(char* data) {
    printf("[INIT] 解析频道列表: %s\n", data);
    char* token = strtok(data, "|");
    int i = 0;

    while (token != NULL && i < MAX_CHANNELS) {
        uint16_t chnid;
        char descr[100];
        
        if (sscanf(token, "%hu,%99[^|]", &chnid, descr) == 2) {
            channels[i].chnid = chnid;
            channels[i].descr = strdup(descr);
            printf("[频道] 加载频道 %hu: %s\n", chnid, descr);
            i++;
        } else {
            printf("[WARN] 无效频道格式: %s\n", token);
        }
        token = strtok(NULL, "|");
    }
}

// 启动mpg123进程，只启动一次
void start_mpg123_player() {
    int pipefd[2];
    if (pipe(pipefd)) {
        perror("[ERROR] 创建管道失败");
        exit(1);
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("[ERROR] fork失败");
        exit(1);
    }
    if (pid == 0) { // 子进程
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execl("/usr/bin/mpg123", "mpg123", "-q", "-", NULL);
        perror("[ERROR] execl失败");
        exit(1);
    } else { // 父进程
        close(pipefd[0]);
        mpg123_pipefd = pipefd[1];
        mpg123_pid = pid;
        printf("[PLAYER] mpg123已启动，pid=%d\n", mpg123_pid);
    }
}

// 停止mpg123进程
void stop_mpg123_player() {
    if (mpg123_pipefd >= 0) close(mpg123_pipefd);
    if (mpg123_pid > 0) waitpid(mpg123_pid, NULL, 0);
    mpg123_pipefd = -1;
    mpg123_pid = -1;
}

// 写数据到mpg123
void write_audio_to_mpg123(const char* audio_data, size_t data_len) {
    if (mpg123_pipefd < 0) return;
    printf("[DEBUG] 写入mpg123: %zu 字节\n", data_len);
    size_t total_written = 0;
    while (total_written < data_len) {
        ssize_t written = write(mpg123_pipefd, audio_data + total_written, data_len - total_written);
        if (written <= 0) break;
        total_written += written;
    }
}

void show_channel_list() {
    printf("\n=== 频道列表 ===\n");
    for (int i = 0; i < MAX_CHANNELS && channels[i].descr != NULL; i++) {
        printf("%d. %s (ID: %hu)%s\n", 
              i+1, channels[i].descr, channels[i].chnid,
              (i == current_channel) ? " [当前]" : "");
    }
    printf("================\n");
}

int init_multicast_socket(const char* mgroup, int port) {
    printf("[NET] 初始化组播套接字 %s:%d\n", mgroup, port);
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[ERROR] 创建socket失败");
        return -1;
    }

    int reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("[WARN] 设置SO_REUSEADDR失败");
    }

    struct sockaddr_in local_addr = {0};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("[ERROR] bind失败");
        close(sockfd);
        return -1;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(mgroup);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("[ERROR] 加入组播组失败");
        close(sockfd);
        return -1;
    }

    struct timeval tv = {3, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    printf("[NET] 组播套接字初始化成功\n");
    return sockfd;
}

void* ui_control_loop(void* arg) {
    printf("[UI] 控制线程启动\n");
    
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    printf("\n控制菜单:\n");
    printf("数字键1-%d - 选择频道\n", MAX_CHANNELS);
    printf("l - 显示频道列表\n");
    printf("q - 退出程序\n> ");

    while (ui_running) {
        char c = getchar();
        if (isdigit(c)) {
            int ch = c - '0' - 1; // 转换为0-based索引
            pthread_mutex_lock(&audio_mutex);
            if (ch >= 0 && ch < MAX_CHANNELS && channels[ch].descr != NULL) {
                current_channel = ch;
                printf("\n切换到频道: %s (ID: %hu)\n> ", 
                      channels[current_channel].descr, channels[current_channel].chnid);
            } else {
                printf("\n无效频道选择\n> ");
            }
            pthread_mutex_unlock(&audio_mutex);
        } else if (tolower(c) == 'l') {
            show_channel_list();
            printf("> ");
        } else if (tolower(c) == 'q') {
            ui_running = 0;
            printf("\n正在退出...\n");
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("[UI] 控制线程退出\n");
    return NULL;
}

void receive_and_play_audio() {
    printf("[AUDIO] 音频接收线程启动\n");

    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    while (ui_running) {
        packet_header_t header;
        ssize_t n = recvfrom(media_sockfd, &header, sizeof(header), MSG_WAITALL, 
                             (struct sockaddr*)&sender_addr, &sender_len);
        if (n != sizeof(header)) {
            printf("[ERROR] 接收包头失败\n");
            continue;
        }
        header.channel_id = ntohs(header.channel_id);
        header.seq_num = ntohl(header.seq_num);
        header.data_len = ntohl(header.data_len);

        if (header.data_len == 0) continue;

        // 动态分配数据缓冲区
        char *data_buf = malloc(header.data_len);
        if (!data_buf) {
            printf("[ERROR] 分配数据缓冲区失败\n");
            continue;
        }
        n = recvfrom(media_sockfd, data_buf, header.data_len, MSG_WAITALL, 
                     (struct sockaddr*)&sender_addr, &sender_len);
        if (n != header.data_len) {
            printf("[ERROR] 接收数据失败\n");
            free(data_buf);
            continue;
        }

        pthread_mutex_lock(&audio_mutex);
        int should_play = (current_channel >= 0 && 
                          header.channel_id == channels[current_channel].chnid);
        pthread_mutex_unlock(&audio_mutex);

        if (should_play) {
            // 直接写入mpg123管道，不再每包重启播放器
            write_audio_to_mpg123(data_buf, header.data_len);
        }
        free(data_buf);
    }
    printf("[AUDIO] 音频接收线程退出\n");
}

int main() {
    printf("=== 组播音频客户端 ===\n");
    
    // 初始化频道列表
    char server_data[] = "1,opear|2,traffic|3,children|4,pop";
    parse_channel_list(server_data);
    
    // 初始化网络
    media_sockfd = init_multicast_socket(DEFAULT_MGROUP, DEFAULT_PORT);
    if (media_sockfd < 0) {
        fprintf(stderr, "初始化网络失败\n");
        return 1;
    }
    
    // 启动mpg123播放器
    start_mpg123_player();
    
    // 启动UI线程
    pthread_t ui_thread;
    if (pthread_create(&ui_thread, NULL, ui_control_loop, NULL)) {
        perror("创建UI线程失败");
        close(media_sockfd);
        stop_mpg123_player();
        return 1;
    }
    
    // 主线程处理音频接收
    receive_and_play_audio();
    
    // 清理
    pthread_join(ui_thread, NULL);
    close(media_sockfd);
    stop_mpg123_player();
    
    for (int i = 0; i < MAX_CHANNELS; i++) {
        if (channels[i].descr) free(channels[i].descr);
    }
    
    printf("客户端正常退出\n");
    return 0;
}
