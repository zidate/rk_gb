#ifndef APP_PROTOCOL_GB28181_MEDIA_SERVER_COMPAT_H
#define APP_PROTOCOL_GB28181_MEDIA_SERVER_COMPAT_H

#include <stddef.h>
#include <stdint.h>

enum
{
    PSI_STREAM_AAC = 0x0F,
    PSI_STREAM_H264 = 0x1B,
    PSI_STREAM_H265 = 0x24,
    PSI_STREAM_AUDIO_G711A = 0x90,
    PSI_STREAM_AUDIO_G711U = 0x91,
    PSI_STREAM_AUDIO_G722 = 0x92,
};

enum
{
    RTP_PAYLOAD_MP2P = 96,
};

struct ps_muxer_func_t
{
    void* (*alloc)(void* param, size_t bytes);
    void (*free)(void* param, void* packet);
    int (*write)(void* param, int stream, void* packet, size_t bytes);
};

struct ps_muxer_t;

struct rtp_payload_t
{
    void* (*alloc)(void* param, int bytes);
    void (*free)(void* param, void* packet);
    int (*packet)(void* param, const void* packet, int bytes, uint32_t timestamp, int flags);
};

#endif
