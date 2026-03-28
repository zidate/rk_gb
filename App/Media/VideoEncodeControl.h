#ifndef _VIDEO_ENCODE_CONTROL_H_
#define _VIDEO_ENCODE_CONTROL_H_

#include <string>

namespace media
{

struct VideoEncodeStreamConfig
{
    std::string codec;
    std::string resolution;
    std::string bitrate_type;
    int fps;
    int bitrate_kbps;

    VideoEncodeStreamConfig()
        : fps(0),
          bitrate_kbps(0)
    {
    }
};

struct VideoEncodeStreamState
{
    bool has_codec;
    std::string codec;
    bool has_resolution;
    int width;
    int height;
    std::string resolution;
    bool has_frame_rate;
    std::string frame_rate;
    bool has_bitrate;
    int bitrate_kbps;
    bool has_bitrate_type;
    std::string bitrate_type;
    bool has_gop;
    int gop;

    VideoEncodeStreamState()
        : has_codec(false),
          has_resolution(false),
          width(0),
          height(0),
          has_frame_rate(false),
          has_bitrate(false),
          bitrate_kbps(0),
          has_bitrate_type(false),
          has_gop(false),
          gop(0)
    {
    }
};

struct VideoEncodeState
{
    VideoEncodeStreamState main_stream;
    VideoEncodeStreamState sub_stream;
    std::string resolution_summary;
};

bool QueryVideoEncodeStreamState(int streamId, VideoEncodeStreamState* state);
bool QueryVideoEncodeState(VideoEncodeState* state);
int ApplyVideoEncodeStreamConfig(int streamId, const VideoEncodeStreamConfig& desired);

} // namespace media

#endif
