#ifndef __DEMO_PUBLIC_H__
#define __DEMO_PUBLIC_H__


/* 头文件 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <ftw.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
//#include <ncursesw/ncurses.h> 
//#include <ncursesw/menu.h>
#include <locale.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "cmiot_define.h"
#include "cmiot_api.h"
#include "cmiot_callback.h"

#include "demo_callback.h"
#include "demo_stream.h"
//#include "demo_menu.h"
//#include "demo_tui.h"
#include "demo_socket.h"

#ifndef PTHREAD_CANCELED
#define PTHREAD_CANCELED ((void *)-1)
#endif

#ifndef PTHREAD_CANCEL_ENABLE
#define PTHREAD_CANCEL_ENABLE ((void *)-1)
#endif

#ifdef ANDROID_ENV
#define pthread_cancel(a) pthread_kill(a, SIGTERM)

void demo_sigterm_signal_handler(int signum) {
    DEMO_PRINT("SIGTERM[%d] received, calling pthread_exit now !\n", signum);
    pthread_exit(EXIT_SUCCESS);
}
#endif

#define DEMO_PRINT(...)   printf("[demo] " __VA_ARGS__)

/* 设备日志文件名格式 */
#define DEV_LOG_FILE_NAME_PATTERN       "DEV_%19s.txt"
#define DEV_LOG_FILE_TIME_PATTERN       "%Y-%m-%d-%H-%M-%S"
/* 日志文件大小 */
#define DEV_LOG_FILE_SIZE       (1024 * 1024)



/* 媒体流配置 */
typedef struct
{
    cmiot_uint32_t streamId;                     /* 摄像头流通道id，取值从0开始。多目摄像头表示当前数据所属通道。单目摄像头固定为0 */
    cmiot_char_t mediaFileName[128];             /* 媒体文件名称 */
} cmiotMediaStream_t;


typedef struct
{
  cmiot_uint64_t startTime;                      /* 回放开始时间 */
  cmiot_bool_t newPlayFlag;                      /* 标志位，是否需要更新回放开始时间 */
  cmiot_bool_t stopPlayFlag;                     /* 标志位，是否需要停止回放 */
} cmiotSdReplayThreadInfo_t;


#endif

