#include "GB28181XmlParser.h"
#include "catalog_query.h"
#include "device_info.h"
#include "gbutil.h"
#include "ptz_control.h"
#include "Iframe_control.h"
#include "record_control.h"
#include "record_query.h"
#include "zoom_control.h"
#include "base_proco.h"
#include "guard_control.h"
#include "alarm_reset.h"
#include "response.h"
#include "catalog_sub.h"
#include "mobile_position_sub.h"
#include "alarm_sub.h"
#include "MarkupSTL.h"
#include "alarm_notify.h"
#include "home_position_control.h"
#include "mobile_position_notify.h"
#include "catalog_response.h"
#include "device_status_response.h"
#include "preset_response.h"
#include "record_index_response.h"
#include "config_query.h"
#include "configdownload_response.h"
#include "media_status_notify.h"
#include "broadcast_notify.h"
#include "keepalive_response.h"
#include "teleboot_control.h"
#include "device_upgrade_control.h"
#include "device_upgrade_result_notify.h"
#include "GB28181ProtocolConstants.h"

#define    QUERY        "Query"
#define    RESPONSE   "Response"
#define    NOTIFY        "Notify"
#define    CONTROL     "Control"


#define PTZ_START  1
#define PTZ_STOP    0

#define PTZ_CMD_LEFT	    0x02
#define PTZ_CMD_RIGHT	     0x01
#define PTZ_CMD_DOWN	     0x04
#define PTZ_CMD_UP		     0x08
#define PTZ_CMD_ZOOMIN	 0x20
#define PTZ_CMD_ZOOMOUT 0x10
#define PTZ_CMD_FI		     0x40

#define PTZ_CMD_ADD_POS		0x81
#define PTZ_CMD_CALL_POS	    0x82
#define PTZ_CMD_DEL_POS		0x83

#define PTZ_CMD_CRU_ADD		0x84
#define PTZ_CMD_CRU_DEL		0x85
#define PTZ_CMD_CRU_SPD		0x86
#define PTZ_CMD_CRU_TIME	   0x87
#define PTZ_CMD_CRU_START	0x88

#define PTZ_CMD_SCAN		     0x89
#define PTZ_CMD_SCAN_SET	0x8A
using namespace std;

namespace
{

static bool ParseUnsignedValue(const std::string& text, unsigned int* value)
{
    if (value == NULL || text.empty()) {
        return false;
    }

    char* end = NULL;
    unsigned long parsed = strtoul(text.c_str(), &end, 10);
    if (end == text.c_str()) {
        return false;
    }

    *value = (unsigned int)parsed;
    return true;
}

static bool ParseBoolValue(const std::string& text, bool* value)
{
    if (value == NULL || text.empty()) {
        return false;
    }

    if (text == "1" || text == "true" || text == "TRUE" ||
        text == "True" || text == "ON" || text == "on") {
        *value = true;
        return true;
    }

    if (text == "0" || text == "false" || text == "FALSE" ||
        text == "False" || text == "OFF" || text == "off") {
        *value = false;
        return true;
    }

    unsigned int parsed = 0;
    if (!ParseUnsignedValue(text, &parsed)) {
        return false;
    }

    *value = (parsed != 0);
    return true;
}

static bool ReadMarkupUnsigned(CMarkupSTL& xml,
                               const char* name,
                               unsigned int* value,
                               bool required,
                               unsigned int defaultValue = 0)
{
    if (!xml.FindElem(name)) {
        if (!required && value != NULL) {
            *value = defaultValue;
        }
        return !required;
    }

    return ParseUnsignedValue(xml.GetData(), value);
}

static void CopyMarkupText(char* dest, size_t destSize, const std::string& text)
{
    if (dest == NULL || destSize == 0) {
        return;
    }

    memset(dest, 0, destSize);
    if (!text.empty()) {
        strncpy(dest, text.c_str(), destSize - 1);
    }
}

static std::string BuildOsdConfigXml(const CfgOsdConfig& osd)
{
    CMarkupSTL markup;
    markup.AddElem(protocol::gb28181::kXmlElementOsdConfig);
    if (!markup.IntoElem()) {
        return "";
    }

    markup.AddElem("Length", (int)osd.Length);
    markup.AddElem("Width", (int)osd.Width);
    markup.AddElem("TimeX", (int)osd.TimeX);
    markup.AddElem("TimeY", (int)osd.TimeY);
    markup.AddElem("TimeEnable", (int)osd.TimeEnable);
    markup.AddElem("TimeType", (int)osd.TimeType);
    markup.AddElem("TextEnable", (int)osd.TextEnable);
    markup.AddElem("SumNum", (int)osd.SumNum);

    const unsigned int itemCount = (osd.SumNum > MAX_OSD_TEXT_NUM) ? MAX_OSD_TEXT_NUM : osd.SumNum;
    for (unsigned int index = 0; index < itemCount; ++index) {
        markup.AddElem("Item");
        if (!markup.IntoElem()) {
            continue;
        }

        markup.AddElem("Text", osd.Item[index].Text);
        markup.AddElem("X", (int)osd.Item[index].X);
        markup.AddElem("Y", (int)osd.Item[index].Y);
        markup.OutOfElem();
    }

    markup.OutOfElem();
    return markup.GetDoc();
}

static std::string BuildVideoParamAttributeXml(const CfgVideoParamAttribute& attr)
{
    CMarkupSTL markup;
    markup.AddElem(protocol::gb28181::kXmlElementVideoParamAttribute);
    if (!markup.IntoElem()) {
        return "";
    }

    markup.AddElem("StreamNumber", (int)attr.StreamNumber);
    markup.AddElem("VideoFormat", attr.VideoFormat);
    markup.AddElem("Resolution", attr.Resolution);
    markup.AddElem("FrameRate", (int)attr.FrameRate);
    markup.AddElem("BitRateType", (int)attr.BitRateType);
    markup.AddElem("VideoBitRate", (int)attr.VideoBitRate);

    markup.OutOfElem();
    return markup.GetDoc();
}

static std::string NormalizeFrameMirrorMode(const std::string& valueIn)
{
    return protocol::gb28181::NormalizeGbImageFlipModeCanonical(valueIn);
}

static std::string BuildFrameMirrorValue(const char* flipMode)
{
    return protocol::gb28181::ResolveGbFrameMirrorValue(flipMode ? flipMode : "");
}

static std::string BuildFrameMirrorXml(const CfgFrameMirror& config)
{
    CMarkupSTL markup;
    const std::string value = BuildFrameMirrorValue(config.FrameMirror);
    markup.AddElem(protocol::gb28181::kXmlElementFrameMirror, value.c_str());
    return markup.GetDoc();
}

static bool InjectXmlBeforeClosingTag(std::string& xml, const std::string& payload, const char* closingTag)
{
    if (payload.empty() || closingTag == NULL || closingTag[0] == '\0') {
        return false;
    }

    const std::string tag = closingTag;
    const std::string::size_type pos = xml.rfind(tag);
    if (pos == std::string::npos) {
        return false;
    }

    xml.insert(pos, payload);
    return true;
}

static bool ParseOsdConfigElement(const std::string& xml_str, OsdSetting* setting)
{
    if (setting == NULL) {
        return false;
    }

    memset(setting, 0, sizeof(*setting));
    setting->TimeEnable = 1;
    setting->TextEnable = 1;

    CMarkupSTL markup;
    if (!markup.SetDoc(xml_str.c_str(), xml_str.size()) ||
        !markup.FindElem(CONTROL) ||
        !markup.IntoElem() ||
        !markup.FindElem(protocol::gb28181::kXmlElementOsdConfig) ||
        !markup.IntoElem()) {
        return false;
    }

    if (!ReadMarkupUnsigned(markup, "Length", &setting->Length, true) ||
        !ReadMarkupUnsigned(markup, "Width", &setting->Width, true) ||
        !ReadMarkupUnsigned(markup, "TimeX", &setting->TimeX, true) ||
        !ReadMarkupUnsigned(markup, "TimeY", &setting->TimeY, true) ||
        !ReadMarkupUnsigned(markup, "TimeEnable", &setting->TimeEnable, false, 1) ||
        !ReadMarkupUnsigned(markup, "TimeType", &setting->TimeType, false, 0) ||
        !ReadMarkupUnsigned(markup, "TextEnable", &setting->TextEnable, false, 1) ||
        !ReadMarkupUnsigned(markup, "SumNum", &setting->SumNum, false, 0)) {
        return false;
    }

    unsigned int itemIndex = 0;
    while (itemIndex < MAX_OSD_TEXT_NUM && markup.FindElem("Item")) {
        if (!markup.IntoElem()) {
            break;
        }

        if (markup.FindElem("Text")) {
            CopyMarkupText(setting->Item[itemIndex].Text,
                           sizeof(setting->Item[itemIndex].Text),
                           markup.GetData());
        }
        if (markup.FindElem("X")) {
            (void)ParseUnsignedValue(markup.GetData(), &setting->Item[itemIndex].X);
        }
        if (markup.FindElem("Y")) {
            (void)ParseUnsignedValue(markup.GetData(), &setting->Item[itemIndex].Y);
        }

        markup.OutOfElem();
        ++itemIndex;
    }

    if (setting->SumNum == 0 && itemIndex > 0) {
        setting->SumNum = itemIndex;
    }

    return true;
}

static bool ParseVideoParamAttributeElements(const std::string& xml_str,
                                            std::vector<VideoParamAttributeSetting>* settings)
{
    if (settings == NULL) {
        return false;
    }

    settings->clear();

    CMarkupSTL markup;
    if (!markup.SetDoc(xml_str.c_str(), xml_str.size()) ||
        !markup.FindElem(CONTROL) ||
        !markup.IntoElem()) {
        return false;
    }

    bool found = false;
    while (markup.FindElem(protocol::gb28181::kXmlElementVideoParamAttribute)) {
        found = true;
        if (!markup.IntoElem()) {
            break;
        }

        VideoParamAttributeSetting setting;
        memset(&setting, 0, sizeof(setting));
        if (!ReadMarkupUnsigned(markup, "StreamNumber", &setting.StreamNumber, true)) {
            markup.OutOfElem();
            return false;
        }

        if (markup.FindElem("VideoFormat")) {
            CopyMarkupText(setting.VideoFormat,
                           sizeof(setting.VideoFormat),
                           markup.GetData());
        }
        if (markup.FindElem("Resolution")) {
            CopyMarkupText(setting.Resolution,
                           sizeof(setting.Resolution),
                           markup.GetData());
        }
        (void)ReadMarkupUnsigned(markup, "FrameRate", &setting.FrameRate, false, 0);
        (void)ReadMarkupUnsigned(markup, "BitRateType", &setting.BitRateType, false, 0);
        (void)ReadMarkupUnsigned(markup, "VideoBitRate", &setting.VideoBitRate, false, 0);

        markup.OutOfElem();
        settings->push_back(setting);
    }

    return found && !settings->empty();
}

static bool ParseFrameMirrorElement(const std::string& xml_str, ImageSetting* setting)
{
    if (setting == NULL) {
        return false;
    }

    CMarkupSTL markup;
    if (!markup.SetDoc(xml_str.c_str(), xml_str.size()) ||
        !markup.FindElem(CONTROL) ||
        !markup.IntoElem() ||
        !markup.FindElem(protocol::gb28181::kXmlElementFrameMirror)) {
        return false;
    }

    const std::string mode = NormalizeFrameMirrorMode(markup.GetData());
    if (mode.empty()) {
        return false;
    }

    memset(setting, 0, sizeof(*setting));
    CopyMarkupText(setting->FlipMode, sizeof(setting->FlipMode), mode);
    return true;
}

} // namespace

ControlType GetControlType( ProtocolType  type )
{
    switch( (int)type ){
       case kPtzControlProco : return  kPtzControl;
       case kTeleBootControlProco : return kTeleBootControl;
       case kDeviceUpgradeControlProco : return kDeviceUpgradeControl;
       case kRecordControlProco : return kRecordControl ;
       case kGurdControlProco : return kGurdControl;
       case kAlarmResetControlProco : return kAlarmResetControl;
       case kZoomInControlProco : return kDragZoomInControl;
       case kZoomOutControlProco : return kDragZoomOutControl;
       case kDevConfigControlProco : return kDevConfigControl;
       case kIFameControlProco : return kIFameControl;
       case kHomePositionControlProco : return kHomePositionControl;
    }
      return kUnknowControl;
}

