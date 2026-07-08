#ifndef _VIDEO_OSD_CONTROL_H_
#define _VIDEO_OSD_CONTROL_H_

#include <string>
#include <vector>

namespace media
{

struct VideoOsdTextItem
{
    bool has_text;
    std::string text;
    bool has_position;
    int x;
    int y;
    bool has_alignment;
    std::string alignment;

    VideoOsdTextItem()
        : has_text(false),
          has_position(false),
          x(0),
          y(0),
          has_alignment(false)
    {
    }
};

struct VideoOsdState
{
    bool has_master_enabled;
    int master_enabled;
    bool has_event_enabled;
    int event_enabled;
    bool has_alert_enabled;
    int alert_enabled;
    bool has_font_size;
    int font_size;
    bool has_font_color_mode;
    std::string font_color_mode;
    bool has_font_color;
    std::string font_color;
    bool has_time_enabled;
    int time_enabled;
    bool has_text_enabled;
    int text_enabled;
    bool has_time_format;
    std::string time_format;
    bool has_date_style;
    std::string date_style;
    bool has_time_style;
    std::string time_style;
    bool has_time_position;
    int time_x;
    int time_y;
    bool has_time_display_week_enabled;
    int time_display_week_enabled;
    bool has_time_alignment;
    std::string time_alignment;
    bool has_text_items;
    std::vector<VideoOsdTextItem> text_items;

    VideoOsdState()
        : has_master_enabled(false),
          master_enabled(0),
          has_event_enabled(false),
          event_enabled(0),
          has_alert_enabled(false),
          alert_enabled(0),
          has_font_size(false),
          font_size(0),
          has_font_color_mode(false),
          has_font_color(false),
          has_time_enabled(false),
          time_enabled(0),
          has_text_enabled(false),
          text_enabled(0),
          has_time_format(false),
          has_date_style(false),
          has_time_style(false),
          has_time_position(false),
          time_x(0),
          time_y(0),
          has_time_display_week_enabled(false),
          time_display_week_enabled(0),
          has_time_alignment(false),
          has_text_items(false)
    {
    }
};

bool ResolveVideoOsdAnchor(const std::string& position, int* x, int* y);
int ApplyVideoOsdConfig(const VideoOsdState& desired);
bool QueryVideoOsdState(VideoOsdState* state);

} // namespace media

#endif
