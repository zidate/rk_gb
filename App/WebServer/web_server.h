#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// 视频分辨率枚举
typedef enum {
    VIDEO_RESOLUTION_HD,    // 高清 2560x1440
    VIDEO_RESOLUTION_SD     // 标清 1280x720
} video_resolution_t;

// 视频编码枚举
typedef enum {
    VIDEO_CODEC_H265,
    VIDEO_CODEC_H264
} video_codec_t;

// 音频编码枚举
typedef enum {
    AUDIO_CODEC_G711A,
    AUDIO_CODEC_G711U,
    AUDIO_CODEC_AAC
} audio_codec_t;

// 夜视模式枚举
typedef enum {
    NIGHT_MODE_DEFAULT,      // 默认
    NIGHT_MODE_WHITE_LIGHT,  // 白光灯开启
    NIGHT_MODE_INFRARED      // 红外灯开启
} night_mode_t;

// 国标连接状态枚举
typedef enum {
    GB_STATUS_OFFLINE,       // 不在线
    GB_STATUS_ONLINE         // 在线
} gb_status_t;

// 1400连接状态枚举
typedef enum {
    GAT1400_STATUS_OFFLINE,       // 不在线
    GAT1400_STATUS_ONLINE         // 在线
} gat1400_status_t;

// SD卡格式化状态枚举
typedef enum {
    SD_FORMAT_IDLE,          // 空闲
    SD_FORMAT_FORMATTING,    // 格式化中
    SD_FORMAT_SUCCESS,       // 格式化成功
    SD_FORMAT_FAILED,        // 格式化失败
    SD_FORMAT_TIMEOUT        // 格式化超时
} sd_format_status_t;

// 视频参数结构体
typedef struct {
    video_codec_t video_codec;       // 视频编码枚举
    int bitrate;                     // 码率 (kbps)
    int framerate;                   // 帧率 (1-60)
    int gop;                         // 关键帧间隔
} video_params_t;

// 设备状态结构体（包含所有可配置参数）
typedef struct {
    char version[32];            // 设备版本号
    int ip_mode;                 // 0: DHCP, 1: Static
    char ip_addr[16];
    char gateway[16];
    char netmask[16];
    char dns[16];
    // 国标参数
    int gb_enable;               // 是否启用国标配置 (0:不启用, 1:启用)
    int gb_config_mode;          // 国标配置方式 (0:零配置, 1:手动配置)
    char gb_code[32];            // SIP服务器编码
    char gb_ip[16];              // SIP服务器IP
    char gb_port[8];             // SIP服务器端口
    char gb_device_id[64];       // 国标设备编码
    char gb_password[32];        // 国标接入密码
    char gb_domain[64];          // SIP服务器域
    char gb_user_id[32];         // 国标用户ID
    char gb_channel_code[32];    // 视频通道编码1
    gb_status_t gb_status;       // 国标服务器连接状态
    // 视频参数 - 主码流和子码流
    video_params_t main_stream;   // 主码流 (2560x1440)
    video_params_t sub_stream;    // 子码流 (1280x720)
    // 音频参数
    audio_codec_t audio_codec;       // 音频编码枚举
    // 夜视功能参数
    night_mode_t night_mode;          // 夜视模式枚举
    // 1400参数
    int gat1400_enable;               // 是否启用1400配置 (0:不启用, 1:启用)
    char gat1400_ip[16];              // 1400接入IPv4
    char gat1400_port[8];            // 1400接入端口
    char gat1400_ipv6[64];            // 1400接入IPv6
    char gat1400_ipv6_port[8];        // 1400接入IPv6端口
    char gat1400_user[32];            // 1400设备用户
    char gat1400_device_id[64];       // 1400设备编码
    char gat1400_password[32];        // 1400设备密码
    gat1400_status_t gat1400_status; // 1400服务器连接状态
    // WiFi 配置
    int wifi_need_config;              // 是否需要WiFi配网 (0:不需要, 1:需要)
    char wifi_ssid[64];                // WiFi SSID
    char wifi_password[64];            // WiFi 密码
    // SD卡配置
    sd_format_status_t sd_format_status;  // SD卡格式化状态
} device_state_t;

/**
 * @brief 配置更新回调函数类型
 * @param state 设备状态结构体指针，包含最新的配置信息
 * @return 0 表示成功，非零表示失败（错误码）
 */
typedef int (*config_update_callback_t)(const device_state_t *state);

/**
 * @brief SD卡格式化回调函数类型
 * @return 0 表示成功，非零表示失败（错误码）
 */
typedef int (*sd_format_callback_t)(void);

/**
 * @brief 启动 Web 服务器（阻塞运行，直到 stop_web_server 被调用）
 * @return 0 成功，-1 失败（如端口被占用）
 */
int start_web_server(void);

/**
 * @brief 停止 Web 服务器（使 start_web_server 返回）
 */
void stop_web_server(void);

/**
 * @brief 更新设备状态（外部调用，用于同步真实设备配置）
 * @param state 新的设备状态指针
 */
void set_device_state(const device_state_t *state);

/**
 * @brief 注册配置更新回调函数
 * @param callback 用户自定义的回调函数，当配置被保存时会被调用
 */
void web_server_register_config_callback(config_update_callback_t callback);

/**
 * @brief 设置Web登录账号密码（必须在 start_web_server 之前调用）
 * @param username 登录用户名
 * @param password 登录密码
 * @note 如果不调用此函数，将使用默认账号: admin, 密码: 123456
 */
void set_login_credentials(const char *username, const char *password);

/**
 * @brief 更新国标服务器连接状态
 * @param status 新的连接状态（在线/离线）
 * @note 此函数可被设备端调用，用于同步国标服务器连接状态到Web端
 */
void update_gb_status(gb_status_t status);

/**
 * @brief 更新1400服务器连接状态
 * @param status 新的连接状态（在线/离线）
 * @note 此函数可被设备端调用，用于同步1400服务器连接状态到Web端
 */
void update_gat1400_status(gat1400_status_t status);

/**
 * @brief 更新SD卡格式化状态
 * @param status 新的格式化状态
 * @note 此函数可被设备端调用，用于同步SD卡格式化状态到Web端
 */
void update_sd_format_status(sd_format_status_t status);

/**
 * @brief 注册SD卡格式化回调函数
 * @param callback 用户自定义的回调函数，当用户点击确认格式化时会被调用
 */
void web_server_register_sd_format_callback(sd_format_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SERVER_H */