QueryType GetQueryType( ProtocolType  type )
{
    switch( (int)type ){
      case kDevStatusQueryProco  :         return  kDeviceStatusQuery ;
      case kCatalogQueryProco  :             return  kDeviceCatalogQuery ;
      case kDevInfoQueryProco  :             return  kDeviceInfoQuery ;
      case kRecordIndexQueryProco  :       return  kDeviceRecordQuery;
      case kConfigDownloadQueryProco :   return  kDeviceConfigQuery;
      case kPresetQueryProco  :               return kDevicePresetQuery ;
    }
      return kUnknowQuery;
}

NotifyType GetNotifyType( ProtocolType  type )
{
    switch( (int)type ){
       case kKeepaliveNotifyProco : return kKeepaliveNotify;
       case kAlarmNotifyProco : return kAlarmNotify;
       case kMediaStatusNotifyProco : return kMediaStatusNotify;
       case kBroadcastNotifyProco : return kBroadcastNotify;
       case kMobilePositionNotifyProco : return kMobilePositionNotify;
       case kCatalogNotifyProco: return kCatalogNotify;
    }
      return kUnkonwNotify;
}



void BuildString(vector<string>& src_vec , string& output, const string& separator)
{
     int i  = 0;
     int last = src_vec.size() -1;
     for(;i < (int)src_vec.size();i++) {
		  std::string&  param = src_vec[i];
          output.append(param);
          if(   i != last ) {
              output.append(separator);
          }
     }
}



void SplitString(const string& srcStr, vector<string>& vec, const string& separator)
{

    string::size_type posSubstringStart;

    string::size_type posSeparator;

    posSeparator= srcStr.find(separator);
    posSubstringStart= 0;
    while (string::npos != posSeparator)
    {
        vec.push_back(srcStr.substr(posSubstringStart, posSeparator- posSubstringStart));

        posSubstringStart= posSeparator+ separator.size();
        posSeparator= srcStr.find(separator, posSubstringStart);
    }

    if (posSubstringStart!= srcStr.length())
        vec.push_back(srcStr.substr(posSubstringStart));
}


long HexStringToLong(const string& Hex)
{
     return strtol(Hex.c_str(), NULL,16);
}

bool  ParsePTZCmd(const std::string & cmd_buf,  PtzCmd & Cmd)
{

    if(cmd_buf.size() != 16) {
            return false;
        }

        BYTE b1 = (BYTE)HexStringToLong( cmd_buf.substr(0, 2)  );
        BYTE b2 = (BYTE)HexStringToLong( cmd_buf.substr(2, 2));
        BYTE b3 = (BYTE)HexStringToLong( cmd_buf.substr(4, 2));
        BYTE b4 = (BYTE)HexStringToLong( cmd_buf.substr(6, 2));
        BYTE b5 = (BYTE)HexStringToLong( cmd_buf.substr(8, 2));
        BYTE b6 = (BYTE)HexStringToLong( cmd_buf.substr(10, 2));
        BYTE b7 = (BYTE)HexStringToLong( cmd_buf.substr(12, 2));
        BYTE b8 = (BYTE)HexStringToLong( cmd_buf.substr(14, 2));

		if(0xA5 != b1){
			return false;
		}

		if( (((b1>>4)+(b1&0x0F)+(b2>>4))%16) != b2 ){
			return false;
		}

		if(b8 != (b1+b2+b3+b4+b5+b6+b7)%256){
			return false;
		}

  
        Cmd.address = b3;
        Cmd.ctrlType = 1;
        PtzParam * param = &Cmd.param;

        if((b4 == 0x00) || (b4 == 0x40))
        {
            Cmd.cmdType = kPtzStop;
            Cmd.ctrlType = 0;
        }
        else if((b4&0xFE) == 0x40 && (b4&PTZ_CMD_RIGHT) == PTZ_CMD_RIGHT)
        {
            Cmd.cmdType = kPtzFocusFar;
            param->speedFocus = b5;
        }
        else if((b4&0xFD) == 0x40 && (b4&PTZ_CMD_LEFT) == PTZ_CMD_LEFT)
        {
            Cmd.cmdType = kPtzFocusNear;
            param->speedFocus = b5;
        }
        else if((b4&0xFB) == 0x40 && (b4&PTZ_CMD_DOWN) == PTZ_CMD_DOWN)
        {
            Cmd.cmdType = kPtzIRISOpen;
            param->speedIris = b6;
        }
        else if((b4&0xF7) == 0x40 && (b4&PTZ_CMD_UP) == PTZ_CMD_UP)
        {
            Cmd.cmdType = kPtzIRISClose;
            param->speedIris = b6;
        }
        else if((b4&0xFE) == 0x00 && (b4&PTZ_CMD_RIGHT) == PTZ_CMD_RIGHT)
        {
            Cmd.cmdType =  kPtzRight;
            param->ptzSpeed.SpeedPan = b5;
        }
        else if((b4&0xFD) == 0x00 && (b4&PTZ_CMD_LEFT) == PTZ_CMD_LEFT)
        {
            Cmd.cmdType = kPtzLeft;
            param->ptzSpeed.SpeedPan = b5;
        }
        else if((b4&0xFB) == 0x00 && (b4&PTZ_CMD_DOWN) == PTZ_CMD_DOWN)
        {
            Cmd.cmdType = kPtzDown;
            param->ptzSpeed.SpeedTilt = b6;
        }
        else if((b4&0xF7) == 0x00 && (b4&PTZ_CMD_UP) == PTZ_CMD_UP)
        {
            Cmd.cmdType = kPtzUp;
            param->ptzSpeed.SpeedTilt = b6;
        }
        else if((b4&0xEF) == 0x00 && (b4&PTZ_CMD_ZOOMOUT) == PTZ_CMD_ZOOMOUT)
        {
            Cmd.cmdType = kPtzZoomOut;
            param->speedZoom = b7>>4;
        }
        else if((b4&0xDF) == 0x00 && (b4&PTZ_CMD_ZOOMIN) == PTZ_CMD_ZOOMIN)
        {
            Cmd.cmdType = kPtzZoomIn;
            param->speedZoom = b7>>4;
        }
        else if((b4&0xF6) == 0x00 && (b4&PTZ_CMD_UP) == PTZ_CMD_UP && (b4&PTZ_CMD_RIGHT) == PTZ_CMD_RIGHT)
        {
            Cmd.cmdType = kPtzUpRight;
            param->ptzSpeed.SpeedPan = b5;
            param->ptzSpeed.SpeedTilt = b6;
        }
        else if((b4&0xF5) == 0x00 && (b4&PTZ_CMD_UP) == PTZ_CMD_UP && (b4&PTZ_CMD_LEFT) == PTZ_CMD_LEFT)
        {
            Cmd.cmdType = kPtzUpLeft;
            param->ptzSpeed.SpeedPan = b5;
            param->ptzSpeed.SpeedTilt = b6;
        }
        else if((b4&0xFA) == 0x00 && (b4&PTZ_CMD_DOWN) == PTZ_CMD_DOWN && (b4&PTZ_CMD_RIGHT) == PTZ_CMD_RIGHT)
        {
            Cmd.cmdType = kPtzDownRight;
            param->ptzSpeed.SpeedPan = b5;
            param->ptzSpeed.SpeedTilt = b6;
        }
        else if((b4&0xF9) == 0x00 && (b4&PTZ_CMD_DOWN) == PTZ_CMD_DOWN && (b4&PTZ_CMD_LEFT) == PTZ_CMD_LEFT)
        {
            Cmd.cmdType = kPtzDownLeft;
            param->ptzSpeed.SpeedPan = b5;
            param->ptzSpeed.SpeedTilt = b6;
        }

        else if( b4 == PTZ_CMD_ADD_POS)
        {
            Cmd.cmdType = kPtzPosAdd;
            param->posNo = b6;
        }
        else if( b4 == PTZ_CMD_CALL_POS )
        {
            Cmd.cmdType = kPtzPosCall;
            param->posNo = b6;
        }
        else if( b4 == PTZ_CMD_DEL_POS )
        {
            Cmd.cmdType =  kPtzPosDel;
             param->posNo = b6;
        }

        else if( b4 == PTZ_CMD_CRU_ADD )
        {
            Cmd.cmdType = kPtzCruAdd;
            param->cruparam.CruID = b5;
            param->cruparam.PosID = b6;
        }
        else if(  b4 == PTZ_CMD_CRU_DEL )
        {
            Cmd.cmdType = kPtzCruDel;
            param->cruparam.CruID = b5;
            param->cruparam.PosID = b6;
        }
        else if( b4 == PTZ_CMD_CRU_SPD )
        {
            Cmd.cmdType = kPtzCruSpeed;

            param->cruparam.CruID = b5;
            param->cruparam.PosID = b6;
            param->cruparam.SpdOrTime = b7>>4;
        }
        else if( b4 == PTZ_CMD_CRU_TIME )
        {
            Cmd.cmdType = kPtzCruTime;
            param->cruparam.CruID = b5;
            param->cruparam.PosID = b6;
             param->cruparam.SpdOrTime = b7>>4;
        }
        else if( b4 == PTZ_CMD_CRU_START )
        {
            Cmd.cmdType = kPtzCruStart;
            param->cruparam.CruID = b5;
        }
        else if( b4 == PTZ_CMD_SCAN )
        {
            if( b6 == 0x00 )
            {
                Cmd.cmdType = kPtzScanStart;
            }
            else if( b6 == 0x01 )
            {
                Cmd.cmdType = kPtzScanLeft;
            }
            else if( b6 == 0x02 )
            {
                Cmd.cmdType = kPtzScanRight;
            }
            else
            {
                Cmd.cmdType = kPtzUnkonw;
                return false;
            }

            param->scanParam.ScanID = b5;
            param->scanParam.BorderID = b6;
        }
        else if( b4 == PTZ_CMD_SCAN_SET )
        {
            Cmd.cmdType = kPtzScanSet;
            param->scanParam.ScanID = b5;
            param->scanParam.BorderID = b6;
            param->scanParam.Speed = b7>>4;
        }
        else if( b4 == 0x8c )
        {
            Cmd.cmdType =  kPtzAidOpen;
            param->aidNo = b5;
        }
        else if( b4 == 0x8d )
        {
            Cmd.cmdType = kPtzAidClose;
            param->aidNo = b5;
        }
        else
        {
            Cmd.cmdType = kPtzUnkonw;
            return false;
        }
        return true;
}



