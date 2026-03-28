#ifndef _VIDEO_IMAGE_CONTROL_H_
#define _VIDEO_IMAGE_CONTROL_H_

#include <string>

namespace media
{

bool QueryVideoImageFlipMode(std::string* value);
int ApplyVideoImageFlipMode(const std::string& desiredMode);

} // namespace media

#endif
