#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <libgen.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include "mtk.h"

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

// 全局媒体库变量定义
media_lib_t g_media_lib = {
    .initialized = 0,
    .chn_count = 0};

// 内部函数声明
static int media_lib_load(const char *lib_path);
static void media_lib_free(void);
static char *media_lib_read_descr(const char *dir_path);

// 初始化媒体库
int media_lib_init()
{
    pthread_mutex_lock(&g_mutex);
    if (!g_media_lib.initialized)
    {
        if (media_lib_load(MEDIA_LIB_PATH) == 0)
        {
            g_media_lib.initialized = 1;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    return g_media_lib.initialized ? 0 : -1;
}

// 加载媒体库
static int media_lib_load(const char *lib_path)
{
    DIR *dir = opendir(lib_path);
    if (!dir)
    {
        fprintf(stderr, "opendir(%s) 失败: %s\n", lib_path, strerror(errno));
        return -1;
    }

    g_media_lib.chn_count = 0;
    struct dirent *entry;
    chnid_t chnid = MIN_CHN_ID;

    while ((entry = readdir(dir)) != NULL && chnid <= MAXCHN_NR)
    {
        // 跳过隐藏文件和目录
        if (entry->d_name[0] == '.' || g_media_lib.chn_count >= MAXCHN_NR)
            continue;

        char dir_path[PATH_MAX];
        snprintf(dir_path, sizeof(dir_path), "%s/%s", lib_path, entry->d_name);

        // 检查是否为目录
        struct stat st;
        if (stat(dir_path, &st))
        {
            fprintf(stderr, "stat(%s) 失败: %s\n", dir_path, strerror(errno));
            continue;
        }
        if (!S_ISDIR(st.st_mode))
            continue;

        // 读取描述文件
        char *descr = media_lib_read_descr(dir_path);
        if (!descr)
        {
            fprintf(stderr, "警告: %s 中没有 %s\n", dir_path, CHN_DESCR_NAME);
            continue;
        }

        // 初始化频道信息
        chn_info_t *chn = &g_media_lib.channels[g_media_lib.chn_count];
        chn->chnid = chnid++;
        chn->descr = descr;
        chn->audio_count = 0;
        chn->current_file_index = 0;
        chn->current_file_offset = 0;

        // 扫描音频文件
        DIR *audio_dir = opendir(dir_path);
        if (audio_dir)
        {
            struct dirent *audio_entry;
            while ((audio_entry = readdir(audio_dir)) != NULL)
            {
                if (chn->audio_count >= MAX_AUDIO_FILES)
                    break;

                char *dot = strrchr(audio_entry->d_name, '.');
                if (dot && strcasecmp(dot, ".mp3") == 0)
                {
                    // 分配内存并存储完整路径
                    char *audio_path = malloc(PATH_MAX);
                    if (!audio_path)
                    {
                        fprintf(stderr, "内存分配失败\n");
                        continue;
                    }

                    // 构建完整路径
                    int n = snprintf(audio_path, PATH_MAX, "%s/%s", dir_path, audio_entry->d_name);
                    if (n < 0 || n >= PATH_MAX)
                    {
                        free(audio_path);
                        continue;
                    }

                    // 添加到音频文件列表
                    chn->audio_files[chn->audio_count] = audio_path;
                    chn->audio_count++;

                    printf("添加音频文件: %s\n", audio_path);
                }
            }
            closedir(audio_dir);
        }

        // 检查是否有音频文件
        if (chn->audio_count > 0)
        {
            g_media_lib.chn_count++;
            printf("加载频道 %d: %s (%d 个音频文件)\n",
                   chn->chnid, chn->descr, chn->audio_count);
        }
        else
        {
            free(descr);
            fprintf(stderr, "警告: %s 中没有音频文件\n", dir_path);
        }
    }

    closedir(dir);
    printf("总共加载 %d 个频道\n", g_media_lib.chn_count);
    return g_media_lib.chn_count > 0 ? 0 : -1;
}

// 读取描述文件
static char *media_lib_read_descr(const char *dir_path)
{
    char descr_path[PATH_MAX];
    snprintf(descr_path, sizeof(descr_path), "%s/%s", dir_path, CHN_DESCR_NAME);

    FILE *file = fopen(descr_path, "r");
    if (!file)
    {
        fprintf(stderr, "无法打开描述文件: %s\n", descr_path);
        return NULL;
    }

    char *descr = NULL;
    size_t len = 0;
    ssize_t read;

    if ((read = getline(&descr, &len, file)) != -1)
    {
        // 去除换行符
        if (read > 0 && descr[read - 1] == '\n')
        {
            descr[read - 1] = '\0';
        }
    }
    else
    {
        free(descr);
        descr = NULL;
    }

    fclose(file);
    return descr;
}

// 释放媒体库资源
static void media_lib_free(void)
{
    for (int i = 0; i < g_media_lib.chn_count; i++)
    {
        chn_info_t *chn = &g_media_lib.channels[i];
        free(chn->descr);
        for (int j = 0; j < chn->audio_count; j++)
        {
            free(chn->audio_files[j]);
        }
    }
    g_media_lib.chn_count = 0;
}

// 释放媒体库资源
void media_lib_deinit(void)
{
    pthread_mutex_lock(&g_mutex);
    if (g_media_lib.initialized)
    {
        media_lib_free();
        g_media_lib.initialized = 0;
    }
    pthread_mutex_unlock(&g_mutex);
}

// 获取频道列表
int media_lib_get_chn_list(struct mlib_list_entry **mlib, int *nmemb)
{
    if (!g_media_lib.initialized)
    {
        if (media_lib_init() != 0)
        {
            return -1;
        }
    }

    pthread_mutex_lock(&g_mutex);
    *nmemb = g_media_lib.chn_count;
    *mlib = malloc(g_media_lib.chn_count * sizeof(struct mlib_list_entry));
    if (!*mlib)
    {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    for (int i = 0; i < g_media_lib.chn_count; i++)
    {
        chn_info_t *chn = &g_media_lib.channels[i];
        (*mlib)[i].chnid = chn->chnid;
        (*mlib)[i].descr = strdup(chn->descr);

        if (!(*mlib)[i].descr)
        {
            // 错误处理：释放已分配的内存
            for (int j = 0; j < i; j++)
            {
                free((*mlib)[j].descr);
            }
            free(*mlib);
            *mlib = NULL;
            pthread_mutex_unlock(&g_mutex);
            return -1;
        }
    }

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

// 读取频道数据
int media_lib_read_data(chnid_t chnid, void *buf, size_t size)
{
    if (!g_media_lib.initialized)
    {
        if (media_lib_init() != 0)
        {
            fprintf(stderr, "媒体库初始化失败\n");
            return -1;
        }
    }

    pthread_mutex_lock(&g_mutex);

    // 查找频道
    chn_info_t *chn = NULL;
    for (int i = 0; i < g_media_lib.chn_count; i++)
    {
        if (g_media_lib.channels[i].chnid == chnid)
        {
            chn = &g_media_lib.channels[i];
            break;
        }
    }

    if (!chn)
    {
        fprintf(stderr, "错误: 频道 %d 未找到\n", chnid);
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    if (chn->audio_count == 0)
    {
        fprintf(stderr, "错误: 频道 %d 没有音频文件\n", chnid);
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    // 获取当前音频文件路径
    const char *current_file = chn->audio_files[chn->current_file_index];
    printf("打开文件: %s (偏移量: %ld)\n", current_file, chn->current_file_offset);

    FILE *file = fopen(current_file, "rb");
    if (!file)
    {
        fprintf(stderr, "无法打开音频文件: %s (%s)\n", current_file, strerror(errno));
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    if (fseek(file, chn->current_file_offset, SEEK_SET) != 0)
    {
        fprintf(stderr, "文件定位失败: %s (%s)\n", current_file, strerror(errno));
        fclose(file);
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    // 只读取 size 字节
    size_t bytes_read = fread(buf, 1, size, file);

    if (ferror(file))
    {
        fprintf(stderr, "文件读取错误: %s (%s)\n", current_file, strerror(errno));
        fclose(file);
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    chn->current_file_offset = ftell(file);

    if (bytes_read < size || feof(file))
    {
        chn->current_file_index = (chn->current_file_index + 1) % chn->audio_count;
        chn->current_file_offset = 0;
        printf("切换到下一个文件: %s\n", chn->audio_files[chn->current_file_index]);
    }

    fclose(file);
    pthread_mutex_unlock(&g_mutex);

    printf("实际读取: %zu 字节 (请求: %zu 字节)\n", bytes_read, size);
    return bytes_read;
}
