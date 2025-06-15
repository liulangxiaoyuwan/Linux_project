#ifndef __CLIENT_H__
#define __CLIENT_H__

#include "mtk.h"

// 组播相关宏定义
#define MAX_CHANNELS 20
#define DEFAULT_MGROUP "226.5.2.1"
#define DEFAULT_PORT 5210

// 频道信息结构
typedef struct {
    uint16_t chnid;      // 频道ID
    char *descr;         // 频道描述
} channel_info_t;
// 数据包头部结构
typedef struct {
    uint16_t channel_id;
    uint32_t seq_num;
    uint32_t data_len;
} packet_header_t;

// 函数声明
void parse_channel_list( char* data);
void play_audio_with_mpg123(const char* audio_data, size_t data_len);
void show_channel_list(void);
int init_multicast_socket(const char* mgroup, int port);
void* ui_control_loop(void *arg);
void receive_and_play_audio(void);
void stop_audio_player(void);
void cleanup(void);

#endif /* __CLIENT_H__ */
