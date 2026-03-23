#ifndef __PROTOCOL_EXTERNAL_CONFIG_H__
#define __PROTOCOL_EXTERNAL_CONFIG_H__

#include <string>
#include <vector>

namespace protocol
{

struct GbRegisterParam
{
    int enabled;
    std::string server_ip;
    int server_port;
    std::string device_id;
    std::string device_name;
    std::string username;
    std::string password;
    int expires_sec;

    GbRegisterParam() : enabled(1), server_port(0), device_name("IPC"), expires_sec(3600) {}
};

struct GbKeepaliveParam
{
    int interval_sec;
    int timeout_sec;
    int max_retry;

    GbKeepaliveParam() : interval_sec(60), timeout_sec(10), max_retry(3) {}
};

struct GbLiveParam
{
    std::string transport;
    std::string target_ip;
    int target_port;
    int local_port;
    std::string video_stream_id;
    std::string video_codec;
    std::string audio_codec;
    int mtu;
    int payload_type;
    int ssrc;
    int clock_rate;

    GbLiveParam()
        : transport("udp"),
          target_ip("127.0.0.1"),
          target_port(30000),
          local_port(0),
          video_stream_id("main"),
          video_codec("h264"),
          audio_codec("g711a"),
          mtu(1200),
          payload_type(96),
          ssrc(0),
          clock_rate(90000) {}
};

struct GbPlaybackParam
{
    int max_sessions;
    int seek_granularity_sec;
    int download_max_bandwidth_kbps;

    GbPlaybackParam() : max_sessions(4), seek_granularity_sec(1), download_max_bandwidth_kbps(4096) {}
};

struct GbTalkParam
{
    std::string codec;
    int recv_port;
    int sample_rate;
    int jitter_buffer_ms;

    GbTalkParam() : codec("g711a"), recv_port(30003), sample_rate(8000), jitter_buffer_ms(80) {}
};

struct GbPtzParam
{
    int max_speed;
    int preset_limit;

    GbPtzParam() : max_speed(255), preset_limit(255) {}
};

struct GbVideoParam
{
    std::string main_codec;
    std::string main_resolution;
    int main_fps;
    int main_bitrate_kbps;
    std::string sub_codec;
    std::string sub_resolution;
    int sub_fps;
    int sub_bitrate_kbps;

    GbVideoParam()
        : main_codec("H.264"),
          main_resolution("1920x1080"),
          main_fps(25),
          main_bitrate_kbps(2048),
          sub_codec("H.264"),
          sub_resolution("640x360"),
          sub_fps(15),
          sub_bitrate_kbps(512) {}
};

struct GbRebootParam
{
    int cooldown_sec;
    int require_auth_level;

    GbRebootParam() : cooldown_sec(60), require_auth_level(1) {}
};

struct GbImageParam
{
    std::string flip_mode;

    GbImageParam() : flip_mode("none") {}
};

struct GbOsdParam
{
    std::string text_template;
    std::string time_format;
    std::string position;
    int time_enabled;
    int event_enabled;
    int alert_enabled;

    GbOsdParam()
        : text_template("{device_id} {time}"),
          time_format("yyyy-MM-dd HH:mm:ss"),
          position("top_left"),
          time_enabled(1),
          event_enabled(0),
          alert_enabled(0) {}
};

struct GbUpgradeParam
{
    std::vector<std::string> url_whitelist;
    int max_package_mb;
    std::string verify_mode;
    int timeout_sec;

    GbUpgradeParam() : max_package_mb(128), verify_mode("md5"), timeout_sec(120) {}
};

struct GbAlarmParam
{
    int default_priority;
    std::string retry_policy;

    GbAlarmParam() : default_priority(4), retry_policy("1,2,5") {}
};

struct GbMultiStreamParam
{
    int stream_count;
    std::string mapping_json;

    GbMultiStreamParam() : stream_count(2), mapping_json("{}") {}
};

struct GbBroadcastParam
{
    std::string input_mode;
    std::string codec;
    int recv_port;
    std::string file_cache_dir;
    std::string transport;

    GbBroadcastParam() : input_mode("stream"), codec("g711a"), recv_port(0), file_cache_dir("/tmp"), transport("tcp") {}
};

struct GbListenParam
{
    std::string transport;
    std::string target_ip;
    int target_port;
    std::string codec;
    int packet_ms;
    int sample_rate;

    GbListenParam() : transport("udp"), target_port(0), codec("g711a"), packet_ms(20), sample_rate(8000) {}
};

struct GatRegisterParam
{
    std::string server_ip;
    int server_port;
    std::string device_id;
    std::string username;
    std::string password;
    std::string auth_method;
    int listen_port;
    int expires_sec;
    int keepalive_interval_sec;
    int max_retry;

    GatRegisterParam()
        : server_port(0),
          auth_method("digest"),
          listen_port(18080),
          expires_sec(3600),
          keepalive_interval_sec(60),
          max_retry(3) {}
};

struct GatUploadParam
{
    int batch_size;
    int flush_interval_ms;
    std::string retry_policy;

    GatUploadParam() : batch_size(10), flush_interval_ms(500), retry_policy("1,2,5") {}
};

struct GatCaptureParam
{
    std::string face_profile;
    std::string nonmotor_profile;
    std::string vehicle_profile;
    int concurrent_limit;

    GatCaptureParam() : concurrent_limit(3) {}
};

struct CloudFastAccessParam
{
    std::string endpoint;
    std::string token;
    int timeout_ms;

    CloudFastAccessParam() : timeout_ms(5000) {}
};

struct ProtocolExternalConfig
{
    std::string version;

    GbRegisterParam gb_register;
    GbKeepaliveParam gb_keepalive;
    GbLiveParam gb_live;
    GbPlaybackParam gb_playback;
    GbTalkParam gb_talk;
    GbPtzParam gb_ptz;
    GbVideoParam gb_video;
    GbRebootParam gb_reboot;
    GbImageParam gb_image;
    GbOsdParam gb_osd;
    GbUpgradeParam gb_upgrade;
    GbAlarmParam gb_alarm;
    GbMultiStreamParam gb_multistream;
    GbBroadcastParam gb_broadcast;
    GbListenParam gb_listen;

    GatRegisterParam gat_register;
    GatUploadParam gat_upload;
    GatCaptureParam gat_capture;

    CloudFastAccessParam cloud_fast_access;

    ProtocolExternalConfig() : version("v1") {}
};

}

#endif