bool FormatPtzCmd(const PtzCmd & Cmd, char * buf)
{
    BYTE b1, b2, b3, b4, b5, b6, b7, b8;
    b1 = 0xA5;
    b2 = ((b1>>4) + (b1&0x0F) + 0x00 )%16;
    b3 = Cmd.address & 0x0F;

    const PtzParam * ptrParam = &Cmd.param;

    if(Cmd.ctrlType == PTZ_START && Cmd.cmdType !=   kPtzStop )
    {

        b4 = b5 = b6 = b7 = 0;

        switch (Cmd.cmdType)
        {
        case  kPtzLeft:
            {
                b4 |= PTZ_CMD_LEFT;
                b5 = ptrParam->ptzSpeed.SpeedPan;
                break;
            }
        case kPtzRight:
            {
                b4 |= PTZ_CMD_RIGHT;
                b5 = ptrParam->ptzSpeed.SpeedPan;
                break;
            }
        case kPtzUp:
            {
                b4 |= PTZ_CMD_UP;
                b6 = ptrParam->ptzSpeed.SpeedTilt;
                break;
            }
        case kPtzDown:
            {
                b4 |= PTZ_CMD_DOWN;
                b6 = ptrParam->ptzSpeed.SpeedTilt;
                break;
            }
        case kPtzDownLeft:
            {
                b4 |= (PTZ_CMD_LEFT | PTZ_CMD_DOWN);
                b5 = ptrParam->ptzSpeed.SpeedPan;
                b6 = ptrParam->ptzSpeed.SpeedTilt;
                break;
            }
        case kPtzDownRight:
            {
                b4 |= (PTZ_CMD_RIGHT | PTZ_CMD_DOWN);
                b5 = ptrParam->ptzSpeed.SpeedPan;
                b6 = ptrParam->ptzSpeed.SpeedTilt;
                break;
            }
        case kPtzUpLeft:
            {
                b4 |= (PTZ_CMD_LEFT | PTZ_CMD_UP);
                b5 = ptrParam->ptzSpeed.SpeedPan;
                b6 = ptrParam->ptzSpeed.SpeedTilt;
                break;
            }
        case kPtzUpRight:
            {
                b4 |= (PTZ_CMD_RIGHT | PTZ_CMD_UP);
                b5 = ptrParam->ptzSpeed.SpeedPan;
                b6 = ptrParam->ptzSpeed.SpeedTilt;
                break;
            }
        case kPtzZoomIn:
            {
                b4 |= PTZ_CMD_ZOOMIN;
                b7 = ptrParam->speedZoom;
                break;
            }
        case kPtzZoomOut:
            {
                b4 |= PTZ_CMD_ZOOMOUT;
                b7 = ptrParam->speedZoom;
                break;
            }
        case kPtzFocusNear:
            {
                b4 |= (PTZ_CMD_FI | PTZ_CMD_LEFT);
                b5 = ptrParam->speedFocus;
                break;
            }
        case kPtzFocusFar:
            {
                b4 |= (PTZ_CMD_FI | PTZ_CMD_RIGHT);
               b5 = ptrParam->speedFocus;
                break;
            }
        case  kPtzIRISClose:
            {
                b4 |= (PTZ_CMD_FI | PTZ_CMD_UP);
                b6 = ptrParam->speedIris;
                break;
            }
        case kPtzIRISOpen:
            {
                b4 |= (PTZ_CMD_FI | PTZ_CMD_DOWN);
                 b6 = ptrParam->speedIris;
                break;
            }
        case kPtzPosAdd:
            {
                b4 |= PTZ_CMD_ADD_POS;
                b6 = ptrParam->posNo;
                break;
            }
        case kPtzPosCall:
            {
                b4 |= PTZ_CMD_CALL_POS;
                b6 = ptrParam->posNo;
                break;
            }
        case kPtzPosDel:
            {
                b4 |= PTZ_CMD_DEL_POS;
                b6 = ptrParam->posNo;
                break;
            }
        case kPtzCruAdd:
            {
                b4 |= PTZ_CMD_CRU_ADD;
                b5 = ptrParam->cruparam.CruID;
                b6 = ptrParam->cruparam.PosID;
                break;
            }
        case kPtzCruDel:
            {
                b4 |= PTZ_CMD_CRU_DEL;
                b5 = ptrParam->cruparam.CruID;
                b6 = ptrParam->cruparam.PosID;
                break;
            }
        case kPtzCruSpeed:
            {
                b4 |= PTZ_CMD_CRU_SPD;
                b5 = ptrParam->cruparam.CruID;
                b6 = ptrParam->cruparam.PosID;
                b7 = ptrParam->cruparam.SpdOrTime;
                break;
            }
        case kPtzCruTime:
            {
                b4 |= PTZ_CMD_CRU_TIME;
                b5 = ptrParam->cruparam.CruID;
                b6 = ptrParam->cruparam.PosID;
                b7 = ptrParam->cruparam.SpdOrTime;
                break;
            }
        case kPtzCruStart:
            {
                b4 |= PTZ_CMD_CRU_START;
                 b5 = ptrParam->cruparam.CruID;
                break;
            }
        case kPtzScanStart:
            {
                b4 |= PTZ_CMD_SCAN;
                b5 = ptrParam->scanParam.ScanID;
                b6 = 0x00;
                break;
            }
        case kPtzScanLeft:
            {
                b4 |= PTZ_CMD_SCAN;
                b5 = ptrParam->scanParam.ScanID;
                b6 = 0x01;
                break;
            }
        case kPtzScanRight:
            {
                b4 |= PTZ_CMD_SCAN;
                b5 = ptrParam->scanParam.ScanID;
                b6 = 0x02;
                break;
            }
        case kPtzScanSet:
            {
                b4 |= PTZ_CMD_SCAN_SET;
                 b5 = ptrParam->scanParam.ScanID;
                b6 = ptrParam->scanParam.BorderID;
                b7 = ptrParam->scanParam.Speed;
                break;
            }
        case kPtzAidOpen:
            {
                b4 |= 0x8c;
                b5 = ptrParam->aidNo;
                break;
            }
        case kPtzAidClose:
            {
                b4 |= 0x8d;
                b5 = ptrParam->aidNo;
                break;
            }
        default:
            return false;
        }

    }
    else
    {
        if(kPtzFocusFar == Cmd.cmdType
            || kPtzFocusNear == Cmd.cmdType
            || kPtzIRISClose== Cmd.ctrlType
            || kPtzIRISOpen == Cmd.cmdType)
        {
              b4 = 0x40;
        }else{
              b4 = 0x00;
        }

        b5 = 0;
        b6 = 0;
        b7 = 0;
    }

    b7 = b7 << 4;
    b7 |= b3 >> 4;

    b8 = (b1 + b2 + b3 + b4 + b5 + b6 + b7) % 256;

    sprintf(buf, "%02X%02X%02X%02X%02X%02X%02X%02X",
        (int)b1, (int)b2, (int)b3, (int)b4, (int)b5, (int)b6, (int)b7, (int)b8);

    return true;
}





CGB28181XmlParser::CGB28181XmlParser()
{

}

int  CGB28181XmlParser::GetSn()
{
    return m_sn.Increment();
}

int  CGB28181XmlParser::GetControlType( CMarkupSTL* xml_parse)
{
    if (xml_parse->FindElem("PTZCmd"))
    {
        return kPtzControlProco;
    }
    if (xml_parse->FindElem("TeleBoot"))
    {
        return kTeleBootControlProco;
    }
    if (xml_parse->FindElem("DeviceUpgrade"))
    {
        return kDeviceUpgradeControlProco;
    }
    if (xml_parse->FindElem("RecordCmd"))
    {
        return kRecordControlProco;
    }
    if (xml_parse->FindElem("GuardCmd"))
    {
        return kGurdControlProco;
    }
    if (xml_parse->FindElem("AlarmCmd") )
    {
        return kAlarmResetControlProco;
    }
    if (xml_parse->FindElem("DragZoomIn"))
    {
        return kZoomInControlProco;
    }
    if (xml_parse->FindElem("DragZoomOut"))
    {
          return kZoomOutControlProco;
    }
    if (xml_parse->FindElem("IFameCmd") )
    {
         return kIFameControlProco;
    }
    if (xml_parse->FindElem("HomePosition") )
    {
         return kHomePositionControlProco;
    }
         return kControlProco;
}

int CGB28181XmlParser::GetProtocolType(const std::string& xml)
{
    CMarkupSTL MarkupStl;

    if(!MarkupStl.SetDoc(xml.c_str(),   xml.size())){
        return kInvailProco;
    }

    if (MarkupStl.FindElem("Control") && MarkupStl.IntoElem())
    {
		if (MarkupStl.FindElem("CmdType"))
		{
			if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeDeviceConfig)
			{
				return kDevConfigControlProco;
			}
			if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeDeviceControl)
			{
				return GetControlType(&MarkupStl);
			}
		}
    }
    else if (MarkupStl.FindElem("Query") && MarkupStl.IntoElem())
    {
        if (MarkupStl.FindElem("CmdType"))
        {
            if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeDeviceStatus)
            {
                 return kDevStatusQueryProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeCatalog)
            {
                  return kCatalogQueryProco;
            }
			else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeMobilePosition)
			{
				return kMobilePositionQueryProco;
			}
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeDeviceInfo)
            {
                //设备信息查询请求
                  return kDevInfoQueryProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeRecordInfo)
            {
                //文件目录检索请求
                  return kRecordIndexQueryProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeAlarm)
            {
                //报警查询
                   return kAlarmQueryProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeConfigDownload)
            {
                  return kConfigDownloadQueryProco;
            }
			else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypePresetQuery)
			{
				return kPresetQueryProco;
			}
        }
    }
    else if (MarkupStl.FindElem("Notify") && MarkupStl.IntoElem())
    {
        if (MarkupStl.FindElem("CmdType"))
        {
            if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeKeepalive)
            {
                //状态信息报送
                return kKeepaliveNotifyProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeAlarm)
            {
                //报警通知
                return kAlarmNotifyProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeCatalog)
            {
                //目录通知
                 return kCatalogNotifyProco;
            }
			else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeMediaStatus)
			{
				//报警通知
				return kMediaStatusNotifyProco;
			}
			else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeBroadcast)
			{
				//报警通知
				return kBroadcastNotifyProco;
			}
        }
    }
    else if (MarkupStl.FindElem("Response") && MarkupStl.IntoElem())
    {
        if (MarkupStl.FindElem("CmdType"))
        {
            if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeDeviceControl)
            {
                //设备控制应答
                 return kControlProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeAlarm)
            {
                //报警通知应答
                 return kAlarmResponseProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeCatalog)
            {
                 return kCatalogResponseProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeDeviceInfo)
            {
                //设备信息查询应答
                 return kDevInfoResponseProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeDeviceStatus)
            {
                //设备状态信息查询应答
                 return kDevStatusResponseProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeRecordInfo)
            {
                //文件目录检索应答
                return kRecordIndexResponseProco;
            }
            else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypeConfigDownload)
            {

                return  kConfingDownloadResponseProco;
            }
			else if (MarkupStl.GetData() == protocol::gb28181::kCmdTypePresetQuery)
			{
				return kPresetResponseProco;
			}
        }
    }
      return kInvailProco;
}


/*
void CGB28181XmlParser::PackBaseProcotol(const std::string&  request_type,  const std::string& cmd_type,  const std::string id , std::string &result)
{
    slothxml::base_proco_t proco;
    proco.CmdType = cmd_type;
    proco.DeviceID = id;
    proco.SN = m_sn.Increment();
    if (!slothxml::encode(proco, request_type.c_str(), result)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackBaseProcotol(const std::string &xml_str, BaseProcos* proco)
{
    slothxml::base_proco_t proco;
    if (!slothxml::decode(xml_str, input->request_type, proco)) {
            return false;
    }
    input->cmd_type =  proco.CmdType;
    input->device_id = proco.DeviceID;
    input->sn = proco.SN ;
    return true;
}
*/


int CGB28181XmlParser::PackDeviceRecordQuery(const RecordParam*  param, std::string &result)
{
    slothxml::record_query_t  record;

    record.DeviceID =  param->DeviceID;
    record.SN = m_sn.Increment();
    record.CmdType = protocol::gb28181::kCmdTypeRecordInfo;
    record.StartTime = param->StartTime;
    record.EndTime = param->EndTime;
	record.Type = "all";
    record.skip_Address();
    record.skip_RecorderID();
    record.skip_FilePath();
    record.skip_Secrecy();

    if (!slothxml::encode(record, QUERY, result)) {
            result = "";
    }
    return record.SN;
}

bool CGB28181XmlParser::UnPackDeviceRecordQuery(const std::string &xml_str, int& sn ,QueryParam* param )
{
    slothxml::record_query_t  query;

    if (!slothxml::decode(xml_str, QUERY, query)) {
            return false;
    }
    sn = query.SN;
    GBUtil::memcpy_safe( param->query_descri.record_index.Address, ROUTE_LEN, query.Address);
    GBUtil::memcpy_safe( param->query_descri.record_index.DeviceID, GB_ID_LEN, query.DeviceID);
    GBUtil::memcpy_safe( param->query_descri.record_index.StartTime , TIME_LEN, query.StartTime);
    GBUtil::memcpy_safe( param->query_descri.record_index.EndTime , TIME_LEN, query.EndTime);
    GBUtil::memcpy_safe( param->query_descri.record_index.Type , TYPE_LEN, query.Type);
	GBUtil::memcpy_safe( param->GBCode, GB_ID_LEN, query.DeviceID);
    param->query_descri.record_index.Secrecy = query.Secrecy;
     return true;
}


int CGB28181XmlParser::PackDeviceInfoQuery(const char* id, std::string &result)
{
      slothxml::base_proco_t proco;
      proco.DeviceID = id;
      proco.CmdType = protocol::gb28181::kCmdTypeDeviceInfo;
      proco.SN = m_sn.Increment();
      if (!slothxml::encode(proco, QUERY, result)) {
              result = "";
      }
      return proco.SN ;
}

bool CGB28181XmlParser::UnPackDeviceQuery( const std::string &xml_str, int &sn,  QueryParam& param  )
{
    switch(  param.type ) {

           case kDeviceCatalogQuery : {
                  return UnPackCatalogQuery(xml_str, sn,  &param);
           }

           case kDeviceRecordQuery : {
                  return UnPackDeviceRecordQuery(  xml_str, sn,  &param );
           }

            case kDeviceConfigQuery:{
				  return UnPackConfigDownloadQuery(xml_str, sn, &param);
            }
			case kDeviceInfoQuery:{
				  
			}
           default:{
                 std::string id;
                 UnPackQuery(  xml_str, sn,  id  );
                 GBUtil::memcpy_safe(param.GBCode, GB_ID_LEN, id  );
                 return true;
            }

   }
       return false;
}



