/**
 * @file dev_log_test.cpp
 * @brief 设备日志轮询测试线程实现
 *
 * 轮询调用 dev_log_write() 写入设备日志，每条间隔2s
 * 轮转覆盖所有日志级别和事件类型，方便测试设备日志模块
 *
 * 日志级别轮转: INFO → WARNING → ERROR → INFO → ...
 * 事件类型轮转: SYSTEM → UPGRADE → SD_CARD → AI → TIME → WEB_CONFIG → NETWORK → ...
 */

#include "dev_log.h"
#include "dev_log_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* 测试日志内容(每个事件类型有3条变体，轮转使用) */
static const char *s_testEventContents[7][3] = {
    /* SYSTEM */
    {
        "device cold boot completed, uptime=60s",
        "system service health check passed, all services running",
        "device periodic self-test finished, status normal",
    },
    /* UPGRADE */
    {
        "firmware version check: current=v2.1.3, latest=v2.1.4",
        "OTA package download completed, size=15.6MB, verifying",
        "upgrade patch applied successfully, ready to reboot",
    },
    /* SD_CARD */
    {
        "SD card detected: capacity=64GB, filesystem=FAT32",
        "SD card write speed test: 18.5MB/s, status=healthy",
        "SD card lifetime check: used=120 days, remaining=85%",
    },
    /* AI */
    {
        "AI human detection started, sensitivity=medium",
        "AI vehicle detection triggered, plate=unknown, speed=45km/h",
        "AI face detection completed, match threshold=0.85",
    },
    /* TIME */
    {
        "NTP time sync: offset=-12ms, server=ntp.aliyun.com",
        "daylight saving time check: DST inactive, zone=CST+8",
        "system clock drift compensation applied, drift=3ppm",
    },
    /* WEB_CONFIG */
    {
        "web config changed: param=video_bitrate, old=2048, new=4096",
        "web config changed: param=osd_text, old='', new='Camera01'",
        "web config batch import completed: 15 params updated",
    },
    /* NETWORK */
    {
        "ethernet link up: speed=100Mbps, duplex=full, auto-negotiation=on",
        "wifi signal strength: RSSI=-45dBm, channel=6, tx_rate=65Mbps",
        "network reconnection succeeded, ip=192.168.1.100, gateway=192.168.1.1",
    },
};

static pthread_t s_testThreadId = 0;
static volatile int s_testRunning = 0;

/* 间隔挡位配置: 通过创建对应文件切换间隔，文件存在即生效 */
static const struct {
    const char *filePath;
    int          intervalMs;
} s_intervalConfig[] = {
    { "/tmp/tmp_50",    50   },
    { "/tmp/tmp_100",   100  },
    { "/tmp/tmp_200",   200  },
    { "/tmp/tmp_500",   500  },
    { "/tmp/tmp_1000",  1000 },
    { "/tmp/tmp_2000",  2000 },
};
static const int s_intervalConfigCount = sizeof(s_intervalConfig) / sizeof(s_intervalConfig[0]);

/**
 * 设备日志轮询测试线程主函数
 * 每间隔轮询调用 dev_log_write()，轮转所有级别和事件类型
 * 默认间隔2000ms，可通过touch对应文件动态切换: /tmp/tmp_50 /tmp/tmp_100 /tmp/tmp_200 /tmp/tmp_500 /tmp/tmp_1000 /tmp/tmp_2000
 */
static void *dev_log_test_thread(void *arg)
{
    int cycleIndex = 0;
    int levelCycle = 0;
    int eventCycle = 0;
    int contentCycle = 0;
    int intervalMs = 2000;  /* 默认2000ms */

    (void)arg;

    printf("[dev_log_test] polling thread started, interval=2000ms\n");

    while (s_testRunning) {
        /* 检测间隔配置文件是否存在，存在则切换间隔并删除文件 */
        for (int i = 0; i < s_intervalConfigCount; i++) {
            if (access(s_intervalConfig[i].filePath, F_OK) == 0) {
                intervalMs = s_intervalConfig[i].intervalMs;
                printf("[dev_log_test] interval switched to %dms\n", intervalMs);
                remove(s_intervalConfig[i].filePath);
                break;  /* 一次只处理一个文件 */
            }
        }

        /* 轮转日志级别 */
        devLogLevel_e level = (devLogLevel_e)(levelCycle % 3);
        levelCycle++;

        /* 轮转事件类型 */
        devEventType_e eventType = (devEventType_e)(eventCycle % 7);
        eventCycle++;

        /* 每个事件类型有3条不同内容，轮转 */
        int contentIdx = contentCycle % 3;
        contentCycle++;

        /* 写入设备日志 */
        switch (eventType) {
        case DEV_EVENT_SYSTEM:
            dev_log_write(level, eventType, "%s [cycle=%d]",
                          s_testEventContents[DEV_EVENT_SYSTEM][contentIdx],
                          cycleIndex);
            break;

        case DEV_EVENT_UPGRADE:
            dev_log_write(level, eventType, "%s [cycle=%d]",
                          s_testEventContents[DEV_EVENT_UPGRADE][contentIdx],
                          cycleIndex);
            break;

        case DEV_EVENT_SD_CARD:
            dev_log_write(level, eventType, "%s [cycle=%d]",
                          s_testEventContents[DEV_EVENT_SD_CARD][contentIdx],
                          cycleIndex);
            break;

        case DEV_EVENT_AI:
            dev_log_write(level, eventType, "%s [cycle=%d]",
                          s_testEventContents[DEV_EVENT_AI][contentIdx],
                          cycleIndex);
            break;

        case DEV_EVENT_TIME:
            dev_log_write(level, eventType, "%s [cycle=%d]",
                          s_testEventContents[DEV_EVENT_TIME][contentIdx],
                          cycleIndex);
            break;

        case DEV_EVENT_WEB_CONFIG:
            dev_log_write(level, eventType, "%s [cycle=%d]",
                          s_testEventContents[DEV_EVENT_WEB_CONFIG][contentIdx],
                          cycleIndex);
            break;

        case DEV_EVENT_NETWORK:
            dev_log_write(level, eventType, "%s [cycle=%d]",
                          s_testEventContents[DEV_EVENT_NETWORK][contentIdx],
                          cycleIndex);
            break;

        default:
            dev_log_write(level, DEV_EVENT_SYSTEM, "unknown event test [cycle=%d]",
                          cycleIndex);
            break;
        }

        // printf("[dev_log_test] cycle=%d, level=%d, event=%d, content=%d written\n",
        //        cycleIndex, level, eventType, contentIdx);

        cycleIndex++;

        /* 休眠(根据当前间隔) */
        usleep(intervalMs * 1000);
    }

    printf("[dev_log_test] polling thread stopped, total cycles=%d\n", cycleIndex);
    return NULL;
}

int dev_log_test_start(void)
{
    if (s_testRunning) {
        printf("[dev_log_test] already running\n");
        return -1;
    }

    s_testRunning = 1;

    if (pthread_create(&s_testThreadId, NULL, dev_log_test_thread, NULL) != 0) {
        printf("[dev_log_test] ERROR: failed to create test thread\n");
        s_testRunning = 0;
        return -1;
    }

    printf("[dev_log_test] started successfully\n");
    return 0;
}

void dev_log_test_stop(void)
{
    if (!s_testRunning) {
        printf("[dev_log_test] not running\n");
        return;
    }

    s_testRunning = 0;

    if (s_testThreadId) {
        pthread_join(s_testThreadId, NULL);
        s_testThreadId = 0;
    }

    printf("[dev_log_test] stopped\n");
}
