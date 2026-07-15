#ifndef __DEV_LOG_H__
#define __DEV_LOG_H__

#include "cmiot_define.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 设备日志模块 - 千里眼设备端事件日志 (方案C: 事件驱动管理线程)
 *
 * 架构说明:
 * - 专用日志管理线程通过管道接收事件，串行处理所有文件操作
 * - 业务线程调用 dev_log_write() 时仅做格式化+投递事件，不阻塞于磁盘IO
 * - SD卡状态变化通过 dev_log_notify_* 系列函数通知管理线程
 * - 支持日志转存(Flash→SD卡)、主动上报、异常触发上报
 *
 * 日志格式: [YYYY-MM-DD HH:MM:SS][日志类型][事件类型] 事件内容
 * 文件命名: DEV_MAC_YYYY-MM-DD-HH-MM-SS.txt
 * ============================================================================ */

/* 设备日志路径常量 */
#define DEV_LOG_DEFAULT_DIR         "/userdata/conf/Log/dev"
#define DEV_LOG_SD_DIR              "/mnt/sdcard/Log/dev"
#define DEV_LOG_TURN_DIR_FLASH      "/userdata/conf/Log/turn"      /* 异常上报临时目录(Flash) */
#define DEV_LOG_TURN_DIR_SD         "/mnt/sdcard/Log/turn"         /* 异常上报临时目录(SD卡) */

/* SDK日志路径常量(SD卡状态变化时切换写入路径+异常上报扫描读取) */
#define SDK_LOG_DEFAULT_DIR         "/userdata/conf/Log/sdk"       /* SDK日志Flash路径 */
#define SDK_LOG_SD_DIR              "/mnt/sdcard/Log/sdk"          /* SDK日志SD卡路径 */
#define SDK_LOG_DEFAULT_ROTATE      10                             /* SDK日志Flash路径最大保留文件数 */
#define SDK_LOG_SD_ROTATE           80                             /* SDK日志SD卡路径最大保留文件数 */

/*
 * SDK日志迁移开关:
 *   定义 DEV_LOG_SDK_MIGRATION_ENABLE → SD卡插入时迁移SDK日志文件(Flash→SD)
 *   注释 DEV_LOG_SDK_MIGRATION_ENABLE → 仅切换路径不迁移, 用于验证SDK内部迁移行为
 */
// #define DEV_LOG_SDK_MIGRATION_ENABLE

/* 日志参数限制 */
#define DEV_LOG_MAX_FILE_SIZE       (1024 * 1024)       /* 单个日志文件最大1MB */
#define DEV_LOG_MAX_FILE_SIZE_KB    1024                /* 单个日志文件最大1024KB */
#define DEV_LOG_DEFAULT_ROTATE      20                  /* 默认路径下最大保留20个文件 */
#define DEV_LOG_SD_ROTATE           80                  /* SD卡路径下最大保留80个文件 */
#define DEV_LOG_MAX_LINE_LEN        512                 /* 单条日志最大长度 */

/* 日志类型枚举 */
typedef enum {
    DEV_LOG_INFO    = 0,    /* 普通信息 */
    DEV_LOG_WARNING = 1,    /* 警告 */
    DEV_LOG_ERROR   = 2,    /* 错误 */
} devLogLevel_e;

/* 事件类型枚举 */
typedef enum {
    DEV_EVENT_SYSTEM        = 0,    /* 系统事件(启动/重启) */
    DEV_EVENT_UPGRADE       = 1,    /* 设备升级 */
    DEV_EVENT_SD_CARD       = 2,    /* SD卡操作(格式化/热插拔) */
    DEV_EVENT_AI            = 3,    /* AI事件(开始/结束) */
    DEV_EVENT_TIME          = 4,    /* 系统时间修改 */
    DEV_EVENT_WEB_CONFIG    = 5,    /* 相机Web配置 */
    DEV_EVENT_NETWORK       = 6,    /* 网络断开/重连 */
} devEventType_e;

/* 异常类型枚举 */
typedef enum {
    DEV_EXCEPTION_NETWORK = 0,  /* 网络异常（断开/不可用） */
    DEV_EXCEPTION_DEVICE  = 1,  /* 设备其他异常 */
} devExceptionType_e;

/* ---- 以下为内部事件类型(调用者栈上分配事件时需sizeof，故放头文件) ---- */

