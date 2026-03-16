#include "SdpUtil.h"
#include "gbutil.h"
#include "SdpMessage.h"
#include <stdlib.h>
#include <string>

static StreamRequestType  String2Enum( const std::string& str   )
{

      if( !str.compare("Play")  ){
           return kLiveStream;
      }

      if( !str.compare("Playback")  ){
           return kPlayback;
      }

      if( !str.compare("Download")  ){
           return kDownload;
      }

      if( !str.compare("Talk")  ){
           return kAudioStream;
      }
           return kLiveStream;
}


static std::string  Enum2String( StreamRequestType type   )
{
      switch( (int)type) {
          case kLiveStream:    return "Play"; break;
          case kPlayback:       return "Playback"; break;
          case kDownload:       return "Download"; break;
          case kAudioStream:    return "Talk"; break;
      }
       return " ";
}

static bool IsDownstreamMediaRequest(StreamRequestType type)
{
      return type == kLiveStream || type == kPlayback || type == kDownload;
}

static std::string StripGbSdpExtensions(const char* str, std::string* ssrc)
{
      std::string input = str ? str : "";
      std::string normalized;
      size_t start = 0;

      while (start < input.size()) {
           size_t end = input.find('\n', start);
           std::string line = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
           while (!line.empty() && (line[line.size() - 1] == '\r' || line[line.size() - 1] == '\0')) {
                line.erase(line.size() - 1);
           }

           if (line.size() > 2 && line[1] == '=' && (line[0] == 'y' || line[0] == 'f')) {
                if (line[0] == 'y' && ssrc && ssrc->empty()) {
                     *ssrc = line.substr(2);
                }
           } else if (!line.empty() || !normalized.empty()) {
                normalized.append(line).append("\r\n");
           }

           if (end == std::string::npos) {
                break;
           }
           start = end + 1;
      }

      return normalized;
}

CSdpUtil::CSdpUtil()
{

}

bool CSdpUtil::String2MediaInfo(const char* str, MediaInfo* output )
{
    CSdpMessage sdp;
    std::string ssrc;
    std::string normalized = StripGbSdpExtensions(str, &ssrc);
    if ( sdp.Parse(str)) {
        if (normalized.empty() || sdp.Parse(normalized.c_str())) {
            return false;
        }
    }
    output->StreamNum = -1;
    if (!ssrc.empty()) {
         GBUtil::memcpy_safe(output->Ssrc, SSRC_LEN, ssrc);
    }

    const char* username = NULL;

    username = sdp.GetUsername();
    if(username){
         GBUtil::memcpy_safe(output->DeviceID, GB_ID_LEN , username);
    }

	const char* ip = NULL;

	ip = sdp.GetAddress();
	if(!ip || ip[0] == '\0'){
		ip = sdp.GetConnection().GetAddress();
	}
    CSdpMedia& media = sdp.GetMedia(0);
	if((!ip || ip[0] == '\0')){
		ip = media.GetConnection(0).GetAddress();
	}
	if(!ip){
		return false;
	}
	GBUtil::memcpy_safe(output->IP, IP_LEN , ip);
    const char* uri = NULL;
    uri = sdp.GetUri();
    if(uri) {
         GBUtil::memcpy_safe(output->Url, URI_LEN , uri);
    }

    const char* name = NULL;
    name = sdp.GetSessionName();
   output->RequestType  = String2Enum(name);

   if(  output->RequestType == kPlayback ||  output->RequestType == kDownload ) {

        CSdpTimeDescr  time_descr = sdp.GetTimeDescr(0);
        output->StartTime = atol( time_descr.GetStartTime());
        output->EndTime = atol( time_descr.GetStopTime());
   }

    output->Port = media.GetPort();

    std::string streamnumber = media.GetAttrValue("streamnumber");
    if (!streamnumber.empty()) {
         output->StreamNum = atoi(streamnumber.c_str());
    }

    if(media.GetProtocol().find("TCP") != std::string::npos){

		 std::string setup =  media.GetAttrValue("setup");
		 if (setup == "active") {
			 output->RtpType = kRtpOverTcpActive;
		 }
		 else {
			 output->RtpType = kRtpOverTcpPassive;
		 }
    }
    else{
         output->RtpType  = kRtpOverUdp;
    }

    if(  media.GetType().compare("video")!= 0) {
         output->RtpDescri.type = kGBVideo;
    }else{
         output->RtpDescri.type = kGBAudio;
    }
     return true;
}

