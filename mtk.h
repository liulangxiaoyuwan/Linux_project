#ifndef __MTK_H__
#define __MTK_H__

#include <stdint.h>
#include <sys/types.h>  // 包含 size_t 定义

// 媒体库路径和参数定义
#define MEDIA_LIB_PATH  "/home/xyw/Linux_project/musical"       // 媒体库根路径
#define CHN_DESCR_NAME  "descr.txt"     // 频道描述文件名
#define MIN_CHN_ID      1                // 最小频道ID
#define MAXCHN_NR       200              // 最大频道数量
#define MAX_AUDIO_FILES 1024             // 每个频道最大音频文件数

// 频道ID类型定义
typedef uint8_t chnid_t;

// 频道信息结构体
typedef struct chn_info {
    chnid_t chnid;                  // 频道ID
    char *descr;                    // 频道描述
    int audio_count;                // 音频文件数量
    int current_file_index;         // 当前播放文件索引
    long current_file_offset;       // 当前文件读取偏移量
    char *audio_files[MAX_AUDIO_FILES]; // 音频文件路径数组
} chn_info_t;

// 媒体库全局结构体
typedef struct media_lib {
    int initialized;                // 库初始化标志
    int chn_count;                  // 已加载的频道数量
    chn_info_t channels[MAXCHN_NR]; // 频道信息数组
} media_lib_t;

// 全局媒体库变量声明
extern media_lib_t g_media_lib;

// 频道列表项结构
typedef struct mlib_list_entry {
    chnid_t chnid;      // 频道ID
    char *descr;        // 频道描述
} mlib_list_entry;

// 功能接口声明
int media_lib_init(void);                // 初始化媒体库
void media_lib_deinit(void);            // 释放媒体库资源
int media_lib_get_chn_list(struct mlib_list_entry **mlib, int *nmemb);  // 获取频道列表
int media_lib_read_data(chnid_t chnid, void *buf, size_t size);         // 读取频道音频数据

#endif /* __MTK_H__ */