/* 管理线程事件类型 */
typedef enum {
    DEV_LOG_EVENT_WRITE = 0,            /* 写日志 */
    DEV_LOG_EVENT_SD_INSERTED,          /* SD卡插入且可用 */
    DEV_LOG_EVENT_SD_REMOVED,           /* SD卡拔出/异常 */
    DEV_LOG_EVENT_SD_FORMAT_START,      /* 格式化开始前 */
    DEV_LOG_EVENT_SD_FORMAT_DONE,       /* 格式化完成后 */
    DEV_LOG_EVENT_ACTIVE_UPLOAD_ON,     /* 主动上报打开 */
    DEV_LOG_EVENT_ACTIVE_UPLOAD_OFF,    /* 主动上报关闭 */
    DEV_LOG_EVENT_EXCEPTION_UPLOAD,     /* 异常触发上报 */
    DEV_LOG_EVENT_SHUTDOWN,             /* 系统关机 */
} devLogEventType_e;

/* 管理线程事件结构体（定长，调用者栈上分配后写入pipe） */
typedef struct {
    devLogEventType_e type;
    union {
        struct {  /* DEV_LOG_EVENT_WRITE */
            devLogLevel_e   level;
            devEventType_e  eventType;
            char            content[DEV_LOG_MAX_LINE_LEN - 128];  /* 预格式化好的日志内容 */
        } write;
        struct {  /* DEV_LOG_EVENT_EXCEPTION_UPLOAD */
            devExceptionType_e  exceptionType;
            cmiot_uint32_t      startTime;      /* 异常开始时间(UTC秒) */
            cmiot_uint32_t      endTime;        /* 异常结束时间(UTC秒) */
        } exceptionUpload;
    } data;
} devLogEvent_t;

/* ============================================================================
 * 公共接口
 * ============================================================================ */

/**
 * 初始化设备日志模块
 * 创建管道、启动管理线程
 * @return 0-成功, -1-失败
 */
cmiot_int32_t dev_log_init(void);

/**
 * 反初始化设备日志模块
 * 发送SHUTDOWN事件给管理线程，等待线程退出
 */
void dev_log_deinit(void);

/**
 * 写入一条设备事件日志
 * 线程安全，格式化后投递到管理线程处理
 *
 * @param [in] level     日志级别
 * @param [in] eventType 事件类型
 * @param [in] format    格式化字符串及后续参数(类似printf)
 */
void dev_log_write(devLogLevel_e level, devEventType_e eventType, const char *format, ...);

/* ---- SD卡状态通知 ---- */

/** SD卡插入且可用 */
void dev_log_notify_sd_inserted(void);

/** SD卡拔出/异常 */
void dev_log_notify_sd_removed(void);

/** 格式化开始前（日志模块立即切回Flash路径） */
void dev_log_notify_sd_format_start(void);

/** 格式化完成后（若SD正常→转存+切回SD路径） */
void dev_log_notify_sd_format_done(void);

/* ---- 主动上报 ---- */

/** 主动上报开关控制 */
void dev_log_notify_active_upload(cmiot_bool_t enable);

/** 获取主动上报开关状态 */
cmiot_bool_t dev_log_get_active_upload(void);

/** 注册主动上报回调函数 */
typedef void (*dev_log_upload_callback_t)(const char *filePath, cmiot_uint64_t fileSize);
void dev_log_register_upload_callback(dev_log_upload_callback_t cb);

/* ---- 异常触发上报 ---- */

/**
 * 异常触发上报
 * 主动上报开关打开+网络恢复后，业务层调用此接口触发上报
 * SDK日志→复制到turn目录+重命名+上传+删除
 * 设备日志→直接上传不删除（即使不满1MB）
 *
 * @param [in] exceptionType  异常类型(网络异常/设备异常)
 * @param [in] startTime      异常开始时间(UTC秒)
 * @param [in] endTime        异常结束时间(UTC秒)
 */
void dev_log_trigger_exception_upload(devExceptionType_e exceptionType,
                                       cmiot_uint32_t startTime,
                                       cmiot_uint32_t endTime);

/* ---- 被动上报 ---- */

/**
 * 获取时间范围内的设备日志文件信息(用于CMIOT_CMD_GET_DEV_LOG回调)
 */
cmiot_int32_t dev_log_get_report_info(cmiot_uint32_t startTime, cmiot_uint32_t endTime,
                                       cmiotLogReportInfo_t *outLogInfo);

/**
 * 判断文件名是否为设备日志文件(供外部回调使用)
 * 兼容新旧两种命名格式: DEV_timestamp.txt 和 DEV_MAC_timestamp.txt
 */
int dev_log_filter_by_name(const char *filename);

/**
 * 获取当前设备日志存储目录路径
 */
const char* dev_log_get_path(void);

#ifdef __cplusplus
}
#endif

#endif /* __DEV_LOG_H__ */
