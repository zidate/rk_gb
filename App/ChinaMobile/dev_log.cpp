/**
 * @file dev_log.cpp
 * @brief 设备日志模块实现 - 方案C V3: 事件驱动管理线程
 *
 * 架构:
 * - 专用日志管理线程通过管道接收事件，串行处理所有文件操作
 * - 业务线程调用 dev_log_write() 时格式化+投递事件，不阻塞于磁盘IO
 * - SD卡状态变化通过 dev_log_notify_* 系列函数通知管理线程
 * - 支持日志转存(Flash→SD卡)、主动上报、异常触发上报
 *
 * 日志格式: [YYYY-MM-DD HH:MM:SS][日志类型][事件类型] 事件内容
 * 文件命名: DEV_MAC_YYYY-MM-DD-HH-MM-SS.txt (兼容旧格式 DEV_YYYY-MM-DD-HH-MM-SS.txt)
 */

#include "dev_log.h"
#include "demo_public.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>

/* ============================================================================
 * 内部常量 & 宏
 * ============================================================================ */

/* 日志类型字符串映射 */
static const char *s_logLevelStr[] = {
    "Info",     /* DEV_LOG_INFO    */
    "Warning",  /* DEV_LOG_WARNING */
    "Error",    /* DEV_LOG_ERROR   */
};

/* 事件类型字符串映射 */
static const char *s_eventTypeStr[] = {
    "System",       /* DEV_EVENT_SYSTEM     */
    "Upgrade",      /* DEV_EVENT_UPGRADE    */
    "SDCard",       /* DEV_EVENT_SD_CARD    */
    "AI",           /* DEV_EVENT_AI         */
    "Time",         /* DEV_EVENT_TIME       */
    "WebConfig",    /* DEV_EVENT_WEB_CONFIG */
    "Network",      /* DEV_EVENT_NETWORK    */
};

/* 设备日志文件名时间戳格式 */
#define DEV_LOG_FILE_TIME_FMT       "%Y-%m-%d-%H-%M-%S"
#define DEV_LOG_TIME_FMT            "%Y-%m-%d %H:%M:%S"

/* SDK日志上传文件名格式 */
#define SDK_LOG_UPLOAD_NAME_FMT     "SDK_%s_%s.txt"

/* 日志转存缓冲区大小 */
#define TRANSFER_BUF_SIZE           4096

/* ============================================================================
 * 内部状态枚举
 * ============================================================================ */

typedef enum {
    STATE_FLASH_ONLY = 0,   /* 仅Flash写入, 轮转上限20 */
    STATE_TRANSFERRING,     /* 转存中(Flash→SD), 转存完成后切到SD_ACTIVE */
    STATE_SD_ACTIVE,        /* SD卡写入, 轮转上限80 */
} devLogState_e;

/* ============================================================================
 * 内部全局变量
 * ============================================================================ */

/* 管道: [0]=读端(管理线程), [1]=写端(业务线程, 非阻塞) */
static int s_pipe_fds[2] = {-1, -1};

/* 管理线程 */
static pthread_t s_threadId = 0;
static volatile int s_running = 0;

/* 当前状态 */
static devLogState_e s_state = STATE_FLASH_ONLY;

/* 文件操作 */
static FILE *s_logFp = NULL;
static char s_logFilePath[512] = {0};
static char s_logDir[256] = {0};
static cmiot_bool_t s_inited = CMIOT_FALSE;

/* 主动上报 */
static cmiot_bool_t s_activeUpload = CMIOT_FALSE;
static dev_log_upload_callback_t s_uploadCallback = NULL;

/* MAC地址 (extern自cmiot_demo.cpp) */
extern char g_mac[CMIOT_MAX_MAC_LEN];

/* ============================================================================
 * 内部函数声明
 * ============================================================================ */

/* 基础设施 */
static void post_event(const devLogEvent_t *event);
static int  log_filter_file(const struct dirent *pDir);
static int  sdk_log_filter_file(const struct dirent *pDir);
static void log_ensure_dir(const char *dirPath);
static void log_close_file(void);
static void log_open_new_file(void);
static void log_cleanup_excess_files(const char *dirPath, int maxFiles);
static void log_switch_path(const char *newDir);
static int  log_transfer_files(void);
static int  sdk_log_transfer_files(void);

/* 时间辅助 */
static int  parse_file_time_range(const char *fullPath, const char *filename,
                                  time_t *createTime, time_t *modifyTime);
static int  parse_sdk_log_file_time(const char *filename, time_t *outTime);
static int  is_time_overlap(time_t fCreate, time_t fModify,
                            cmiot_uint32_t startTime, cmiot_uint32_t endTime);

/* 事件处理 */
static void handle_write_event(const devLogEvent_t *event);
static void handle_sd_inserted(void);
static void handle_sd_removed(void);
static void handle_sd_format_start(void);
static void handle_sd_format_done(void);
static void handle_shutdown(void);
static void handle_exception_upload(const devLogEvent_t *event);
static void do_sdk_log_upload(const char *sdkLogDir, const char *turnDir,
                              cmiot_uint32_t startTime, cmiot_uint32_t endTime);
static void do_dev_log_upload(const char *devLogDir,
                              cmiot_uint32_t startTime, cmiot_uint32_t endTime);

/* 管理线程主循环 */
static void *dev_log_thread_main(void *arg);

/* ============================================================================
 * 基础设施: 管道 & 文件过滤 & 目录 & 清理
 * ============================================================================ */

/**
 * 向管道投递事件(非阻塞)
 * pipe满时静默丢弃(正常不会触发，设备日志写入频率低)
 */
static void post_event(const devLogEvent_t *event)
{
    if (s_pipe_fds[1] < 0) return;

    ssize_t written = write(s_pipe_fds[1], event, sizeof(devLogEvent_t));
    if (written != sizeof(devLogEvent_t)) {
        /* pipe满或其他错误，静默丢弃 */
    }
}