bool CGB28181XmlParser::UnPackQuery(const std::string &xml_str, int &sn, std::string& id)
{
        slothxml::base_proco_t proco;
       if (!slothxml::decode(xml_str, QUERY, proco)) {
           return false;
       }
       sn =  proco.SN;
       id= proco.DeviceID;
        return true;
}

void CGB28181XmlParser::PackDeviceInfoResponse(int sn ,const DeviceInfo* device_info, std::string &result)
{
        slothxml::device_info_t  info;
		info.CmdType = protocol::gb28181::kCmdTypeDeviceInfo;
        info.SN = sn;
		info.DeviceID = device_info->GBCode;
        info.Firmware = device_info->firmware;
        info.DeviceName = device_info->device_name;
        info.DeviceType = device_info->device_type;
        info.Channel = device_info->video_channel_num;
        info.Manufacturer = device_info->manufacturer;
        info.Model = device_info->model;
        info.StringCode = device_info->string_code;
        info.Mac = device_info->mac_address;
        info.Line = device_info->line_id;
        info.CustomProtocolVersion = device_info->custom_protocol_version;
        info.DeviceCapabilityList.PositionCapability = device_info->device_capability_list.PositionCapability;
        info.DeviceCapabilityList.AlarmOutCapability = device_info->device_capability_list.AlarmOutCapability;
        info.DeviceCapabilityList.MICCapability = device_info->device_capability_list.MICCapability;
        info.DeviceCapabilityList.SpeakerCapability = device_info->device_capability_list.SpeakerCapability;
        info.DeviceCapabilityList.ImagingCapability = device_info->device_capability_list.ImagingCapability;
        info.DeviceCapabilityList.StreamPeramCapability = device_info->device_capability_list.StreamPeramCapability;
        info.DeviceCapabilityList.SupplyLightCapability = device_info->device_capability_list.SupplyLightCapability;
        info.ProtocolFunctionList.BroadcastCapability = device_info->protocol_function_list.BroadcastCapability;
        info.ProtocolFunctionList.FrameMirrorCapability = device_info->protocol_function_list.FrameMirrorCapability;
        info.ProtocolFunctionList.MultiStreamCapability = device_info->protocol_function_list.MultiStreamCapability;
        info.ProtocolFunctionList.OSDCapability = device_info->protocol_function_list.OSDCapability;
        info.ProtocolFunctionList.DeviceUpgradeCapability = device_info->protocol_function_list.DeviceUpgradeCapability;
        info.ProtocolFunctionList.AlarmCapability = device_info->protocol_function_list.AlarmCapability;
        info.ProtocolFunctionList.BroadcastTcpActiveCapability = device_info->protocol_function_list.BroadcastTcpActiveCapability;
        info.Result = device_info->result ? "OK": "ERROR" ;
        if (!slothxml::encode(info, RESPONSE, result)) {
                result = "";
        }
}

bool CGB28181XmlParser::UnPackDeviceInfoResponse(const std::string &xml_str, int& sn, DeviceInfo* device_info)
{
    slothxml::device_info_t  info;

    if (!slothxml::decode(xml_str, RESPONSE, info)) {
        return false;
    }

    GBUtil::memcpy_safe( device_info->GBCode, GB_ID_LEN , info.DeviceID);
    GBUtil::memcpy_safe( device_info->device_name, STR_LEN , info.DeviceName);
    GBUtil::memcpy_safe( device_info->device_type, TYPE_LEN , info.DeviceType);
    device_info->video_channel_num = info.Channel;
    GBUtil::memcpy_safe( device_info->firmware, STR_LEN , info.Firmware);
    GBUtil::memcpy_safe( device_info->manufacturer, STR_LEN , info.Manufacturer);
    GBUtil::memcpy_safe( device_info->model, STR_LEN , info.Model);
    GBUtil::memcpy_safe( device_info->string_code, STR_LEN , info.StringCode);
    GBUtil::memcpy_safe( device_info->mac_address, STR_LEN , info.Mac);
    GBUtil::memcpy_safe( device_info->line_id, STR_LEN , info.Line);
    GBUtil::memcpy_safe( device_info->custom_protocol_version, STR_LEN , info.CustomProtocolVersion);
    GBUtil::memcpy_safe( device_info->device_capability_list.PositionCapability,
                         STR_LEN,
                         info.DeviceCapabilityList.PositionCapability);
    GBUtil::memcpy_safe( device_info->device_capability_list.AlarmOutCapability,
                         STR_LEN,
                         info.DeviceCapabilityList.AlarmOutCapability);
    GBUtil::memcpy_safe( device_info->device_capability_list.MICCapability,
                         STR_LEN,
                         info.DeviceCapabilityList.MICCapability);
    GBUtil::memcpy_safe( device_info->device_capability_list.SpeakerCapability,
                         STR_LEN,
                         info.DeviceCapabilityList.SpeakerCapability);
    GBUtil::memcpy_safe( device_info->device_capability_list.ImagingCapability,
                         STR_LEN,
                         info.DeviceCapabilityList.ImagingCapability);
    GBUtil::memcpy_safe( device_info->device_capability_list.StreamPeramCapability,
                         STR_LEN,
                         info.DeviceCapabilityList.StreamPeramCapability);
    GBUtil::memcpy_safe( device_info->device_capability_list.SupplyLightCapability,
                         STR_LEN,
                         info.DeviceCapabilityList.SupplyLightCapability);
    GBUtil::memcpy_safe( device_info->protocol_function_list.BroadcastCapability,
                         STR_LEN,
                         info.ProtocolFunctionList.BroadcastCapability);
    GBUtil::memcpy_safe( device_info->protocol_function_list.FrameMirrorCapability,
                         STR_LEN,
                         info.ProtocolFunctionList.FrameMirrorCapability);
    GBUtil::memcpy_safe( device_info->protocol_function_list.MultiStreamCapability,
                         STR_LEN,
                         info.ProtocolFunctionList.MultiStreamCapability);
    GBUtil::memcpy_safe( device_info->protocol_function_list.OSDCapability,
                         STR_LEN,
                         info.ProtocolFunctionList.OSDCapability);
    GBUtil::memcpy_safe( device_info->protocol_function_list.DeviceUpgradeCapability,
                         STR_LEN,
                         info.ProtocolFunctionList.DeviceUpgradeCapability);
    GBUtil::memcpy_safe( device_info->protocol_function_list.AlarmCapability,
                         STR_LEN,
                         info.ProtocolFunctionList.AlarmCapability);
    GBUtil::memcpy_safe( device_info->protocol_function_list.BroadcastTcpActiveCapability,
                         STR_LEN,
                         info.ProtocolFunctionList.BroadcastTcpActiveCapability);

    if(  !info.Result.compare("OK") ) {
        device_info->result = true;
    }else{
        device_info->result = false;
    }
    sn = info.SN;
     return true;
}



void CGB28181XmlParser::PackCatalogResponse(int sn , int start ,int end, const DeviceCatalogList* param,  std::string &result)
{
        slothxml::catalog_response_t catalog;
        catalog.CmdType = protocol::gb28181::kCmdTypeCatalog;
        catalog.DeviceID = param->GBCode;
        catalog.SN = sn;
        catalog.SumNum = param->sum;

		if (catalog.SumNum != 0) {

			int position = start;
			for (; position <= end; position++) {
				slothxml::device_list_t device;
				device.Address = param->catalog[position].Address;
				device.Block = param->catalog[position].Block;
				device.Certifiable = param->catalog[position].Certifiable;
				device.CertNum = param->catalog[position].CertNum;
				device.CivilCode = param->catalog[position].CivilCode;
				device.DeviceID = param->catalog[position].DeviceID;
				device.EndTime = param->catalog[position].EndTime;
				device.ErrCode = param->catalog[position].ErrCode;
				device.IPAddress = param->catalog[position].IPAddress;
				device.Ower = param->catalog[position].Owner;
				device.Parental = param->catalog[position].Parental ? "1" : "0";
				device.ParentID = param->catalog[position].ParentID;
				device.StreamNumberList = param->catalog[position].StreamNumberList;
				device.SafetyWay = param->catalog[position].SafetyWay;
				device.RegisterWay = param->catalog[position].RegisterWay;
				device.Secrecy = param->catalog[position].Secrecy ? 1 : 0;
				device.Port = param->catalog[position].port;
				device.Password = param->catalog[position].Password;
				device.Manufacturer = param->catalog[position].Manufacturer;
				device.Model = param->catalog[position].Model;
				device.Name = param->catalog[position].Name;
				device.Event = param->catalog[position].Event;
				if (param->catalog[position].Status == 1) {
					device.Status = "ON";
				}
				else {
					device.Status = "OFF";
				}

				catalog.DeviceList.push_back(device);
			}
		}
		else {

			catalog.skip_DeviceList();

		}

        if(  !slothxml::encode(catalog, RESPONSE, result  )){
             result = "";
        }
}

bool CGB28181XmlParser::UnPackCatalogResponse(const std::string &xml_str, int& sn , int& SumList,  DeviceCatalogList* param)
{
    slothxml::catalog_response_t catalog;
    if(  !slothxml::decode(xml_str,RESPONSE, catalog  )){
        return false;
    }
    sn = catalog.SN;
    SumList = catalog.SumNum;
    GBUtil::memcpy_safe(param->GBCode, GB_ID_LEN,catalog.DeviceID   );
    param->catalog = (CatalogInfo*)malloc(sizeof(CatalogInfo)*catalog.DeviceList.size());
    memset(param->catalog,0,(sizeof(CatalogInfo)*catalog.DeviceList.size()) );
    param->sum = catalog.DeviceList.size();
    int i =0;
    for(;  i< (int)catalog.DeviceList.size();i++ )  {

        slothxml::device_list_t&   device =  catalog.DeviceList[i];

        param->catalog[i].Certifiable=  device.Certifiable;
        param->catalog[i].ErrCode = device.ErrCode;
        if( device.Status == "ON"){
              param->catalog[i].Status = true;
        }else{
              param->catalog[i].Status = false;
        }

        GBUtil::memcpy_safe(param->catalog[i].Address, COMM_LEN,  device.Address );
        GBUtil::memcpy_safe(param->catalog[i].Block, COMM_LEN,  device.Block );
        GBUtil::memcpy_safe(param->catalog[i].CertNum, COMM_LEN,  device.CertNum);
        GBUtil::memcpy_safe(param->catalog[i].CivilCode, GB_ID_LEN-1,  device.CivilCode);
        GBUtil::memcpy_safe(param->catalog[i].DeviceID, GB_ID_LEN-1,  device.DeviceID);
        GBUtil::memcpy_safe(param->catalog[i].Owner, COMM_LEN, device.Ower);
        GBUtil::memcpy_safe(param->catalog[i].ParentID, GB_ID_LEN-1,  device.ParentID);
        GBUtil::memcpy_safe(param->catalog[i].StreamNumberList, 32,  device.StreamNumberList);
        GBUtil::memcpy_safe(param->catalog[i].EndTime, TIME_LEN,  device.EndTime);
        GBUtil::memcpy_safe(param->catalog[i].IPAddress, IP_LEN,  device.IPAddress);
        GBUtil::memcpy_safe(param->catalog[i].Manufacturer, COMM_LEN,  device.Manufacturer);
        GBUtil::memcpy_safe(param->catalog[i].Model, COMM_LEN,  device.Model);
        GBUtil::memcpy_safe(param->catalog[i].Name, COMM_LEN,  device.Name);
        GBUtil::memcpy_safe(param->catalog[i].Password, COMM_LEN, device.Password);
        GBUtil::memcpy_safe(param->catalog[i].Event, EVENT_LEN, device.Event);
        ParseBoolValue(device.Parental, &param->catalog[i].Parental);
        param->catalog[i].RegisterWay = device.RegisterWay;
        param->catalog[i].SafetyWay = device.SafetyWay;
        param->catalog[i].Secrecy = (device.Secrecy != 0);
        param->catalog[i].port = device.Port;
    }


     return true;
}