void CSdpUtil::ToString(const MediaInfo*  gb_meida,  std::string& result )
{
    CSdpMessage sdp;
    // v= 字段
    sdp.SetVersion("0");

    // o= 字段
    sdp.SetOrigin( gb_meida->DeviceID, "0", "0", "IN", "IP4",   gb_meida->IP );

    // s= 字段
    sdp.SetSessionName(   Enum2String(gb_meida->RequestType).c_str()   );

    // c= 字段
    CSdpConnection conn;
    conn.SetAddress(gb_meida->IP);
    conn.SetAddrType("IP4");
    conn.SetNetType("IN");
    sdp.SetConnection(conn);

    // m= 字段必须存在
    CSdpMedia media;
    media.SetPort11(gb_meida->Port);

    if( gb_meida->RtpType == kRtpOverUdp) {

		   media.SetProtocol("RTP/AVP");

    }else{

		media.SetProtocol("TCP/RTP/AVP");
		CSdpAttribute setup;
		if (gb_meida->RtpType == kRtpOverTcpActive) {
			setup.SetAttribute("setup", "active");
		}
		else {
			setup.SetAttribute("setup", "passive");
		}
		media.AddAttribute(setup);
		CSdpAttribute connection;
		connection.SetAttribute("connection", "new");
		media.AddAttribute(connection);
    }


    if( gb_meida->RtpDescri.type ==  kGBVideo) {
        media.SetType("video");
    }else{
        media.SetType("audio");
    }

    if( gb_meida->RequestType == kPlayback || gb_meida->RequestType == kDownload ) {
        CSdpTimeDescr timeDes;
        char start_time[128] = {0};
        sprintf(start_time, "%lu", gb_meida->StartTime );
        char end_time[128] = {0};
        sprintf(end_time, "%lu", gb_meida->EndTime );
        timeDes.SetTime(start_time,  end_time);
        sdp.ClearTimeDescr();
        sdp.AddTimeDescr(timeDes);
    }

	if (gb_meida->RequestType == kDownload) {

		char buf[64] = { 0 };
		if (gb_meida->DownloadSpeed > 0) {
			sprintf(buf, "%d", gb_meida->DownloadSpeed);
			CSdpAttribute speed;
			speed.SetAttribute("downloadspeed", buf);
			media.AddAttribute(speed);
		}

		if (gb_meida->FileSize > 0) {
			memset(buf, 0, sizeof(buf));
			sprintf(buf, "%d", gb_meida->FileSize);
			CSdpAttribute file_size;
			file_size.SetAttribute("filesize", buf);
			media.AddAttribute(file_size);
		}
	}


    int i = 0 ;
    char format[128];
    char sample[128];

    bool added_payload = false;
    if(gb_meida->RtpDescri.DescriNum >=1 && gb_meida->RtpDescri.mapDescri ) {

        for(; i < (int)gb_meida->RtpDescri.DescriNum; i++) {

            //96 PS/90000
            CSdpAttribute a1;
            std::string descri;

            sprintf(format,"%d",gb_meida->RtpDescri.mapDescri[i].MediaFormat );
            sprintf(sample,"%d",gb_meida->RtpDescri.mapDescri[i].SampleRate );
            media.AddPayload(format);
            added_payload = true;
            descri.append(  format     )
                     .append(" ")
                     .append(gb_meida->RtpDescri.mapDescri[i].MimeType)
                     .append("/")
                     .append( sample);
            a1.SetAttribute("rtpmap",  descri.c_str() );
            media.AddAttribute(a1);
        }

    }

    if (!added_payload) {
        media.AddPayload("96");
    }

	if (gb_meida->Url[0] != '\0') {
		sdp.SetUri(gb_meida->Url);
	}

    if (gb_meida->StreamNum >= 0) {
        char stream_num[32] = {0};
        sprintf(stream_num, "%d", gb_meida->StreamNum);
        CSdpAttribute stream_attr;
        stream_attr.SetAttribute("streamnumber", stream_num);
        media.AddAttribute(stream_attr);
    }

    CSdpAttribute a;
    if (IsDownstreamMediaRequest(gb_meida->RequestType)) {
        a.SetAttribute("sendonly");
    }
    else {
        a.SetAttribute("recvonly");
    }
    media.AddAttribute(a);

    //添加y字段，TVT的IPC如果没有y字段无法建立会话
    sdp.AddMedia(media);
    result = sdp.ToString();
	std::string ssrcstr = "y=";
	ssrcstr.append(gb_meida->Ssrc).append("\r\n");
    result+= ssrcstr;
}