/**
 * 文件过滤函数: 匹配 DEV_MAC_timestamp.txt 和 DEV_timestamp.txt
 * 兼容新旧两种文件命名格式
 */
static int log_filter_file(const struct dirent *pDir)
{
    struct tm tm = {0};
    char timeStr[20] = {0};
    int nameLen;
    const char *name;
    const char *timeStart;

    if (pDir == NULL || pDir->d_name == NULL) return 0;

    name = pDir->d_name;
    nameLen = strlen(name);

    /* 最小长度: DEV_ + 时间戳19字符 + .txt = 27 */
    if (nameLen < 27) return 0;

    /* 检查前缀 DEV_ */
    if (strncmp(name, "DEV_", 4) != 0) return 0;

    /* 检查后缀 .txt */
    if (strncmp(name + nameLen - 4, ".txt", 4) != 0) return 0;

    /* 时间戳在 .txt 之前, 固定19字符: YYYY-MM-DD-HH-MM-SS */
    timeStart = name + nameLen - 4 - 19;
    if (timeStart[-1] != '_') return 0;

    strncpy(timeStr, timeStart, 19);
    timeStr[19] = '\0';

    if (strptime(timeStr, DEV_LOG_FILE_TIME_FMT, &tm) != NULL) return 1;

    return 0;
}

/**
 * SDK日志文件过滤函数: 匹配 cmiot_sdk_log_YYYY-MM-DD-HH-MM-SS.txt
 * 文件名格式: cmiot_sdk_log_ + 19字符时间戳 + .txt = 37字符
 */
static int sdk_log_filter_file(const struct dirent *pDir)
{
    struct tm tm = {0};
    char timeStr[20] = {0};
    int nameLen;
    const char *name;
    const char *timeStart;

    if (pDir == NULL || pDir->d_name == NULL) return 0;

    name = pDir->d_name;
    nameLen = strlen(name);

    /* 固定长度: cmiot_sdk_log_(14) + 时间戳(19) + .txt(4) = 37 */
    if (nameLen != 37) return 0;

    /* 检查前缀 cmiot_sdk_log_ */
    if (strncmp(name, "cmiot_sdk_log_", 14) != 0) return 0;

    /* 检查后缀 .txt */
    if (strncmp(name + nameLen - 4, ".txt", 4) != 0) return 0;

    /* 时间戳在 .txt 之前, 固定19字符: YYYY-MM-DD-HH-MM-SS */
    timeStart = name + nameLen - 4 - 19;
    strncpy(timeStr, timeStart, 19);
    timeStr[19] = '\0';

    if (strptime(timeStr, DEV_LOG_FILE_TIME_FMT, &tm) != NULL) return 1;

    return 0;
}

/**
 * 从SDK日志文件名中解析创建时间
 * 文件名格式: cmiot_sdk_log_YYYY-MM-DD-HH-MM-SS.txt
 * @return 0-成功, -1-失败
 */
static int parse_sdk_log_file_time(const char *filename, time_t *outTime)
{
    struct tm tm = {0};
    char timeStr[20] = {0};
    int nameLen;

    if (filename == NULL || outTime == NULL) return -1;

    nameLen = strlen(filename);
    if (nameLen != 37) return -1;
    if (strncmp(filename, "cmiot_sdk_log_", 14) != 0) return -1;

    /* 时间戳在 .txt 之前, 固定19字符 */
    const char *timeStart = filename + nameLen - 4 - 19;
    strncpy(timeStr, timeStart, 19);
    timeStr[19] = '\0';

    if (strptime(timeStr, DEV_LOG_FILE_TIME_FMT, &tm) != NULL) {
        *outTime = mktime(&tm);
        return 0;
    }
    return -1;
}

