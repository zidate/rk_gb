#ifndef __GB28181_PROTOCOL_CONSTANTS_H__
#define __GB28181_PROTOCOL_CONSTANTS_H__

#include <string>

#include "GB28181Defs.h"

namespace protocol {
namespace gb28181 {

static const char kDefaultDeviceId[] = "34020000001320000001";
static const unsigned int kDefaultRegisterExpiresSec = 3600U;
static const unsigned int kDefaultKeepaliveIntervalSec = 60U;
static const unsigned int kDefaultKeepaliveRetryCount = 3U;
static const char kDefaultBitrateType[] = "VBR";
static const char kDefaultResolutionSummary[] = "1920x1080/640x360";
static const unsigned int kDefaultOsdCanvasWidth = 1920U;
static const unsigned int kDefaultOsdCanvasHeight = 1080U;

static const int kAudioPayloadTypePcma = 8;
static const int kAudioPayloadTypePcmu = 0;
static const int kAudioPayloadTypePcm = 96;

static const char kAudioMimePcma[] = "PCMA";
static const char kAudioMimePcmu[] = "PCMU";
static const char kAudioMimePcm[] = "L16";

static const char kXmlElementOsdConfig[] = "OSDConfig";
static const char kXmlElementVideoParamAttribute[] = "VideoParamAttribute";
static const char kXmlElementFrameMirror[] = "FrameMirror";

static const char kConfigTypeBasicParam[] = "BasicParam";
static const char kConfigTypeVideoParamOpt[] = "VideoParamOpt";
static const char kConfigTypeVideoParamAttribute[] = "VideoParamAttribute";
static const char kConfigTypeFrameMirror[] = "FrameMirror";
static const char kConfigTypeSvacEncode[] = "SVACEncodeConfig";
static const char kConfigTypeSvacDecode[] = "SVACDecodeConfig";
static const char kConfigTypeOsd[] = "OSDConfig";

static const char kCmdTypeDeviceConfig[] = "DeviceConfig";
static const char kCmdTypeDeviceControl[] = "DeviceControl";
static const char kCmdTypeDeviceStatus[] = "DeviceStatus";
static const char kCmdTypeCatalog[] = "Catalog";
static const char kCmdTypeMobilePosition[] = "MobilePosition";
static const char kCmdTypeDeviceInfo[] = "DeviceInfo";
static const char kCmdTypeRecordInfo[] = "RecordInfo";
static const char kCmdTypeAlarm[] = "Alarm";
static const char kCmdTypeConfigDownload[] = "ConfigDownload";
static const char kCmdTypePresetQuery[] = "PresetQuery";
static const char kCmdTypeKeepalive[] = "Keepalive";
static const char kCmdTypeMediaStatus[] = "MediaStatus";
static const char kCmdTypeBroadcast[] = "Broadcast";
static const char kCmdTypeDeviceUpgradeResult[] = "DeviceUpgradeResult";

static const char kSdpMediaAudioPrefix[] = "m=audio ";
static const char kSdpAttributeSetupPrefix[] = "a=setup:";
static const char kSdpTransportTcpRtpAvp[] = "TCP/RTP/AVP";
static const char kSdpTransportRtpAvp[] = "RTP/AVP";

inline std::string ToLowerAsciiCopy(const std::string& text)
{
    std::string out = text;
    for (std::string::size_type i = 0; i < out.size(); ++i) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = static_cast<char>(out[i] - 'A' + 'a');
        }
    }
    return out;
}

inline std::string NormalizeGbAudioCodec(const std::string& codecIn)
{
    const std::string codec = ToLowerAsciiCopy(codecIn);
    if (codec == "pcma" || codec == "g711a") {
        return "g711a";
    }

    if (codec == "pcmu" || codec == "g711u") {
        return "g711u";
    }

    if (codec == "pcm" || codec == "l16" || codec == "pcm16" || codec == "pcm_s16le") {
        return "pcm";
    }

    return "";
}

inline int ResolveGbAudioPayloadType(const std::string& codecIn)
{
    const std::string codec = NormalizeGbAudioCodec(codecIn);
    if (codec == "g711a") {
        return kAudioPayloadTypePcma;
    }

    if (codec == "g711u") {
        return kAudioPayloadTypePcmu;
    }

    return kAudioPayloadTypePcm;
}

inline const char* ResolveGbAudioMimeName(const std::string& codecIn)
{
    const std::string codec = NormalizeGbAudioCodec(codecIn);
    if (codec == "g711a") {
        return kAudioMimePcma;
    }

    if (codec == "g711u") {
        return kAudioMimePcmu;
    }

    return kAudioMimePcm;
}

inline std::string NormalizeGbImageFlipModeCanonical(const std::string& modeIn)
{
    const std::string mode = ToLowerAsciiCopy(modeIn);
    if (mode.empty() || mode == "0" || mode == "none" || mode == "close" ||
        mode == "off" || mode == "restore" || mode == "reset") {
        return "close";
    }

    if (mode == "1" || mode == "mirror" || mode == "horizontal" ||
        mode == "hflip" || mode == "leftright" || mode == "left_right") {
        return "mirror";
    }

    if (mode == "2" || mode == "flip" || mode == "vertical" ||
        mode == "vflip" || mode == "updown" || mode == "up_down") {
        return "flip";
    }

    if (mode == "3" || mode == "centrosymmetric" || mode == "center" ||
        mode == "both" || mode == "all") {
        return "centrosymmetric";
    }

    return "";
}

inline std::string NormalizeGbImageFlipModeOrKeepInput(const std::string& modeIn)
{
    const std::string mode = NormalizeGbImageFlipModeCanonical(modeIn);
    return mode.empty() ? modeIn : mode;
}

inline const char* ResolveGbFrameMirrorValue(const std::string& modeIn)
{
    const std::string mode = NormalizeGbImageFlipModeCanonical(modeIn);
    if (mode == "mirror") {
        return "1";
    }
    if (mode == "flip") {
        return "2";
    }
    if (mode == "centrosymmetric") {
        return "3";
    }
    return "0";
}

inline const char* GetGbConfigTypeName(ConfigType type)
{
    switch (type) {
        case kBasicParam:
            return kConfigTypeBasicParam;
        case kVideoParamOpt:
            return kConfigTypeVideoParamOpt;
        case kVideoParamAttribute:
            return kConfigTypeVideoParamAttribute;
        case kFrameMirrorConfig:
            return kConfigTypeFrameMirror;
        case kSVACEncodeConfig:
            return kConfigTypeSvacEncode;
        case kSVACDecodeConfig:
            return kConfigTypeSvacDecode;
        case kOsdConfig:
            return kConfigTypeOsd;
        case kUnknow:
        default:
            return 0;
    }
}

inline ConfigType ParseGbConfigTypeName(const std::string& typeName)
{
    if (typeName == kConfigTypeBasicParam) {
        return kBasicParam;
    }
    if (typeName == kConfigTypeVideoParamOpt) {
        return kVideoParamOpt;
    }
    if (typeName == kConfigTypeVideoParamAttribute) {
        return kVideoParamAttribute;
    }
    if (typeName == kConfigTypeFrameMirror) {
        return kFrameMirrorConfig;
    }
    if (typeName == kConfigTypeSvacEncode) {
        return kSVACEncodeConfig;
    }
    if (typeName == kConfigTypeSvacDecode) {
        return kSVACDecodeConfig;
    }
    if (typeName == kConfigTypeOsd) {
        return kOsdConfig;
    }
    return kUnknow;
}

}  // namespace gb28181
}  // namespace protocol

#endif  // __GB28181_PROTOCOL_CONSTANTS_H__