void CGB28181XmlParser::PackCatalogNotify(int sn , int position,const DeviceCatalogList* info, std::string &result)
{
	slothxml::catalog_response_t catalog;
	catalog.CmdType = protocol::gb28181::kCmdTypeCatalog;
	catalog.DeviceID = info->GBCode;
	catalog.SN = sn;
	catalog.SumNum = info->sum;
	int num = position;
	for (int i = 0; i< num; i++)
	{
		slothxml::device_list_t   device;
		device.Address =  info->catalog[i].Address;
		device.Block=  info->catalog[i].Block;
		device.Certifiable=  info->catalog[i].Certifiable;
		device.CertNum=  info->catalog[i].CertNum;
		device.CivilCode=  info->catalog[i].CivilCode;
		device.DeviceID=  info->catalog[i].DeviceID;
		device.Name = info->catalog[i].Name;
		device.EndTime=  info->catalog[i].EndTime;
		device.ErrCode=  info->catalog[i].ErrCode;
		device.IPAddress=  info->catalog[i].IPAddress;
		device.Ower = info->catalog[i].Owner;
		device.Parental = info->catalog[i].Parental ? "1" : "0";
		device.ParentID = info->catalog[i].ParentID;
		device.StreamNumberList = info->catalog[i].StreamNumberList;
		device.SafetyWay = info->catalog[i].SafetyWay;
		device.RegisterWay = info->catalog[i].RegisterWay;
		device.Secrecy = info->catalog[i].Secrecy ? 1 : 0;
		device.Port = info->catalog[i].port;
		device.Password = info->catalog[i].Password;
		device.Manufacturer=  info->catalog[i].Manufacturer;
		device.Model=  info->catalog[i].Model;
		device.Name=  info->catalog[i].Name;
		device.Event = info->catalog[i].Event;
		if (info->catalog[i].Status == 1) {
			device.Status = "ON";
		}
		else {
			device.Status = "OFF";
		}
		catalog.DeviceList.push_back(device);
	}
	if(  !slothxml::encode(catalog, NOTIFY, result  )){
		result = "";
	}
}

bool CGB28181XmlParser::UnPackCatalogNotify(const std::string &xml_str, int& sn, std::string &code,  DeviceCatalogList* info)
{
	slothxml::catalog_response_t catalog;
	if(  !slothxml::decode(xml_str,NOTIFY, catalog  )){
		return false;
	}
	sn = catalog.SN;
	GBUtil::memcpy_safe(info->GBCode, GB_ID_LEN,catalog.DeviceID   );
	code = catalog.DeviceID;
	info->catalog = (CatalogInfo*)malloc(sizeof(CatalogInfo)*catalog.DeviceList.size());
	memset(info->catalog,0,(sizeof(CatalogInfo)*catalog.DeviceList.size()) );
	info->sum = catalog.DeviceList.size();
	int i =0;
	for(;  i< (int)catalog.DeviceList.size();i++ )  {

		slothxml::device_list_t&   device =  catalog.DeviceList[i];

		info->catalog[i].Certifiable=  device.Certifiable;
		info->catalog[i].ErrCode = device.ErrCode;
		if( device.Status == "ON"){
			info->catalog[i].Status = true;
		}else{
			info->catalog[i].Status = false;
		}

		GBUtil::memcpy_safe(info->catalog[i].Address, COMM_LEN,  device.Address );
		GBUtil::memcpy_safe(info->catalog[i].Block, COMM_LEN,  device.Block );
		GBUtil::memcpy_safe(info->catalog[i].CertNum, COMM_LEN,  device.CertNum);
		GBUtil::memcpy_safe(info->catalog[i].CivilCode, GB_ID_LEN-1,  device.CivilCode);
		GBUtil::memcpy_safe(info->catalog[i].DeviceID, GB_ID_LEN-1,  device.DeviceID);
		GBUtil::memcpy_safe(info->catalog[i].Owner, COMM_LEN, device.Ower);
		GBUtil::memcpy_safe(info->catalog[i].ParentID, GB_ID_LEN-1,  device.ParentID);
		GBUtil::memcpy_safe(info->catalog[i].StreamNumberList, 32,  device.StreamNumberList);
		GBUtil::memcpy_safe(info->catalog[i].EndTime, TIME_LEN,  device.EndTime);
		GBUtil::memcpy_safe(info->catalog[i].IPAddress, IP_LEN,  device.IPAddress);
		GBUtil::memcpy_safe(info->catalog[i].Manufacturer, COMM_LEN,  device.Manufacturer);
		GBUtil::memcpy_safe(info->catalog[i].Model, COMM_LEN,  device.Model);
		GBUtil::memcpy_safe(info->catalog[i].Name, COMM_LEN,  device.Name);
		GBUtil::memcpy_safe(info->catalog[i].Password, COMM_LEN, device.Password);
		GBUtil::memcpy_safe(info->catalog[i].Event, EVENT_LEN, device.Event);
		ParseBoolValue(device.Parental, &info->catalog[i].Parental);
		info->catalog[i].RegisterWay = device.RegisterWay;
		info->catalog[i].SafetyWay = device.SafetyWay;
		info->catalog[i].Secrecy = (device.Secrecy != 0);
		info->catalog[i].port = device.Port;

	}
	
	return true;
}


int  CGB28181XmlParser::PackCatalogQuery(const CatalogQueryParam* param, std::string &result)
{
          slothxml::catalog_query_t catalog;

          catalog.CmdType = protocol::gb28181::kCmdTypeCatalog;
          catalog.DeviceID = param->DeviceID;
          catalog.SN = m_sn.Increment();

          if(  param->time.EndTime[0] >0  ){
                 catalog.EndTime =  param->time.EndTime;
                 catalog.StartTime =  param->time.StartTime;
          }else{
              catalog.skip_EndTime();
              catalog.skip_StartTime();
          }

          if (!slothxml::encode(catalog, QUERY, result)) {
                  result = "";
          }
              return catalog.SN ;
}

bool CGB28181XmlParser::UnPackCatalogQuery( const std::string &xml_str, int& sn ,QueryParam* param)
{
          slothxml::catalog_query_t catalog;

          if (!slothxml::decode(xml_str, QUERY, catalog)) {
              return false;
          }

          if( catalog.xml_has_EndTime() ){

              GBUtil::memcpy_safe( param->query_descri.catalog_period.EndTime, TIME_LEN , catalog.EndTime);
          }
          if( catalog.xml_has_StartTime() ){
               GBUtil::memcpy_safe( param->query_descri.catalog_period.StartTime,TIME_LEN, catalog.StartTime);
          }
               GBUtil::memcpy_safe(param->GBCode,  GB_ID_LEN,  catalog.DeviceID);
               sn = catalog.SN;
                return true;
}