static void log_ensure_dir(const char *dirPath)
{
    if (access(dirPath, F_OK) == 0) return;

    /* 递归创建父目录(因mkdir只创建叶子节点,父目录不存在时会失败) */
    char tmp[256];
    strncpy(tmp, dirPath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (access(tmp, F_OK) == -1) mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

static void log_close_file(void)
{
    if (s_logFp) {
        fflush(s_logFp);
        fclose(s_logFp);
        s_logFp = NULL;
    }
    s_logFilePath[0] = '\0';
}

/**
 * 打开新的日志文件
 * 文件命名: DEV_MAC_YYYY-MM-DD-HH-MM-SS.txt
 */
static void log_open_new_file(void)
{
    struct timeval tv = {0};
    struct tm tm = {0};
    char dateTime[32] = {0};

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    strftime(dateTime, sizeof(dateTime), DEV_LOG_FILE_TIME_FMT, &tm);

    snprintf(s_logFilePath, sizeof(s_logFilePath), "%s/DEV_%s_%s.txt",
             s_logDir, g_mac, dateTime);

    s_logFp = fopen(s_logFilePath, "a");
    if (s_logFp == NULL) {
        printf("[dev_log] ERROR: failed to open log file: %s (errno=%d, %s)\n",
               s_logFilePath, errno, strerror(errno));
        return;
    }

    /* 设置行缓冲, 每条日志自动flush */
    setvbuf(s_logFp, NULL, _IOLBF, 0);

    printf("[dev_log] opened new log file: %s\n", s_logFilePath);
}

/**
 * 清理超出数量限制的日志文件(删除最旧的文件)
 */
static void log_cleanup_excess_files(const char *dirPath, int maxFiles)
{
    struct dirent **files = NULL;
    int fileNum;
    int i;

    if (maxFiles <= 0) return;

    fileNum = scandir(dirPath, &files, log_filter_file, alphasort);
    if (fileNum < 0 || fileNum <= maxFiles) {
        if (files && fileNum > 0) {
            for (i = 0; i < fileNum; i++) free(files[i]);
            free(files);
        }
        return;
    }

    /* 按字母排序后, 最旧的文件在前面, 删除最旧的 */
    int deleteCount = fileNum - maxFiles;
    for (i = 0; i < deleteCount; i++) {
        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, files[i]->d_name);
        if (remove(fullPath) == 0) {
            printf("[dev_log] cleanup old log file: %s\n", fullPath);
        }
    }

    /* 释放scandir分配的内存 */
    for (i = 0; i < fileNum; i++) free(files[i]);
    free(files);
}

/**
 * 切换日志存储路径
 */
static void log_switch_path(const char *newDir)
{
    if (newDir == NULL || strlen(newDir) == 0) return;
    if (strcmp(s_logDir, newDir) == 0) return;

    printf("[dev_log] switching log path: %s -> %s\n", s_logDir, newDir);

    log_close_file();

    strncpy(s_logDir, newDir, sizeof(s_logDir) - 1);
    s_logDir[sizeof(s_logDir) - 1] = '\0';

    log_ensure_dir(s_logDir);
}

/* ============================================================================
 * 时间辅助函数
 * ============================================================================ */

/**
 * 解析文件的时间范围
 * 创建时间从文件名解析，修改时间取st_mtime
 */
static int parse_file_time_range(const char *fullPath, const char *filename,
                                  time_t *createTime, time_t *modifyTime)
{
    struct stat buf = {0};
    struct tm tm = {0};

    *createTime = 0;
    *modifyTime = 0;

    /* 获取修改时间 */
    if (stat(fullPath, &buf) == 0) {
        *modifyTime = buf.st_mtime;
    }

    /* 从文件名解析创建时间: 时间戳固定在后缀 .txt 之前19字符 */
    {
        int nameLen = strlen(filename);
        const char *timeStart;
        char timeStr[20] = {0};

        if (nameLen >= 27 &&
            strncmp(filename, "DEV_", 4) == 0 &&
            strncmp(filename + nameLen - 4, ".txt", 4) == 0) {

            timeStart = filename + nameLen - 4 - 19;
            if (timeStart[-1] == '_') {
                strncpy(timeStr, timeStart, 19);
                timeStr[19] = '\0';
                if (strptime(timeStr, DEV_LOG_FILE_TIME_FMT, &tm) != NULL) {
                    *createTime = mktime(&tm);
                    return 0;
                }
            }
        }
    }

    return -1;
}

/**
 * 判断两个时间区间是否有交集
 * [fCreate, fModify] 与 [startTime, endTime] 有交集
 */
static int is_time_overlap(time_t fCreate, time_t fModify,
                            cmiot_uint32_t startTime, cmiot_uint32_t endTime)
{
    return (fCreate <= (time_t)endTime && fModify >= (time_t)startTime);
}

/* ============================================================================
 * 日志转存: Flash → SD卡
 * 逐文件搬运(读+写+unlink), 可中断(检查s_running)
 * ============================================================================ */

static int log_transfer_files(void)
{
    struct dirent **files = NULL;
    int fileNum;
    int i;
    const char *srcDir = DEV_LOG_DEFAULT_DIR;
    const char *dstDir = DEV_LOG_SD_DIR;

    log_ensure_dir(dstDir);

    fileNum = scandir(srcDir, &files, log_filter_file, alphasort);
    if (fileNum <= 0) {
        if (files) free(files);
        return 0;  /* 无文件需要转存 */
    }

    for (i = 0; i < fileNum; i++) {
        /* 检查是否还在运行/中断 */
        if (!s_running) {
            int j;
            for (j = i; j < fileNum; j++) free(files[j]);
            free(files);
            return -1;
        }

        char srcPath[512], dstPath[512];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", srcDir, files[i]->d_name);
        snprintf(dstPath, sizeof(dstPath), "%s/%s", dstDir, files[i]->d_name);

        /* 如果目标已存在，跳过并删除源文件 */
        if (access(dstPath, F_OK) == 0) {
            printf("[dev_log] transfer: skip existing file %s\n", files[i]->d_name);
            remove(srcPath);
            free(files[i]);
            continue;
        }

        /* 跨文件系统不能用rename()，使用复制+删除 */
        FILE *srcFp = fopen(srcPath, "rb");
        if (!srcFp) {
            printf("[dev_log] transfer: cannot open src %s\n", srcPath);
            free(files[i]);
            continue;
        }

        FILE *dstFp = fopen(dstPath, "wb");
        if (!dstFp) {
            printf("[dev_log] transfer: cannot open dst %s\n", dstPath);
            fclose(srcFp);
            free(files[i]);
            continue;
        }

        char buf[TRANSFER_BUF_SIZE];
        size_t nread;
        while ((nread = fread(buf, 1, sizeof(buf), srcFp)) > 0) {
            fwrite(buf, 1, nread, dstFp);
        }

        fclose(srcFp);
        fclose(dstFp);

        /* 删除源文件 (MOVE语义) */
        if (remove(srcPath) == 0) {
            printf("[dev_log] transferred: %s -> %s\n", files[i]->d_name, dstDir);
        }

        free(files[i]);
    }

    free(files);

    /* 清理SD路径超出80个的文件 */
    log_cleanup_excess_files(dstDir, DEV_LOG_SD_ROTATE);

    return 0;
}

/* ============================================================================
 * SDK日志转存: Flash → SD卡
 * 仅搬运 cmiot_sdk_log_YYYY-MM-DD-HH-MM-SS.txt 格式的文件
 * 可中断(检查s_running)
 * ============================================================================ */

static int sdk_log_transfer_files(void)
{
    struct dirent **files = NULL;
    int fileNum;
    int i;
    const char *srcDir = SDK_LOG_DEFAULT_DIR;
    const char *dstDir = SDK_LOG_SD_DIR;

    log_ensure_dir(dstDir);

    fileNum = scandir(srcDir, &files, sdk_log_filter_file, alphasort);
    if (fileNum <= 0) {
        if (files) free(files);
        return 0;  /* 无文件需要转存 */
    }

    for (i = 0; i < fileNum; i++) {
        /* 检查是否还在运行/中断 */
        if (!s_running) {
            int j;
            for (j = i; j < fileNum; j++) free(files[j]);
            free(files);
            return -1;
        }

        char srcPath[512], dstPath[512];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", srcDir, files[i]->d_name);
        snprintf(dstPath, sizeof(dstPath), "%s/%s", dstDir, files[i]->d_name);

        /* 如果目标已存在，跳过并删除源文件 */
        if (access(dstPath, F_OK) == 0) {
            printf("[dev_log] sdk transfer: skip existing file %s\n", files[i]->d_name);
            remove(srcPath);
            free(files[i]);
            continue;
        }

        /* 跨文件系统不能用rename()，使用复制+删除 */
        FILE *srcFp = fopen(srcPath, "rb");
        if (!srcFp) {
            printf("[dev_log] sdk transfer: cannot open src %s\n", srcPath);
            free(files[i]);
            continue;
        }

        FILE *dstFp = fopen(dstPath, "wb");
        if (!dstFp) {
            printf("[dev_log] sdk transfer: cannot open dst %s\n", dstPath);
            fclose(srcFp);
            free(files[i]);
            continue;
        }

        char buf[TRANSFER_BUF_SIZE];
        size_t nread;
        while ((nread = fread(buf, 1, sizeof(buf), srcFp)) > 0) {
            fwrite(buf, 1, nread, dstFp);
        }

        fclose(srcFp);
        fclose(dstFp);

        /* 删除源文件 (MOVE语义) */
        if (remove(srcPath) == 0) {
            printf("[dev_log] sdk transferred: %s -> %s\n", files[i]->d_name, dstDir);
        }

        free(files[i]);
    }

    free(files);
    return 0;
}

/* ============================================================================
 * 事件处理函数
 * ============================================================================ */

/**
 * 处理日志写入事件
 * 格式化→检查本条写入后是否≥预设大小→是则上报旧文件+另起新文件→写入+fflush
 * 确保单个日志文件绝对不超过 DEV_LOG_MAX_FILE_SIZE
 */
static void handle_write_event(const devLogEvent_t *event)
{
    struct timeval tv = {0};
    struct tm tm = {0};
    char dateTime[32] = {0};
    char logLine[DEV_LOG_MAX_LINE_LEN] = {0};
    int maxFiles;
    long fileSize = 0;
    long lineLen = 0;
    int needRotate = 0;

    if (!s_inited) return;

    /* 1. 格式化时间 */
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    strftime(dateTime, sizeof(dateTime), DEV_LOG_TIME_FMT, &tm);

    /* 获取级别和事件类型字符串 */
    const char *levelStr = (event->data.write.level >= 0 &&
                            event->data.write.level < (int)(sizeof(s_logLevelStr)/sizeof(s_logLevelStr[0])))
                           ? s_logLevelStr[event->data.write.level] : "Unknown";
    const char *eventStr = (event->data.write.eventType >= 0 &&
                            event->data.write.eventType < (int)(sizeof(s_eventTypeStr)/sizeof(s_eventTypeStr[0])))
                           ? s_eventTypeStr[event->data.write.eventType] : "Unknown";

    /* 组装日志行: [时间][日志类型][事件类型] 事件内容 */
    snprintf(logLine, sizeof(logLine), "[%s][%s][%s] %s\n",
             dateTime, levelStr, eventStr, event->data.write.content);
    lineLen = (long)strlen(logLine);

    /* 2. 检查本条写入后是否会达到/超出预设大小 */
    if (s_logFp == NULL) {
        needRotate = 1;
    } else {
        fseek(s_logFp, 0, SEEK_END);
        fileSize = ftell(s_logFp);
        if (fileSize + lineLen >= DEV_LOG_MAX_FILE_SIZE) {
            needRotate = 1;
        }
        /* ftell后文件指针已在末尾，后续fputs可直接追加 */
    }

    /* 3. 需要轮转: 上报旧文件 + 另起新文件 */
    if (needRotate) {
        char uploadedPath[512];
        int doUpload = 0;

        /* 当前文件已有内容且主动上报打开：记录路径用于上报 */
        if (s_logFp && fileSize > 0 && s_activeUpload && s_uploadCallback) {
            strncpy(uploadedPath, s_logFilePath, sizeof(uploadedPath) - 1);
            uploadedPath[sizeof(uploadedPath) - 1] = '\0';
            doUpload = 1;
        }

        log_close_file();

        /* 正常主动上报: 上报旧文件的真实大小 */
        if (doUpload) {
            s_uploadCallback(uploadedPath, fileSize);
        }

        /* 选定轮转上限 */
        maxFiles = (s_state == STATE_SD_ACTIVE) ? DEV_LOG_SD_ROTATE : DEV_LOG_DEFAULT_ROTATE;

        /* 先开新文件再清理, 新文件纳入计数, 确保恰好等于上限 */
        log_open_new_file();
        log_cleanup_excess_files(s_logDir, maxFiles);

        if (s_logFp == NULL) return;
    }

    /* 4. 写入本条日志到（新）文件 */
    if (s_logFp) {
        fputs(logLine, s_logFp);
        fflush(s_logFp);  /* 立即刷盘, 防止掉电丢失 */
    }
}

/**
 * SD卡插入: 转存Flash→SD, 切换路径(设备日志+SDK日志)
 */
static void handle_sd_inserted(void)
{
    printf("[dev_log] SD card inserted, state=%d\n", s_state);

    if (s_state == STATE_SD_ACTIVE) return;  /* 已经在SD_ACTIVE, 幂等 */

    /* 确保SD卡目录存在 */
    log_ensure_dir(DEV_LOG_SD_DIR);
    log_ensure_dir(SDK_LOG_SD_DIR);

    /* 转存Flash上的日志到SD卡 */
    s_state = STATE_TRANSFERRING;

    int devRet = log_transfer_files();
#ifdef DEV_LOG_SDK_MIGRATION_ENABLE
    int sdkRet = sdk_log_transfer_files();
#endif

    if (!s_running) return;  /* 关机中，不继续 */

    /* ---- 设备日志切换路径到SD卡 ---- */
    log_close_file();
    strncpy(s_logDir, DEV_LOG_SD_DIR, sizeof(s_logDir) - 1);
    s_logDir[sizeof(s_logDir) - 1] = '\0';
    log_ensure_dir(s_logDir);
    log_open_new_file();
    log_cleanup_excess_files(s_logDir, DEV_LOG_SD_ROTATE);

    /* ---- SDK日志切换路径到SD卡 ---- */
    cmiot_set_log_config(SDK_LOG_SD_DIR, 0, SDK_LOG_SD_ROTATE);

    s_state = STATE_SD_ACTIVE;

#ifdef DEV_LOG_SDK_MIGRATION_ENABLE
    printf("[dev_log] dev transfer %s, sdk transfer %s, switched to SD path\n",
           devRet == 0 ? "complete" : "interrupted",
           sdkRet == 0 ? "complete" : "interrupted");
#else
    printf("[dev_log] dev transfer %s, sdk migration SKIPPED, switched to SD path\n",
           devRet == 0 ? "complete" : "interrupted");
#endif
}

/**
 * SD卡拔出/异常: 切回Flash路径(设备日志+SDK日志, 幂等)
 */
static void handle_sd_removed(void)
{
    printf("[dev_log] SD card removed, state=%d\n", s_state);

    if (s_state == STATE_FLASH_ONLY) return;  /* 已经在FLASH_ONLY, 幂等 */

    /* ---- 设备日志切回Flash ---- */
    log_close_file();
    strncpy(s_logDir, DEV_LOG_DEFAULT_DIR, sizeof(s_logDir) - 1);
    s_logDir[sizeof(s_logDir) - 1] = '\0';
    log_ensure_dir(s_logDir);
    log_open_new_file();
    log_cleanup_excess_files(s_logDir, DEV_LOG_DEFAULT_ROTATE);

    /* ---- SDK日志切回Flash ---- */
    log_ensure_dir(SDK_LOG_DEFAULT_DIR);
    cmiot_set_log_config(SDK_LOG_DEFAULT_DIR, 0, SDK_LOG_DEFAULT_ROTATE);

    s_state = STATE_FLASH_ONLY;
}

/**
 * SD卡格式化开始: 提前切回Flash路径(设备日志+SDK日志)
 */
static void handle_sd_format_start(void)
{
    printf("[dev_log] SD format start, state=%d\n", s_state);

    if (s_state == STATE_FLASH_ONLY) return;  /* 已经在Flash写入, 幂等 */

    /* ---- 设备日志切回Flash ---- */
    log_close_file();
    strncpy(s_logDir, DEV_LOG_DEFAULT_DIR, sizeof(s_logDir) - 1);
    s_logDir[sizeof(s_logDir) - 1] = '\0';
    log_ensure_dir(s_logDir);
    log_open_new_file();
    log_cleanup_excess_files(s_logDir, DEV_LOG_DEFAULT_ROTATE);

    /* ---- SDK日志切回Flash ---- */
    log_ensure_dir(SDK_LOG_DEFAULT_DIR);
    cmiot_set_log_config(SDK_LOG_DEFAULT_DIR, 0, SDK_LOG_DEFAULT_ROTATE);

    s_state = STATE_FLASH_ONLY;
}

/**
 * SD卡格式化完成: 若SD正常则转存+切回SD
 * 实际转存由后续SD_INSERTED事件触发
 */
static void handle_sd_format_done(void)
{
    printf("[dev_log] SD format done, state=%d\n", s_state);
    /* 格式化完成后状态机和文件都已就绪,
     * 后续SD_INSERTED事件会触发转存并切回SD路径 */
}

/**
 * 系统关机: 关闭文件, 退出循环
 */
static void handle_shutdown(void)
{
    printf("[dev_log] shutdown\n");
    log_close_file();
    s_inited = CMIOT_FALSE;
}

/* ============================================================================
 * 异常上报
 * ============================================================================ */

/**
 * SDK日志异常上报
 * 扫描sdkLogDir→筛选时间→复制到turn→重命名为SDK_MAC_timestamp.txt→上报→删除
 */
static void do_sdk_log_upload(const char *sdkLogDir, const char *turnDir,
                               cmiot_uint32_t startTime, cmiot_uint32_t endTime)
{
    struct dirent **files = NULL;
    int fileNum;
    int i;

    if (!s_uploadCallback) return;

    /* 检查目录是否存在 */
    if (access(sdkLogDir, F_OK) != 0) {
        printf("[dev_log] sdk log dir not exist: %s\n", sdkLogDir);
        return;
    }

    log_ensure_dir(turnDir);

    fileNum = scandir(sdkLogDir, &files, sdk_log_filter_file, alphasort);
    if (fileNum <= 0) {
        if (files) free(files);
        return;
    }

    for (i = 0; i < fileNum; i++) {
        if (!s_running) break;

        char srcPath[512];
        snprintf(srcPath, sizeof(srcPath), "%s/%s", sdkLogDir, files[i]->d_name);

        /* 从文件名解析精确创建时间: cmiot_sdk_log_YYYY-MM-DD-HH-MM-SS.txt */
        time_t fCreate = 0;
        if (parse_sdk_log_file_time(files[i]->d_name, &fCreate) != 0) {
            free(files[i]);
            continue;
        }

        /* 时间筛选: 文件创建时间需在 [startTime, endTime] 范围内 */
        if (fCreate > (time_t)endTime || fCreate < (time_t)startTime) {
            free(files[i]);
            continue;
        }

        /* SDK日志保留原始文件名中的时间戳作为上传文件名的一部分 */
        char timeStr[32] = {0};
        struct tm tm = {0};
        localtime_r(&fCreate, &tm);
        strftime(timeStr, sizeof(timeStr), DEV_LOG_FILE_TIME_FMT, &tm);

        char newName[256];
        snprintf(newName, sizeof(newName), "SDK_%s_%s.txt", g_mac, timeStr);

        char turnPath[512];
        snprintf(turnPath, sizeof(turnPath), "%s/%s", turnDir, newName);

        /* 复制到turn目录 */
        FILE *srcFp = fopen(srcPath, "rb");
        if (!srcFp) { free(files[i]); continue; }

        FILE *dstFp = fopen(turnPath, "wb");
        if (!dstFp) { fclose(srcFp); free(files[i]); continue; }

        char chunk[TRANSFER_BUF_SIZE];
        size_t nread;
        while ((nread = fread(chunk, 1, sizeof(chunk), srcFp)) > 0) {
            fwrite(chunk, 1, nread, dstFp);
        }
        fclose(srcFp);
        fclose(dstFp);

        /* 获取文件大小并上报 */
        struct stat turnStat = {0};
        cmiot_uint64_t fileSize = 0;
        if (stat(turnPath, &turnStat) == 0) {
            fileSize = turnStat.st_size;
        }

        printf("[dev_log] exception upload SDK log: %s, size=%llu\n", turnPath, fileSize);
        s_uploadCallback(turnPath, fileSize);

        /* 上传后删除turn目录下的文件 */
        remove(turnPath);

        free(files[i]);
    }

    free(files);
}

/**
 * 设备日志异常上报
 * 扫描devLogDir→筛选时间→直接上报不删除(即使不满1MB)
 */
static void do_dev_log_upload(const char *devLogDir,
                               cmiot_uint32_t startTime, cmiot_uint32_t endTime)
{
    struct dirent **files = NULL;
    int fileNum;
    int i;

    if (!s_uploadCallback) return;

    if (access(devLogDir, F_OK) != 0) {
        printf("[dev_log] dev log dir not exist: %s\n", devLogDir);
        return;
    }

    fileNum = scandir(devLogDir, &files, log_filter_file, alphasort);
    if (fileNum <= 0) {
        if (files) free(files);
        return;
    }

    for (i = 0; i < fileNum; i++) {
        if (!s_running) break;

        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", devLogDir, files[i]->d_name);

        time_t fCreate = 0, fModify = 0;
        if (parse_file_time_range(fullPath, files[i]->d_name, &fCreate, &fModify) != 0) {
            free(files[i]);
            continue;
        }

        /* 时间筛选 */
        if (!is_time_overlap(fCreate, fModify, startTime, endTime)) {
            free(files[i]);
            continue;
        }

        struct stat buf = {0};
        cmiot_uint64_t fileSize = 0;
        if (stat(fullPath, &buf) == 0) {
            fileSize = buf.st_size;
        }

        printf("[dev_log] exception upload dev log: %s, size=%llu\n", fullPath, fileSize);
        /* 直接上传原文件, 不复制不删除 */
        s_uploadCallback(fullPath, fileSize);

        free(files[i]);
    }

    free(files);
}

/**
 * 异常触发上报总入口
 * SDK日志→复制到turn目录+重命名+上传+删除
 * 设备日志→直接上传不删除(即使不满1MB)
 */
static void handle_exception_upload(const devLogEvent_t *event)
{
    const char *sdkLogDir;
    const char *turnDir;
    const char *devLogDir;
    cmiot_uint32_t startTime = event->data.exceptionUpload.startTime;
    cmiot_uint32_t endTime = event->data.exceptionUpload.endTime;

    /* 设计要求的触发条件: 主动上报开关必须打开 */
    if (!s_activeUpload || !s_uploadCallback) {
        printf("[dev_log] exception upload skipped: activeUpload=%d, callback=%p\n",
               s_activeUpload, (void*)s_uploadCallback);
        return;
    }

    printf("[dev_log] exception upload triggered, type=%d, startTime=%u, endTime=%u\n",
           event->data.exceptionUpload.exceptionType, startTime, endTime);

    /* 根据当前SD卡状态确定操作路径 */
    if (s_state == STATE_SD_ACTIVE) {
        sdkLogDir = SDK_LOG_SD_DIR;
        turnDir   = DEV_LOG_TURN_DIR_SD;
        devLogDir = DEV_LOG_SD_DIR;
    } else {
        sdkLogDir = SDK_LOG_DEFAULT_DIR;
        turnDir   = DEV_LOG_TURN_DIR_FLASH;
        devLogDir = DEV_LOG_DEFAULT_DIR;
    }

    /* 1. SDK日志: 复制到turn→重命名→上传→删除 */
    do_sdk_log_upload(sdkLogDir, turnDir, startTime, endTime);

    /* 2. 设备日志: 直接上传不删除 */
    do_dev_log_upload(devLogDir, startTime, endTime);
}

/* ============================================================================
 * 管理线程主循环
 * ============================================================================ */

static void *dev_log_thread_main(void *arg)
{
    devLogEvent_t event;

    printf("[dev_log] management thread started\n");

    while (s_running) {
        ssize_t n = read(s_pipe_fds[0], &event, sizeof(event));
        if (n != sizeof(event)) {
            if (n < 0 && errno == EINTR) continue;  /* 被信号中断，继续 */
            if (n < 0) {
                usleep(10000);  /* 读错误，短暂休眠 */
                continue;
            }
            continue;  /* 不完整读取，跳过 */
        }

        switch (event.type) {
        case DEV_LOG_EVENT_WRITE:
            handle_write_event(&event);
            break;
        case DEV_LOG_EVENT_SD_INSERTED:
            handle_sd_inserted();
            break;
        case DEV_LOG_EVENT_SD_REMOVED:
            handle_sd_removed();
            break;
        case DEV_LOG_EVENT_SD_FORMAT_START:
            handle_sd_format_start();
            break;
        case DEV_LOG_EVENT_SD_FORMAT_DONE:
            handle_sd_format_done();
            break;
        case DEV_LOG_EVENT_ACTIVE_UPLOAD_ON:
            s_activeUpload = CMIOT_TRUE;
            printf("[dev_log] active upload enabled\n");
            break;
        case DEV_LOG_EVENT_ACTIVE_UPLOAD_OFF:
            s_activeUpload = CMIOT_FALSE;
            printf("[dev_log] active upload disabled\n");
            break;
        case DEV_LOG_EVENT_EXCEPTION_UPLOAD:
            handle_exception_upload(&event);
            break;
        case DEV_LOG_EVENT_SHUTDOWN:
            handle_shutdown();
            return NULL;
        default:
            break;
        }
    }

    printf("[dev_log] management thread exited\n");
    return NULL;
}

/* ============================================================================
 * 公共接口实现
 * ============================================================================ */

/**
 * 初始化设备日志模块
 * 创建管道(写端非阻塞)、创建管理线程、写入启动日志
 */
cmiot_int32_t dev_log_init(void)
{
    if (s_inited) {
        printf("[dev_log] already initialized\n");
        return 0;
    }

    /* 创建管道 */
    if (pipe(s_pipe_fds) != 0) {
        printf("[dev_log] ERROR: failed to create pipe (errno=%d)\n", errno);
        return -1;
    }

    /* 设置写端为非阻塞 */
    int flags = fcntl(s_pipe_fds[1], F_GETFL, 0);
    fcntl(s_pipe_fds[1], F_SETFL, flags | O_NONBLOCK);

    /* 初始化默认路径(Flash) */
    strncpy(s_logDir, DEV_LOG_DEFAULT_DIR, sizeof(s_logDir) - 1);
    s_logDir[sizeof(s_logDir) - 1] = '\0';
    printf("\033[1;36m dev_log_init: default log dir: %s\033[0m\n", s_logDir);

    log_ensure_dir(s_logDir);
    log_open_new_file();
    log_cleanup_excess_files(s_logDir, DEV_LOG_DEFAULT_ROTATE);

    s_state = STATE_FLASH_ONLY;
    s_activeUpload = CMIOT_FALSE;
    s_inited = CMIOT_TRUE;
    s_running = 1;

    /* 创建管理线程 */
    if (pthread_create(&s_threadId, NULL, dev_log_thread_main, NULL) != 0) {
        printf("[dev_log] ERROR: failed to create management thread\n");
        close(s_pipe_fds[0]);
        close(s_pipe_fds[1]);
        s_pipe_fds[0] = s_pipe_fds[1] = -1;
        s_inited = CMIOT_FALSE;
        s_running = 0;
        return -1;
    }

    /* 写入设备启动日志 */
    dev_log_write(DEV_LOG_INFO, DEV_EVENT_SYSTEM, "device start!");

    printf("[dev_log] initialized, log dir: %s\n", s_logDir);
    return 0;
}

/**
 * 反初始化设备日志模块
 * 发送SHUTDOWN事件→等待管理线程退出→关闭管道
 */
void dev_log_deinit(void)
{
    if (!s_inited) return;

    /* 发送SHUTDOWN事件 */
    devLogEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = DEV_LOG_EVENT_SHUTDOWN;
    post_event(&event);

    /* 停止标志 + 关闭写端(若SHUTDOWN未被消费则解除read阻塞) */
    s_running = 0;
    if (s_pipe_fds[1] >= 0) { close(s_pipe_fds[1]); s_pipe_fds[1] = -1; }

    /* 等待管理线程退出 */
    if (s_threadId) {
        pthread_join(s_threadId, NULL);
        s_threadId = 0;
    }

    /* 关闭读端 */
    if (s_pipe_fds[0] >= 0) { close(s_pipe_fds[0]); s_pipe_fds[0] = -1; }

    s_inited = CMIOT_FALSE;
    printf("[dev_log] deinitialized\n");
}

/**
 * 写入一条设备事件日志
 * 线程安全: 格式化后投递到管理线程pipe, 不阻塞于磁盘IO
 */
void dev_log_write(devLogLevel_e level, devEventType_e eventType, const char *format, ...)
{
    devLogEvent_t event;
    va_list args;

    if (!s_inited || format == NULL) return;

    memset(&event, 0, sizeof(event));
    event.type = DEV_LOG_EVENT_WRITE;
    event.data.write.level = level;
    event.data.write.eventType = eventType;

    /* 在调用者线程中格式化, 减轻管理线程负担 */
    va_start(args, format);
    vsnprintf(event.data.write.content, sizeof(event.data.write.content), format, args);
    va_end(args);

    post_event(&event);
}

/* ---- SD卡状态通知 ---- */

void dev_log_notify_sd_inserted(void)
{
    if (!s_inited) return;
    devLogEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = DEV_LOG_EVENT_SD_INSERTED;
    post_event(&event);
}

void dev_log_notify_sd_removed(void)
{
    if (!s_inited) return;
    devLogEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = DEV_LOG_EVENT_SD_REMOVED;
    post_event(&event);
}

void dev_log_notify_sd_format_start(void)
{
    if (!s_inited) return;
    devLogEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = DEV_LOG_EVENT_SD_FORMAT_START;
    post_event(&event);
}

void dev_log_notify_sd_format_done(void)
{
    if (!s_inited) return;
    devLogEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = DEV_LOG_EVENT_SD_FORMAT_DONE;
    post_event(&event);
}

/* ---- 主动上报 ---- */

void dev_log_notify_active_upload(cmiot_bool_t enable)
{
    if (!s_inited) return;
    devLogEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = enable ? DEV_LOG_EVENT_ACTIVE_UPLOAD_ON : DEV_LOG_EVENT_ACTIVE_UPLOAD_OFF;
    post_event(&event);
}

cmiot_bool_t dev_log_get_active_upload(void)
{
    return s_activeUpload;
}

void dev_log_register_upload_callback(dev_log_upload_callback_t cb)
{
    s_uploadCallback = cb;
}

/* ---- 异常触发上报 ---- */

void dev_log_trigger_exception_upload(devExceptionType_e exceptionType,
                                       cmiot_uint32_t startTime,
                                       cmiot_uint32_t endTime)
{
    if (!s_inited) return;
    devLogEvent_t event;
    memset(&event, 0, sizeof(event));
    event.type = DEV_LOG_EVENT_EXCEPTION_UPLOAD;
    event.data.exceptionUpload.exceptionType = exceptionType;
    event.data.exceptionUpload.startTime = startTime;
    event.data.exceptionUpload.endTime = endTime;
    post_event(&event);
}

/* ---- 被动上报 ---- */

cmiot_int32_t dev_log_get_report_info(cmiot_uint32_t startTime, cmiot_uint32_t endTime,
                                       cmiotLogReportInfo_t *outLogInfo)
{
    struct dirent **files = NULL;
    int fileNum;
    int i;
    cmiot_int32_t ret = CMIOT_RETURN_CODE_SUCCESS;

    if (outLogInfo == NULL) return CMIOT_RETURN_CODE_FAILED;

    memset(outLogInfo, 0, sizeof(cmiotLogReportInfo_t));

    if (strlen(s_logDir) == 0) {
        printf("[dev_log] log path not set\n");
        return CMIOT_RETURN_CODE_FAILED;
    }

    /* 扫描日志目录 (无锁, 由管理线程串行化保证一致性) */
    fileNum = scandir(s_logDir, &files, log_filter_file, alphasort);
    if (fileNum < 0) {
        printf("[dev_log] scandir failed, errno=%d, %s\n", errno, strerror(errno));
        return CMIOT_RETURN_CODE_FAILED;
    } else if (fileNum == 0) {
        printf("[dev_log] no dev log files found\n");
        return CMIOT_RETURN_CODE_FAILED;
    }

    for (i = 0; i < fileNum; i++) {
        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", s_logDir, files[i]->d_name);

        time_t fCreate = 0, fModify = 0;
        if (parse_file_time_range(fullPath, files[i]->d_name, &fCreate, &fModify) != 0) {
            free(files[i]);
            continue;
        }

        /* 判断是否在时间范围内 */
        /* startTime为0时, 返回endTime之前的所有文件 */
        if (!(startTime == 0 && fModify <= (time_t)endTime) &&
            (fCreate > (time_t)endTime || fModify < (time_t)startTime)) {
            free(files[i]);
            continue;
        }

        printf("[dev_log] match log file[%d]: %s\n", i, files[i]->d_name);

        struct stat buf = {0};
        if (stat(fullPath, &buf) != 0) {
            free(files[i]);
            continue;
        }

        outLogInfo->logNum++;
        outLogInfo->logSize += buf.st_size;

        cmiotLogPath_t *tmpPtr = (cmiotLogPath_t *)realloc(outLogInfo->pathInfo,
                                     outLogInfo->logNum * sizeof(cmiotLogPath_t));
        if (tmpPtr == NULL) {
            printf("[dev_log] realloc failed!\n");
            if (outLogInfo->pathInfo) {
                free(outLogInfo->pathInfo);
                outLogInfo->pathInfo = NULL;
            }
            ret = CMIOT_RETURN_CODE_FAILED;
            free(files[i]);
            break;
        }
        strncpy(tmpPtr[outLogInfo->logNum - 1].logPath, fullPath,
                sizeof(tmpPtr[outLogInfo->logNum - 1].logPath) - 1);
        tmpPtr[outLogInfo->logNum - 1].logPath[sizeof(tmpPtr[outLogInfo->logNum - 1].logPath) - 1] = '\0';
        outLogInfo->pathInfo = tmpPtr;

        free(files[i]);
    }

    free(files);
    return ret;
}

/**
 * 判断文件名是否为设备日志文件(供外部回调使用)
 * 兼容新旧两种命名格式: DEV_timestamp.txt 和 DEV_MAC_timestamp.txt
 */
int dev_log_filter_by_name(const char *filename)
{
    struct tm tm = {0};
    char timeStr[20] = {0};
    int nameLen;
    const char *timeStart;

    if (filename == NULL) return 0;

    nameLen = strlen(filename);

    /* 最小长度: DEV_ + 时间戳19字符 + .txt = 27 */
    if (nameLen < 27) return 0;

    /* 检查前缀 DEV_ */
    if (strncmp(filename, "DEV_", 4) != 0) return 0;

    /* 检查后缀 .txt */
    if (strncmp(filename + nameLen - 4, ".txt", 4) != 0) return 0;

    /* 时间戳在 .txt 之前, 固定19字符: YYYY-MM-DD-HH-MM-SS */
    timeStart = filename + nameLen - 4 - 19;
    if (timeStart[-1] != '_') return 0;

    strncpy(timeStr, timeStart, 19);
    timeStr[19] = '\0';

    if (strptime(timeStr, DEV_LOG_FILE_TIME_FMT, &tm) != NULL) return 1;

    return 0;
}

const char* dev_log_get_path(void)
{
    return s_logDir;
}