void CGB28181XmlParser::PackKeepalive(const KeepaliveInfo*  list,  std::string& result)
{
    slothxml::keepalive_t device_status;

    device_status.DeviceID = m_local_code;
    device_status.CmdType = protocol::gb28181::kCmdTypeKeepalive;
    device_status.Status =  list->status ? "OK" : "ERROR" ;
    device_status.SN = m_sn.Increment();

    if( IsGb2016OrLater(m_version) && list && list->Num > 0  ) {
        int i = 0;
        for(; i< (int)list->Num;i++  ) {
              device_status.Info.DeviceID.push_back( list->fault_device[i].DeviceID );
        }
    }
    else {
        device_status.skip_Info();
    }

    if (!slothxml::encode(device_status, NOTIFY, result)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackNotifyInfo(const std::string &xml_str,  int& sn,  NotifyInfo&  info)
{
    std::string code;
    bool res = false;
     switch( (int)info.type ) {
           case kKeepaliveNotify:
            {
                 res = UnPackKeepalive( xml_str, code,  &(info.notify_message.keepalive) ); break;
            }
          case kAlarmNotify:
            {
                res = UnPackAlarmNotify(xml_str,  sn, code, &(info.notify_message.alarm_notify)); break;
            }

          case kMobilePositionNotify:
            {
                res = UnPackMobilePositionNotify( xml_str,  sn, code, &(info.notify_message.moblie_notify)  );break;
            }

         case kCatalogNotify:
           {
              res = UnPackCatalogNotify( xml_str,  sn, code, &(info.notify_message.catalog_list)  );

			  break;
           }

		 case kMediaStatusNotify:
		 {
			 res = UnPackMediaStatusNotify(xml_str, code, info.notify_message.media_status); break;
		 }

		 case kBroadcastNotify:
		 {
			 res = UnPackBroadcastNotify(xml_str, sn, code, info.notify_message.broadcast_info); break;
		 }

     }

     if(res){
         GBUtil::memcpy_safe(info.GBCode, GB_ID_LEN, code);
     }
     return res;
}



bool CGB28181XmlParser::UnPackKeepalive(const std::string &xml_str, std::string& code, KeepaliveInfo*  list)
{
    slothxml::keepalive_t device_status;
    if (!slothxml::decode(xml_str,NOTIFY, device_status)) {
        return false;
    }

    if( device_status.Status == "OK") {
        list->status =  1;
    }
    else{
        list->status =  0;
    }
   code = device_status.DeviceID;
   int num =  device_status.Info.DeviceID.size();
   if(num >= 1) {
         list->fault_device = (FaultDevice*)malloc( sizeof(FaultDevice)* num);
   }
    if(  IsGb2016OrLater(m_version)   && num >= 1 && list->fault_device ){
        list->Num = device_status.Info.DeviceID.size();
        int i=0;
        for(;i< (int)list->Num; i++) {
            GBUtil::memcpy_safe( list->fault_device[i].DeviceID, GB_ID_LEN,  device_status.Info.DeviceID[i]);
        }
    }

       return true;
}

int CGB28181XmlParser::PackConfigDownloadQuery(const ConfigDownloadQuery* param, std::string &result)
{
    slothxml::config_query_t query;
    std::vector<std::string>  configtype;
    const unsigned int maxTypeNum = sizeof(param->Type) / sizeof(param->Type[0]);
    const unsigned int queryCount = (param->Num < maxTypeNum) ? param->Num : maxTypeNum;
    for (unsigned int i = 0; i < queryCount; ++i) {
        const char* typeName = protocol::gb28181::GetGbConfigTypeName(param->Type[i]);
        if (typeName != NULL) {
            configtype.push_back(typeName);
        }
    }
    std::string buffer;
    BuildString(configtype, buffer, "/");

    query.ConfigType = buffer;
    query.DeviceID = param->DeviceID;
    query.SN = m_sn.Increment();

    if (!slothxml::encode( query, QUERY, result)) {
            result = "";
    }
      return query.SN;
}

bool CGB28181XmlParser::UnPackConfigDownloadQuery(const std::string &xml_str, int& sn, QueryParam* param)
{

    slothxml::config_query_t query;
    if (!slothxml::decode( xml_str, QUERY, query)) {
            return false;
    }
    std::vector<std::string>  configtype;
    SplitString(query.ConfigType, configtype, "/");
    const unsigned int maxTypeNum =
        sizeof(param->query_descri.config_param.Type) / sizeof(param->query_descri.config_param.Type[0]);
    const unsigned int queryCount = (configtype.size() < maxTypeNum) ?
        (unsigned int)configtype.size() : maxTypeNum;
    param->query_descri.config_param.Num = queryCount;
    memset(param->query_descri.config_param.Type, 0, sizeof(param->query_descri.config_param.Type));
	for (unsigned int i = 0; i < queryCount; i++)
	{
		param->query_descri.config_param.Type[i] =
            protocol::gb28181::ParseGbConfigTypeName(configtype[i]);
	}


    GBUtil::memcpy_safe( param->GBCode, GB_ID_LEN, query.DeviceID );
    sn = query.SN ;
    return true;
}


void CGB28181XmlParser::PackConfigDownloadResponse(int sn,const DeviceConfigDownload* param, std::string &result)
{
	slothxml::configdownload_response_t config;
    std::string extraConfigXml;
	config.DeviceID = param->GBCode;
	config.CmdType = protocol::gb28181::kCmdTypeConfigDownload;
	config.SN = sn;
	config.Result = "1";

	for (int i = 0; i < (int)param->Num; i++)
	{
		switch(param->CfgParam[i].CfgType)
		{
			case kBasicParam:
				{
					char expireBuf[32] = {0};
					config.BasicParam.Name = param->CfgParam[i].UnionCfgParam.CfgBasic.DeviceName;
					sprintf(expireBuf,"%d",param->CfgParam[i].UnionCfgParam.CfgBasic.Expiration);
					config.BasicParam.Expiration = expireBuf;
					config.BasicParam.HeartBeatCount = param->CfgParam[i].UnionCfgParam.CfgBasic.HeartBeatcount;
					config.BasicParam.HeartBeatInterval = param->CfgParam[i].UnionCfgParam.CfgBasic.HeartBeatInterval;
				}
				break;
			case kVideoParamOpt:
				config.VideoParamOpt.DownloadSpeed = "6";
				config.VideoParamOpt.Resolution = param->CfgParam[i].UnionCfgParam.CfgVideoOpt.Resolution;
				config.VideoParamOpt.ImageFlip = param->CfgParam[i].UnionCfgParam.CfgVideoOpt.ImageFlip;
				break;
			case kVideoParamAttribute:
				extraConfigXml += BuildVideoParamAttributeXml(
                    param->CfgParam[i].UnionCfgParam.CfgVideoAttr);
				break;
			case kFrameMirrorConfig:
				extraConfigXml += BuildFrameMirrorXml(
                    param->CfgParam[i].UnionCfgParam.FrameMirror);
				break;
			case kSVACEncodeConfig:
				{
					config.SVACEncodeConfig.ROIParam.ROIFlag = param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.ROIFlag;
					config.SVACEncodeConfig.ROIParam.ROINumber = param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.ROINum;
					for (int j = 0; j < (int)(param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.ROINum); j++)
					{
						slothxml::roi_area_description_list_t roiDes;
						roiDes.BottomRight = param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.aryParam[j].BottomRight;
						roiDes.ROIQP = param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.aryParam[j].ROIQP;
					    roiDes.ROISeq = param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.aryParam[j].ROISeq;
					    roiDes.TopLeft = param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.aryParam[j].TopLeft;
						config.SVACEncodeConfig.ROIParam.ROIAreaDescriptionList.push_back(roiDes);
					}
					config.SVACEncodeConfig.ROIParam.BackGroundQP = param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.BackGroundQP;
					config.SVACEncodeConfig.ROIParam.BackGroundSkipFlag = param->CfgParam[i].UnionCfgParam.CfgEncode.stuRoiParam.BackGroundSkipFlag;
				break;
				}
			case kSVACDecodeConfig:
				break;
            case kOsdConfig:
                extraConfigXml += BuildOsdConfigXml(param->CfgParam[i].UnionCfgParam.CfgOsd);
                break;
			default:
				break;
		}
	}

	bool BasicParam = false;
	bool VideoParamOpt = false;
	bool SVACEncodeConfig = false;
	for (int j = 0; j < (int)param->Num; j++)
	{
		//ConfigType type = param->CfgParam[j].CfgType;
		if (param->CfgParam[j].CfgType == kBasicParam)
		{
			BasicParam = true;
		}
		if (param->CfgParam[j].CfgType == kVideoParamOpt)
		{
			VideoParamOpt = true;
		}
		if (param->CfgParam[j].CfgType == kSVACEncodeConfig)
		{
			SVACEncodeConfig = true;
		}
	}
	if(!BasicParam)
	{
		config.skip_BasicParam();
	}
	if (!VideoParamOpt)
	{
		config.skip_VideoParamOpt();
	}
	if (!SVACEncodeConfig)
	{
		config.skip_SVACEncodeConfig();
	}
	config.skip_SVACDecodeConfig();
	config.SVACEncodeConfig.skip_SurveillanceParam();
	config.SVACEncodeConfig.skip_SVCParam();
	config.SVACEncodeConfig.skip_AudioParam();

	if (!slothxml::encode(config, RESPONSE, result)) {
		result = "";
	}

    if (!result.empty() && !extraConfigXml.empty() &&
        !InjectXmlBeforeClosingTag(result, extraConfigXml, "</Response>")) {
        result = "";
    }
}

bool CGB28181XmlParser::UnPackConfigDownloadResponse(const std::string &xml_str, int& sn, QueryParam* param)
{
	return true;
}

int CGB28181XmlParser::PackDeviceStatusQuery(const char* device_id, std::string &result)
{
    slothxml::base_proco_t proco;
    proco.DeviceID = device_id;
    proco.CmdType = protocol::gb28181::kCmdTypeDeviceStatus;
    proco.SN = m_sn.Increment();
    if (!slothxml::encode(proco, QUERY, result)) {
            result = "";
    }
    return proco.SN;
}

void CGB28181XmlParser::PackDevicePresetResponse(int sn , const PresetInfo* info, std::string &result)
{
    slothxml::preset_reponse_t preset;
    preset.DeviceID = info->DeviceID;
    preset.CmdType = protocol::gb28181::kCmdTypePresetQuery;
    preset.SN = sn;

   int i = 0;
   for(  ;i< (int)info->PersetListNum;i++) {
        slothxml::preset_list_t  one;
        one.PresetID = info->PresetList[i].PresetID;
        one.PresetName = info->PresetList[i].PresetName;
        preset.PresetList.push_back(one);
   }


    if (!slothxml::encode(preset,   RESPONSE, result)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackDevicePresetResponse(const std::string &xml_str, int& sn, PresetInfo* info)
{
    slothxml::preset_reponse_t preset;

    if (!slothxml::decode(xml_str,   RESPONSE, preset)) {
            return false;
    }
   GBUtil::memcpy_safe( info->DeviceID ,GB_ID_LEN, preset.DeviceID);

   int i = 0;
   info->PersetListNum = preset.PresetList.size();
   info->PresetList = (PresetParam*)malloc(  sizeof(PresetParam)* info->PersetListNum  );
   memset(info->PresetList,0,sizeof(PresetParam)* info->PersetListNum);
   for(  ; i< (int)preset.PresetList.size(); i++) {
        GBUtil::memcpy_safe( info->PresetList[i].PresetID, STR_LEN, preset.PresetList[i].PresetID);
        GBUtil::memcpy_safe( info->PresetList[i].PresetName, STR_LEN, preset.PresetList[i].PresetName);
   }
   sn = preset.SN;
   return true;
}

void CGB28181XmlParser::PackDeviceRecordIndexResponseEx(int sn,
                                                        const char* device_id,
                                                        unsigned int total_num,
                                                        const RecordParam* record_list,
                                                        unsigned int record_num,
                                                        std::string &result)
{
    slothxml::record_index_response_t record;
    record.DeviceID = device_id ? device_id : "";
    record.CmdType = protocol::gb28181::kCmdTypeRecordInfo;
    record.SN = sn;
    record.SumNum = total_num;
   unsigned int i = 0;
   for(  ;i < record_num; ++i) {
        slothxml::record_list_t  one;
        one.DeviceID = record_list[i].DeviceID;
        one.EndTime = record_list[i].EndTime;
        one.FilePath = record_list[i].FilePath;
        if (one.FilePath.empty()) {
            one.skip_FilePath();
        }
        one.Address = record_list[i].Address;
        if (one.Address.empty()) {
            one.skip_Address();
        }
        one.Name = record_list[i].Name;
        one.Secrecy = record_list[i].Secrecy;
        one.StartTime = record_list[i].StartTime;
        one.Type = record_list[i].Type;
        record.RecordList.push_back(one);
   }

   if (record_num == 0) {
	   record.skip_RecordList();
   }

    if (!slothxml::encode(record,   RESPONSE, result)) {
            result = "";
    }
}

void CGB28181XmlParser::PackDeviceRecordIndexResponse(int sn ,const RecordIndex* index, std::string &result)
{
    if (index == NULL) {
        result = "";
        return;
    }

    PackDeviceRecordIndexResponseEx(sn,
                                    index->GBCode,
                                    index->Num,
                                    index->record_list,
                                    index->Num,
                                    result);
}

bool CGB28181XmlParser::UnPackDeviceRecordIndexResponse(const std::string &xml_str, int& sn, int& sum,   RecordIndex* index)
{
    slothxml::record_index_response_t record;

    if(!slothxml::decode(xml_str, RESPONSE, record )) {
        return false;
    }
   sum = record.SumNum;
   sn = record.SN;
   GBUtil::memcpy_safe(index->GBCode, GB_ID_LEN, record.DeviceID );

   if (record.RecordList.size() == 0) {
	   index->Num = 0;
	   index->record_list = NULL;
	   return true;
   }

  int i = 0;
  index->Num = record.RecordList.size();
  index->record_list = (RecordParam*)malloc(  sizeof(RecordParam)* index->Num  );
  memset(index->record_list,0,(sizeof(RecordParam)* index->Num) );
   for(  ;i< (int)index->Num; i++) {
        GBUtil::memcpy_safe(index->record_list[i].Address, ROUTE_LEN, record.RecordList[i].Address);
        GBUtil::memcpy_safe(index->record_list[i].DeviceID, GB_ID_LEN, record.RecordList[i].DeviceID);
        GBUtil::memcpy_safe(index->record_list[i].StartTime, TIME_LEN, record.RecordList[i].StartTime);
        GBUtil::memcpy_safe(index->record_list[i].EndTime, TIME_LEN, record.RecordList[i].EndTime);
        GBUtil::memcpy_safe(index->record_list[i].FilePath, ROUTE_LEN, record.RecordList[i].FilePath);
        GBUtil::memcpy_safe(index->record_list[i].Type, ROUTE_LEN, record.RecordList[i].Type);
        index->record_list[i].Secrecy = record.RecordList[i].Secrecy;
   }

    return true;
}



void CGB28181XmlParser::PackDeviceStatusResponse(int sn , const DeviceStatus* device_status, std::string &result)
{
      slothxml::device_status_response_t response;
      response.DeviceID = device_status->GBCode;
	  response.SN = sn;
      response.DeviceTime = device_status->DevDateTime;
      response.Encode = device_status->Encode ? "ON" : "OFF";
      response.Reason = device_status->ErrReason;
      response.Online = device_status->OnLine  ?  "ONLINE" : "OFFLINE";
      response.Status = device_status->StatusOK ? "OK" :"ERROR";
      response.Record = device_status->Record ? "ON" : "OFF";
	  response.Result = device_status->Result ? "OK" : "ERROR";

     if(  !slothxml::encode(response,RESPONSE,result)){
         result = "";
     }
}

bool CGB28181XmlParser::UnPackDeviceStatusResponse(const std::string &xml_str,  int& sn , DeviceStatus* device_status)
{
    slothxml::device_status_response_t response;
    if(!slothxml::decode(xml_str,RESPONSE,response )){
        return false;
    }

    GBUtil::memcpy_safe(device_status->GBCode, GB_ID_LEN, response.DeviceID );
    GBUtil::memcpy_safe(device_status->DevDateTime,TIME_LEN ,response.DeviceTime);
    GBUtil::memcpy_safe(device_status->ErrReason,STR_LEN, response.Reason);

    if( response.Encode == "ON"){
        device_status->Encode = true;
    }else{
       device_status->Encode = false;
    }

    if( response.Online == "ONLINE"){
        device_status->OnLine = true;
    }else{
       device_status->OnLine = false;
    }

    if( response.Status == "OK"){
            device_status->StatusOK = true;
    }else{
           device_status->StatusOK  = false;
    }


    if( response.Record == "ON"){
           device_status->Record = true;
       }else{
          device_status->Record = false;
      }
       sn = response.SN;
       return true;
}



int CGB28181XmlParser::PackQueryDevicePresetInfo(const char* device_id, std::string &result)
{
    slothxml::base_proco_t proco;
    proco.DeviceID = device_id;
    proco.CmdType = protocol::gb28181::kCmdTypePresetQuery;
    proco.SN = m_sn.Increment();
    if (!slothxml::encode(proco, QUERY, result)) {
            result = "";
    }
    return proco.SN;
}


void CGB28181XmlParser::PackAlarmResetControl(const char* device_id, const AlarmResetInfo* info,  std::string &result)
{
    slothxml::alarm_reset_control_t     control;
    control.CmdType = protocol::gb28181::kCmdTypeDeviceControl;
    control.SN = m_sn.Increment();
    control.DeviceID = device_id;
    control.AlarmCmd =  "ResetAlarm";

    if( m_version == kGB2016Version){
          control.Info.AlarmMethod = info->AlarmMethod;
          control.Info.AlarmType = info->AlarmType;
    }else{
         control.skip_Info();
    }

    if (!slothxml::encode(control, "Control", result)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackAlarmResetControl(const std::string &xml_str, int& sn , DevControlCmd& cmd)
{
    slothxml::alarm_reset_control_t     control;
    if (!slothxml::decode(xml_str, "Control", control)) {
        return false;
    }

    sn = control.SN;
    if( m_version == kGB2016Version && control.xml_has_Info()) {
         GBUtil::memcpy_safe(cmd.control_param.alarm_reset_cmd.AlarmMethod, STR_LEN, control.Info.AlarmMethod   );
         GBUtil::memcpy_safe(cmd.control_param.alarm_reset_cmd.AlarmType, STR_LEN, control.Info.AlarmType   );
    }
    GBUtil::memcpy_safe(cmd.GBCode ,GB_ID_LEN, control.DeviceID  );
       return true;
}

void CGB28181XmlParser::PackGurdControl(const char* device_id,bool opt, std::string &result)
{
    slothxml::guard_control_t     control;
    control.CmdType = protocol::gb28181::kCmdTypeDeviceControl;
    control.SN = m_sn.Increment();
    control.DeviceID = device_id;
    control.GuardCmd =  opt ? "SetGuard"  :  "ResetGuard";
    if (!slothxml::encode(control, "Control", result)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackGurdControl(const std::string &xml_str, int& sn , DevControlCmd& cmd)
{
    slothxml::guard_control_t     control;
    if (!slothxml::decode(xml_str, "Control", control)) {
        return false;
    }
    sn = control.SN;
    GBUtil::memcpy_safe( cmd.GBCode, GB_ID_LEN,   control.DeviceID  );
    if(  control.GuardCmd == "SetGuard" ){
          cmd.control_param.gurd_cmd = true;
    }
	else if (control.GuardCmd == "ResetGuard")
	{
        cmd.control_param.gurd_cmd = false;
    }
    return true;
}


void CGB28181XmlParser::PackRecordControl(const char* device_id, bool start, std::string &result)
{
     slothxml::record_control_t control;
     control.CmdType = protocol::gb28181::kCmdTypeDeviceControl;
     control.DeviceID = device_id;
     control.SN = m_sn.Increment();
     control.RecordCmd =  start ? "Record" : "StopRecord";
     if (!slothxml::encode(control, "Control", result)) {
          result = "";
     }
}

bool CGB28181XmlParser::UnPackRecordControl(const std::string &xml_str, int& sn , DevControlCmd& cmd)
{
    slothxml::record_control_t     control;

    if (!slothxml::decode(xml_str, "Control", control)) {
        return false;
    }
    sn = control.SN;
    GBUtil::memcpy_safe( cmd.GBCode, GB_ID_LEN,   control.DeviceID  );
    if(  control.RecordCmd == "Record" ){
         cmd.control_param.record_cmd = true;
    }else if( control.RecordCmd ==  "StopRecord" ){
         cmd.control_param.record_cmd = false;
    }
      return true;
}


void CGB28181XmlParser::PackZoomControl(const char* device_id, const ZoomCmd* cmd,  std::string &result)
{
    slothxml::zoom_control_t     control;

    control.CmdType = protocol::gb28181::kCmdTypeDeviceControl;
    control.DeviceID = device_id;
    control.SN = m_sn.Increment();
    if( cmd->in  ) {
         control.DragZoomIn.Width = cmd->Width;
         control.DragZoomIn.Length = cmd->Length;
         control.DragZoomIn.LengthX = cmd->LengthX;
         control.DragZoomIn.LengthY = cmd->LengthY;
         control.DragZoomIn.MidPointX = cmd->MidPointX;
         control.DragZoomIn.MidPointY = cmd->MidPointY;
         control.skip_DragZoomOut();
    }else{
        control.DragZoomOut.Width = cmd->Width;
        control.DragZoomOut.Length = cmd->Length;
        control.DragZoomOut.LengthX = cmd->LengthX;
        control.DragZoomOut.LengthY = cmd->LengthY;
        control.DragZoomOut.MidPointX = cmd->MidPointX;
        control.DragZoomOut.MidPointY = cmd->MidPointY;
        control.skip_DragZoomIn();
    }


    if (!slothxml::encode(control, "Control", result)) {
         result = "";
    }
}

bool CGB28181XmlParser::UnPackZoomControl(const std::string &xml_str, int& sn , DevControlCmd& cmd)
{
    slothxml::zoom_control_t     control;

    if (!slothxml::decode(xml_str, "Control", control)) {
        return false;
    }
    sn = control.SN;
    GBUtil::memcpy_safe(cmd.GBCode ,GB_ID_LEN,  control.DeviceID  );

    slothxml::drag_t  drag;

    if(control.xml_has_DragZoomIn() )  {
        cmd.control_param.zoom_cmd.in = true;
        memcpy(&drag,&(control.DragZoomIn),sizeof(drag));
    }else{
        cmd.control_param.zoom_cmd.in = false;
        memcpy(&drag, &(control.DragZoomOut),sizeof(drag));
   }

    cmd.control_param.zoom_cmd.Length = drag.Length;
    cmd.control_param.zoom_cmd.LengthX = drag.LengthX;
    cmd.control_param.zoom_cmd.LengthY = drag.LengthY;
    cmd.control_param.zoom_cmd.MidPointX = drag.MidPointX;
    cmd.control_param.zoom_cmd.MidPointY = drag.MidPointY;
    cmd.control_param.zoom_cmd.Width = drag.Width;

    return true;
}


void CGB28181XmlParser::PackPTZControl(const char* device_id, const PtzCmd* cmd, std::string &result)
{
    slothxml::ptz_control_t  ptz;
    ptz.CmdType = protocol::gb28181::kCmdTypeDeviceControl;
    ptz.SN = m_sn.Increment();
    ptz.DeviceID = device_id;
    char temp[128] = {0};
    FormatPtzCmd(*cmd ,temp);
    ptz.PTZCmd = temp;
    if (!slothxml::encode(ptz, "Control", result)) {
            result = "";
    }

}

bool CGB28181XmlParser::UnPackPTZControl(const std::string &xml_str, int& sn, DevControlCmd& cmd)
{
    slothxml::ptz_control_t  ptz;
    if (!slothxml::decode(xml_str, "Control", ptz)) {
        return false;
    }
    sn = ptz.SN;
    ParsePTZCmd(  ptz.PTZCmd, cmd.control_param.ptz_cmd    );
    GBUtil::memcpy_safe(cmd.GBCode ,GB_ID_LEN, ptz.DeviceID  );
    return false;
}



void CGB28181XmlParser::PackAlarmSubcribe(const AlarmSubcribeInfo* info,std::string &result)
{
    slothxml::alarm_sub_t     subcribe;
    subcribe.CmdType = protocol::gb28181::kCmdTypeAlarm;
    subcribe.SN = m_sn.Increment();
    subcribe.DeviceID = info->DeivceID;

    subcribe.AlarmMethod = info->Method;
	subcribe.AlarmType = info->AlarmType;
    subcribe.EndAlarmPriority = info->EndPriority;
    subcribe.StartAlarmPriority = info->StartPriority;
    subcribe.EndTime = info->EndTime;
    subcribe.StartTime = info->StartTime;

    if (!slothxml::encode(subcribe, QUERY, result)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackAlarmSubcribe(const std::string &xml_str, int& sn, std::string& code, AlarmSubcribeInfo* info)
{
    slothxml::alarm_sub_t     subcribe;
    if (!slothxml::decode(xml_str, QUERY, subcribe)) {
            return false;
    }
    sn = subcribe.SN;
    code = subcribe.DeviceID;
    info->Method =subcribe.AlarmMethod;
	info->AlarmType = subcribe.AlarmType;
    info->EndPriority = subcribe.EndAlarmPriority;
    info->StartPriority = subcribe.StartAlarmPriority;
    GBUtil::memcpy_safe(info->EndTime ,TIME_LEN,  subcribe.EndTime);
    GBUtil::memcpy_safe( info->StartTime ,TIME_LEN, subcribe.StartTime);
    return true;
}

bool CGB28181XmlParser::PackMediaStatusNotify(const char* id, int NotifyType, std::string &result)
{
     slothxml::media_status_notify_t notify;
     notify.CmdType = protocol::gb28181::kCmdTypeMediaStatus;
     notify.SN = m_sn.Increment();
     notify.DeviceID = id;
	 notify.NotifyType = NotifyType;
     if(!slothxml::encode(notify,NOTIFY, result) ){
          result = "";
     }
	 return true;
}

bool CGB28181XmlParser::UnPackMediaStatusNotify(const std::string &xml_str, std::string code,MediaStatusNotify& meida_status)
{
    slothxml::media_status_notify_t notify;
    if( !slothxml::decode(xml_str,NOTIFY, notify) ){
         return false;
    }
    code  = notify.DeviceID;
	meida_status.type = notify.NotifyType;
    return true;
}

bool CGB28181XmlParser::PackDeviceUpgradeResultNotify(const char* device_id, const char* session_id, const char* firmware, bool result, const char* description, std::string &output)
{
     slothxml::device_upgrade_result_notify_t notify;
     notify.CmdType = protocol::gb28181::kCmdTypeDeviceUpgradeResult;
     notify.SN = m_sn.Increment();
     notify.DeviceID = (device_id != NULL) ? device_id : "";
     notify.Result = result ? "OK" : "ERROR";
     if (firmware != NULL && firmware[0] != ' ') {
         notify.Firmware = firmware;
     } else {
         notify.skip_Firmware();
     }
     if (session_id != NULL && session_id[0] != ' ') {
         notify.SessionID = session_id;
     } else {
         notify.skip_SessionID();
     }
     if (description != NULL && description[0] != ' ') {
         notify.Description = description;
     } else {
         notify.skip_Description();
     }
     if (!slothxml::encode(notify, NOTIFY, output)) {
         output = "";
         return false;
     }
     return true;
}

bool CGB28181XmlParser::UnPackBroadcastNotify(const std::string &xml_str, int& sn, std::string& code, BroadcastInfo& broadcast)
{
	slothxml::broadcast_notify_t notify;
	if (!slothxml::decode(xml_str, NOTIFY, notify)) {
		return false;
	}
	sn = (int)notify.SN;
	code = notify.TargetID;
	GBUtil::memcpy_safe(broadcast.SourceID, GB_ID_LEN, notify.SourceID);
	GBUtil::memcpy_safe(broadcast.TargetID, GB_ID_LEN, notify.TargetID);

	return true;
}



void CGB28181XmlParser::PackAlarmNotify(int sn ,const AlarmNotifyInfo* info, std::string &result)
{
    slothxml::alarm_notify_t notify;
    notify.CmdType = protocol::gb28181::kCmdTypeAlarm;
    notify.SN = m_sn.Increment();
    notify.DeviceID = info->DeviceID;
    notify.AlarmTime = info->AlarmTime;
    notify.AlarmPriority = info->AlarmPriority;
    notify.AlarmMethod = info->AlarmMethod;
	notify.Info.AlarmType = info->AlarmType;
	notify.AlarmDescription.assign(info->AlarmDescription);
	notify.AlarmState = info->AlarmState;

    if( info->Latitude > 0 ){
          notify.Latitude = info->Latitude;
    }else{
        notify.skip_Latitude();
    }

    if( info->Longitude > 0 ){
          notify.Longitude = info->Longitude;
    }else{
        notify.skip_Longitude();
    }

    if (!slothxml::encode(notify, NOTIFY, result)) {
            result = "";
    }

}

bool CGB28181XmlParser::UnPackAlarmNotify(const std::string &xml_str, int& sn,  std::string& code ,AlarmNotifyInfo* info)
{
    slothxml::alarm_notify_t notify;
    if (!slothxml::decode(xml_str, NOTIFY, notify)) {
            return false;
    }
    sn = notify.SN;
    code = notify.DeviceID;
    GBUtil::memcpy_safe(info->AlarmTime, TIME_LEN, notify.AlarmTime);
    info->AlarmPriority = notify.AlarmPriority;
    info->AlarmMethod =  notify.AlarmMethod;
    info->Latitude = notify.Latitude;
    info->Longitude = notify.Longitude;
	memcpy(info->AlarmDescription,notify.AlarmDescription.c_str(),notify.AlarmDescription.length());
	memcpy(info->DeviceID,notify.DeviceID.c_str(),notify.DeviceID.length());
	info->AlarmType = notify.Info.AlarmType;
	info->AlarmState = notify.AlarmState;
   
    return true;
}


void CGB28181XmlParser::PackResponse( const std::string& cmd,  int sn,  bool result, const char* device_id, std::string & output)
{
    slothxml::response_t     response;
    response.CmdType = cmd;
    response.SN = sn;
    response.DeviceID = device_id;
    response.Result = result ? "OK" :"ERROR";

    if (!slothxml::encode(response, RESPONSE, output)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackResponse(const std::string &xml_str,bool& result, std::string& device_id )
{
    slothxml::response_t     response;
    if (!slothxml::decode(xml_str, RESPONSE, response)) {
            return false;
    }
    device_id = response.DeviceID;
    if(  response.Result == "OK" ) {
        result  = true;
    }else{
        result = false;
    }
     return true;
}


void CGB28181XmlParser::PackCatalogSubcribe(const CatalogSubcribeInfo* info, std::string &result)
{
	slothxml::catalog_query_t catalog;

	catalog.CmdType = protocol::gb28181::kCmdTypeCatalog;
	catalog.DeviceID = info->DeivceID;
	catalog.SN = m_sn.Increment();

	catalog.skip_EndTime();
	catalog.skip_StartTime();
	
	if (!slothxml::encode(catalog, QUERY, result)) {
		result = "";
	}
	
}

bool CGB28181XmlParser::UnPackCatalogSubcribe(const std::string &xml_str, int& sn, std::string& code,CatalogSubcribeInfo* info)
{
	slothxml::catalog_query_t catalog;

	if (!slothxml::decode(xml_str, QUERY, catalog)) {
		return false;
	}
	sn = catalog.SN;
	code = catalog.DeviceID;

	return true;
}
void CGB28181XmlParser::PackMobilePositionNotify(int sn ,const MobilePositionInfo* info, std::string &result)
{
        slothxml::mobile_position_notify_t notify;
		notify.CmdType = protocol::gb28181::kCmdTypeMobilePosition;
        notify.DeviceID = info->GBCode;
		notify.SN = sn;
        notify.Altitude = info->Altitude;
        notify.Direction = info->Direction;
        notify.Longitude = info->Longitude;
        notify.Latitude = info->Latitude;

        if(!slothxml::encode(notify, NOTIFY, result)){
             result ="";
        }
}

bool CGB28181XmlParser::UnPackMobilePositionNotify(const std::string &xml_str, int& sn, std::string& code, MobilePositionInfo* info)
{
    slothxml::mobile_position_notify_t notify;
    if(!slothxml::decode(xml_str, NOTIFY, notify)){
          return false;
    }
    code = notify.DeviceID;
    info->Altitude = (int)notify.Altitude;
    info->Direction = notify.Direction;
    info->Longitude = notify.Longitude;
    info->Latitude = notify.Latitude;
       return true;
}

void CGB28181XmlParser::PackMobilePositionSubcribe(const MobilePositionSubInfo* info, std::string &result)
{
    slothxml::mobile_position_sub_t     subscribe;
    subscribe.CmdType = protocol::gb28181::kCmdTypeMobilePosition;
    subscribe.SN = m_sn.Increment();
    subscribe.DeviceID = info->DeivceID;
    subscribe.Interval = info->Interval;
    if (!slothxml::encode(subscribe, QUERY, result)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackMobilePositionSubcribe(const std::string &xml_str, int& sn, std::string& code, MobilePositionSubInfo* info)
{
    slothxml::mobile_position_sub_t    sub;
   if (!slothxml::decode(xml_str, QUERY, sub)) {
       return false;
   }
   sn =  sub.SN;
   code = sub.DeviceID;
   info->Interval = sub.Interval;
   return true;

}



void CGB28181XmlParser::PackKeyFrameControl(const char* device_id, std::string &result)
{
    slothxml::Iframe_control_t     control;
    control.CmdType = protocol::gb28181::kCmdTypeDeviceControl;
    control.SN = m_sn;
    control.DeviceID = device_id;
    control.IFameCmd = "Send";
    if (!slothxml::encode(control, "Control", result)) {
            result = "";
    }
}

bool CGB28181XmlParser::UnPackKeyFrameControl(const std::string &xml_str, int& sn,  DevControlCmd& cmd)
{

    slothxml::Iframe_control_t     control;
    if (!slothxml::decode(xml_str, "Control", control)) {
        return false;
    }
    sn = control.SN;
    GBUtil::memcpy_safe(cmd.GBCode ,GB_ID_LEN, control.DeviceID  );
    return true;
}

void CGB28181XmlParser::PackHomePositionControl(const char* device_id, const HomePositionCmd* cmd, std::string &result)
{
    slothxml::home_position_control_t control;
   control.CmdType = protocol::gb28181::kCmdTypeDeviceControl;
   control.DeviceID = device_id;
   control.SN = m_sn.Increment();
   control.HomePosition.Enabled = cmd->enable;
   control.HomePosition.PresetIndex = cmd->presetIndex;
   control.HomePosition.ResetTime = cmd->resetTime;
    if (!slothxml::encode(control, "Control", result)) {
        result = "";
    }
      return ;
}

bool CGB28181XmlParser::UnPackHomePositionControl(const std::string &xml_str, int& sn , DevControlCmd& cmd)
{
      slothxml::home_position_control_t control;
      if (!slothxml::decode(xml_str, "Control", control)) {
          return false;
      }
      sn = control.SN;
      GBUtil::memcpy_safe(cmd.GBCode ,GB_ID_LEN, control.DeviceID  );
      cmd.control_param.homeposition_cmd.enable = control.HomePosition.Enabled;
      cmd.control_param.homeposition_cmd.presetIndex = control.HomePosition.PresetIndex;
      cmd.control_param.homeposition_cmd.resetTime = control.HomePosition.ResetTime;
        return true;
}


bool CGB28181XmlParser::UnPackDeviceControl( const std::string &xml_str, int &sn,  DevControlCmd& cmd  )
{
	   
        switch( (int)cmd.type ) {

               case kPtzControl : return UnPackPTZControl( xml_str , sn, cmd  );
               case kRecordControl : return UnPackRecordControl( xml_str , sn, cmd  );
               case kGurdControl : return UnPackGurdControl(  xml_str , sn, cmd  );
               case kAlarmResetControl : return UnPackAlarmResetControl(  xml_str , sn, cmd  );
               case kDragZoomInControl :  return UnPackZoomControl(xml_str , sn, cmd );
               case kDragZoomOutControl : return UnPackZoomControl(xml_str , sn, cmd );
               case kDevConfigControl : return UnPackConfigControl(xml_str, sn, cmd);
               case kIFameControl : return   UnPackKeyFrameControl(xml_str , sn, cmd ) ;
               case kHomePositionControl :  return UnPackHomePositionControl(xml_str , sn, cmd );
			   case kTeleBootControl : return UnPackTeleBootControl(xml_str , sn, cmd);
               case kDeviceUpgradeControl : return UnPackDeviceUpgradeControl(xml_str , sn, cmd);
        }

		return false;
}


bool CGB28181XmlParser::UnPackTeleBootControl(const std::string &xml_str, int& sn , DevControlCmd& cmd)
{
	slothxml::teleboot_control_t control;
	if (!slothxml::decode(xml_str, "Control", control))
	{
		return false;
	}
	GBUtil::memcpy_safe(cmd.GBCode,GB_ID_LEN,control.DeviceID);
	sn = control.SN;
	return true;
}

bool CGB28181XmlParser::UnPackDeviceUpgradeControl(const std::string &xml_str, int& sn , DevControlCmd& cmd)
{
	slothxml::device_upgrade_control_t control;
	if (!slothxml::decode(xml_str, "Control", control))
	{
		return false;
	}
	GBUtil::memcpy_safe(cmd.GBCode,GB_ID_LEN,control.DeviceID);
	sn = control.SN;
	GBUtil::memcpy_safe(cmd.control_param.device_upgrade_cmd.Firmware,
		sizeof(cmd.control_param.device_upgrade_cmd.Firmware),
		control.DeviceUpgrade.Firmware);
	GBUtil::memcpy_safe(cmd.control_param.device_upgrade_cmd.FileURL,
		sizeof(cmd.control_param.device_upgrade_cmd.FileURL),
		control.DeviceUpgrade.FileURL);
	GBUtil::memcpy_safe(cmd.control_param.device_upgrade_cmd.Manufacturer,
		sizeof(cmd.control_param.device_upgrade_cmd.Manufacturer),
		control.DeviceUpgrade.Manufacturer);
	GBUtil::memcpy_safe(cmd.control_param.device_upgrade_cmd.SessionID,
		sizeof(cmd.control_param.device_upgrade_cmd.SessionID),
		control.DeviceUpgrade.SessionID);
	cmd.control_param.device_upgrade_cmd.FileSize = control.DeviceUpgrade.FileSize;
	GBUtil::memcpy_safe(cmd.control_param.device_upgrade_cmd.Checksum,
		sizeof(cmd.control_param.device_upgrade_cmd.Checksum),
		control.DeviceUpgrade.Checksum);
	cmd.control_param.device_upgrade_cmd.ForceUpgrade = control.DeviceUpgrade.ForceUpgrade;
	return true;
}

bool CGB28181XmlParser::UnPackConfigControl(const std::string &xml_str, int& sn , DevControlCmd& cmd)
{
	slothxml::configdownload_response_t control;
	if (!slothxml::decode(xml_str, "Control",control))
	{
		return false;
	}
	std::vector<SettingParam> configParamVector;
	GBUtil::memcpy_safe(cmd.GBCode,GB_ID_LEN,control.DeviceID);
	sn = control.SN;
	if (control.xml_has_BasicParam())
	{
		SettingParam basicConfigParam;
		memset(&basicConfigParam,0,sizeof(SettingParam));
		basicConfigParam.SetType = kBasicSetting;
        basicConfigParam.unionSetParam.Basic.Expiration = atoi(control.BasicParam.Expiration.c_str());
		basicConfigParam.unionSetParam.Basic.HeartBeatcount = control.BasicParam.HeartBeatCount;
		basicConfigParam.unionSetParam.Basic.HeartBeatInterval = control.BasicParam.HeartBeatInterval;
		if (!control.BasicParam.Name.empty())
		{
			memcpy(basicConfigParam.unionSetParam.Basic.DeviceName,control.BasicParam.Name.c_str(),control.BasicParam.Name.length());
		}
		configParamVector.push_back(basicConfigParam);
	}
    ImageSetting frameMirrorSetting;
    const bool hasFrameMirror = ParseFrameMirrorElement(xml_str, &frameMirrorSetting);
	if (!hasFrameMirror && control.xml_has_VideoParamOpt())
	{
		if (!control.VideoParamOpt.ImageFlip.empty())
		{
			SettingParam imageConfigParam;
			memset(&imageConfigParam,0,sizeof(SettingParam));
			imageConfigParam.SetType = kImageSetting;
			GBUtil::memcpy_safe(imageConfigParam.unionSetParam.Image.FlipMode,
				sizeof(imageConfigParam.unionSetParam.Image.FlipMode),
				control.VideoParamOpt.ImageFlip);
			configParamVector.push_back(imageConfigParam);
		}
	}
    if (hasFrameMirror)
    {
        SettingParam imageConfigParam;
        memset(&imageConfigParam, 0, sizeof(SettingParam));
        imageConfigParam.SetType = kImageSetting;
        imageConfigParam.unionSetParam.Image = frameMirrorSetting;
        configParamVector.push_back(imageConfigParam);
    }
	if (control.xml_has_SVACEncodeConfig())
	{
	}
    std::vector<VideoParamAttributeSetting> videoParamSettings;
    if (ParseVideoParamAttributeElements(xml_str, &videoParamSettings))
    {
        for (size_t i = 0; i < videoParamSettings.size(); ++i)
        {
            SettingParam videoConfigParam;
            memset(&videoConfigParam, 0, sizeof(SettingParam));
            videoConfigParam.SetType = kVideoParamAttributeSetting;
            videoConfigParam.unionSetParam.VideoAttr = videoParamSettings[i];
            configParamVector.push_back(videoConfigParam);
        }
    }
    OsdSetting osdSetting;
    if (ParseOsdConfigElement(xml_str, &osdSetting))
    {
        SettingParam osdConfigParam;
        memset(&osdConfigParam, 0, sizeof(SettingParam));
        osdConfigParam.SetType = kOsdSetting;
        osdConfigParam.unionSetParam.Osd = osdSetting;
        configParamVector.push_back(osdConfigParam);
    }
	int num = configParamVector.size();
	cmd.control_param.config_set_param.Num = num;
	cmd.control_param.config_set_param.arySetParam = new SettingParam[num];
	memset(cmd.control_param.config_set_param.arySetParam,0,sizeof(SettingParam)*num);
	for (int i = 0; i < num; i++)
	{
        switch((int)configParamVector[i].SetType)
		{
		case kBasicSetting:
			cmd.control_param.config_set_param.arySetParam[i] = configParamVector[i];
			break;
		case kEncodeSetting:
			break;
		case kDecodeSetting:
			break;
		case kImageSetting:
			cmd.control_param.config_set_param.arySetParam[i] = configParamVector[i];
			break;
        case kOsdSetting:
            cmd.control_param.config_set_param.arySetParam[i] = configParamVector[i];
            break;
        case kVideoParamAttributeSetting:
            cmd.control_param.config_set_param.arySetParam[i] = configParamVector[i];
            break;
		}
	}
	return true;
}




