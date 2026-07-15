#include <sys/prctl.h>
#include "live_rtsp.h"
#include "Common.h"
#include "xop/RtspServer.h"
#include "xop/G711ASource.h"
#include "net/Timer.h"

static xop::MediaSessionId session_id_main = 0;
static xop::RtspServer* rtsp_server = NULL;
static xop::MediaSessionId session_id_sub = 0;

static pthread_t g_rtspPthreadTid = 0;
static bool s_bRtspStart = false;
static bool s_rtspThreadQuit = true;

typedef struct
{
    uint32_t len;                     //! Length of the NAL unit (Excluding the start code, which does not belong to the NALU)
    int32_t forbidden_bit;            //! should be always FALSE
    int32_t nal_reference_idc;        //! NALU_PRIORITY_xxxx
    int32_t nal_unit_type;            //! NALU_TYPE_xxxx
    char *buf;                        //! contains the first byte followed by the EBSP
} NALU_H264_t;

typedef struct
{
    uint32_t len;                     //! Length of the NAL unit (Excluding the start code, which does not belong to the NALU)
    int32_t forbidden_bit;            //! should be always FALSE
    int32_t naltype;                  //! NALTYPE
    int32_t layerid;                  //! LAYERID
	int32_t tid;                      //! TID
    char *buf;                        //! contains the first byte followed by the EBSP
} NALU_H265_t;

static int32_t FindStartCode(const char *buf)
{
    if(buf[0] != 0x0 || buf[1] != 0x0 ){
        return 0;
    }

    if(buf[2] == 0x1){
        return 3;
    } else if(buf[2] == 0x0){
        if(buf[3] == 0x1){
            return 4;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
    return 0;
}

static int32_t ReadOneNaluH265FromBuf(const char *buffer, uint32_t nBufferSize, uint32_t offSet, NALU_H265_t* pNalu)
{
    uint32_t start = 0;
    if( offSet < nBufferSize) {
        start = FindStartCode(buffer + offSet);
        if(start != 0) {
            uint32_t pos = start;
            while (offSet + pos < nBufferSize) {
                if(buffer[offSet + pos++] == 0x00 &&
                    buffer[offSet + pos++] == 0x00 &&
                    buffer[offSet + pos++] == 0x00 &&
                    buffer[offSet + pos++] == 0x01
                    ) {
                    break;
                }
            }

            if(offSet + pos == nBufferSize){
                pNalu->len = pos - start;
            } else {
                pNalu->len = (pos - 4) - start;
            }

            pNalu->buf =(char*)&buffer[offSet + start];
            pNalu->forbidden_bit = pNalu->buf[0] & 0x80;
            pNalu->naltype = pNalu->buf[0] & 0x7e; // 6 bit


            return (pNalu->len + start);
        }
    }

    return 0;
}

static int32_t ReadOneNaluFromBuf(const char *buffer, uint32_t nBufferSize, uint32_t offSet, NALU_H264_t* pNalu)
{
    uint32_t start = 0;
    if( offSet < nBufferSize) {
        start = FindStartCode(buffer + offSet);
        if(start != 0) {
            uint32_t pos = start;
            while (offSet + pos < nBufferSize) {
                if(buffer[offSet + pos++] == 0x00 &&
                    buffer[offSet + pos++] == 0x00 &&
                    buffer[offSet + pos++] == 0x00 &&
                    buffer[offSet + pos++] == 0x01
                    ) {
                    break;
                }
            }

            if(offSet + pos == nBufferSize){
                pNalu->len = pos - start;
            } else {
                pNalu->len = (pos - 4) - start;
            }

            pNalu->buf =(char*)&buffer[offSet + start];
            pNalu->forbidden_bit = pNalu->buf[0] & 0x80;
            pNalu->nal_reference_idc = pNalu->buf[0] & 0x60; // 2 bit
            pNalu->nal_unit_type = (pNalu->buf[0]) & 0x1f;// 5 bit

            return (pNalu->len + start);
        }
    }

    return 0;
}

static int SIGN_BIT = 0x80;
static int QUANT_MASK = 0xf;
static int SEG_SHIFT = 4;
static int SEG_MASK = 0x70;
static int BIAS = 0x84;
static int CLIP = 8159;

static short seg_end[] = {0xFF, 0x1FF, 0x3FF, 0x7FF,0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};

static short search(short val,short table[],short size){

	for (short i = 0 ; i < size; i++) {
		if(val <= table[i]){
			return i;
		}
	}
	return size;
}

static unsigned char linear2alaw(short pcm_val){
	short mask;
	short seg;
	unsigned char aval;
	if(pcm_val >= 0){
		mask = 0xD5;
	}else{
		mask = 0x55;
		pcm_val = (short) (-pcm_val - 1);
		if(pcm_val < 0){
			pcm_val = 32767;
		}
	}

	/* Convert the scaled magnitude to segment number. */
	seg = search(pcm_val, seg_end, (short) 8);

	/* Combine the sign, segment, and quantization bits. */

	if (seg >= 8)       /* out of range, return maximum value. */
		return (unsigned char) (0x7F ^ mask);
	else {
		aval = (unsigned char) (seg << SEG_SHIFT);
		if (seg < 2)
			aval |= (pcm_val >> 4) & QUANT_MASK;
		else
			aval |= (pcm_val >> (seg + 3)) & QUANT_MASK;
		return (unsigned char) (aval ^ mask);
	}
}
//extern int get_local_ip_info(char *interface, char *ip);
extern int g_test_enc_type[2]; //for debug
//extern char g_onvifPassword[33];
static void *rtspThread(void *data)
{
	char ip[20] = {0};
	g_NetConfigHook.GetNetWorkIp(ip,sizeof(ip));
//	int ret; //<shang>
//	ret = get_local_ip_info("eth0", ip);
//	if (ret)
//		ret = get_local_ip_info("wlan0", ip);
	printf("rtspThread get ip : %s\n",ip);
	
	char url[100] = {0};
	snprintf(url,sizeof(url),"rtsp://%s:8554/main",ip);

	char url_sub[100] = {0};
	snprintf(url_sub,sizeof(url_sub),"rtsp://%s:8554/sub",ip);

	std::string rtsp_url = url;            //"rtsp://192.168.110.228:8554/main";
	std::string rtsp_url_sub = url_sub;    //"rtsp://192.168.110.228:8554/sub";

	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());  
	std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());
	if (!server->Start(ip, 8554)) 
	{
		s_rtspThreadQuit = true;
		return NULL;
	}
	
// #ifdef AUTH_CONFIG
// server->SetAuthConfig("-_-", "admin", g_onvifPassword);
// #endif

	{
		xop::MediaSession *session = xop::MediaSession::CreateNew("main"); // url: rtsp://ip/live
		if (g_test_enc_type[0])
		session->AddSource(xop::channel_0, xop::H265Source::CreateNew(15)); 
		else
		session->AddSource(xop::channel_0, xop::H264Source::CreateNew(15)); 
		session->AddSource(xop::channel_1, xop::G711ASource::CreateNew());
		//session->StartMulticast(); /* 开启组播(ip,端口随机生成), 默认使用 RTP_OVER_UDP, RTP_OVER_RTSP */

		session->AddNotifyConnectedCallback([] (xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port){
			printf("RTSP client connect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
		});
	
		session->AddNotifyDisconnectedCallback([](xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port) {
			printf("RTSP client disconnect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
		});

		std::cout << "URL: " << rtsp_url << std::endl;
		
		//xop::MediaSessionId 
		session_id_main = server->AddSession(session);
	}

	{
		xop::MediaSession *session = xop::MediaSession::CreateNew("sub"); // url: rtsp://ip/live
		if (g_test_enc_type[1])
		session->AddSource(xop::channel_0, xop::H265Source::CreateNew(15));//H265
		else
		session->AddSource(xop::channel_0, xop::H264Source::CreateNew(15));  //H264
		session->AddSource(xop::channel_1, xop::G711ASource::CreateNew());
		//session->StartMulticast(); /* 开启组播(ip,端口随机生成), 默认使用 RTP_OVER_UDP, RTP_OVER_RTSP */

		session->AddNotifyConnectedCallback([] (xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port){
			printf("RTSP client sub connect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
		});
	
		session->AddNotifyDisconnectedCallback([](xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port) {
			printf("RTSP client sub disconnect, ip=%s, port=%hu \n", peer_ip.c_str(), peer_port);
		});

		std::cout << "URL: " << rtsp_url_sub << std::endl;
		
		//xop::MediaSessionId 
		session_id_sub = server->AddSession(session); 
	}

	rtsp_server = server.get();

	s_rtspThreadQuit = false;
	while(s_bRtspStart)
	{
		xop::Timer::Sleep(1000);
	}
	s_rtspThreadQuit = true;

	return NULL;
}

void StopRtspPthread(void)
{
	s_bRtspStart = false;
	if (g_rtspPthreadTid)
	{
		int nRet = pthread_join(g_rtspPthreadTid,NULL);
		if (nRet != 0) 
		{
			printf("StopRtspPthread stop rtsp pthread error :%d!\n",errno);
		}
		else
		{
			printf("StopRtspPthread stop rtsp pthread ok!\n");
		}
	}
	g_rtspPthreadTid = 0;
	s_rtspThreadQuit = true;
}

int StartRtspPthread(void)
{
	int nRet = 0;
	s_bRtspStart = true;
	
	if( (g_rtspPthreadTid == 0) && s_rtspThreadQuit)
	{
		nRet = pthread_create(&g_rtspPthreadTid, NULL,rtspThread, (void *)NULL);
		if (nRet != 0) 
		{
			printf("StartRtspPthread create rtsp pthread error :%d!\n",errno);
			return -1;
		}
	}
	else
	{
		printf("StartRtspPthread error!\n");
		return -1;
	}

	return 0;
}

int RtspPutFrame_Main(int iFrmType, unsigned long long *pullTimestamp, char *pData, int iDataLen)
{
	//获取一帧 H265, 打包 去掉startcode 00 00 00 01
	NALU_H265_t nalu;
    int32_t pos = 0, len = 0;
    
    while (pos<iDataLen) 
	{
        len = ReadOneNaluH265FromBuf(pData, iDataLen, pos, &nalu);
		//printf("len(%d)\n", len);
		if (0==len)
		{
			break;
		}
		pos += len;
		//printf("pos(%d)\n", pos);
		//解析出nalu
		//printf("## nalu type(%d)\n", nalu.naltype);
        //printf("NALU size:%8d\n",nalu.len);

		xop::AVFrame videoFrame = {0};
		videoFrame.type = (DMC_MEDIA_SUBTYPE_IFRAME == iFrmType) ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P; // 建议确定帧类型。I帧(xop::VIDEO_FRAME_I) P帧(xop::VIDEO_FRAME_P)
		videoFrame.size = nalu.len;  // 视频帧大小 
		videoFrame.timestamp = xop::H265Source::GetTimestamp(); // 时间戳, 建议使用编码器提供的时间戳
		videoFrame.buffer.reset(new uint8_t[videoFrame.size]);                    
		memcpy(videoFrame.buffer.get(), nalu.buf, videoFrame.size);					
		if (!s_rtspThreadQuit && rtsp_server)
		{
			rtsp_server->PushFrame(session_id_main, xop::channel_0, videoFrame); //送到服务器进行转发, 接口线程安全
		}
	}

	return 0;
}

int RtspPutFrame_Sub(int iFrmType, unsigned long long *pullTimestamp, char *pData, int iDataLen)
{
	//获取一帧 H265, 打包 去掉startcode 00 00 00 01
	// NALU_H265_t nalu;
	NALU_H264_t nalu;
    int32_t pos = 0, len = 0;
    
    while (pos<iDataLen) 
	{
        // len = ReadOneNaluH265FromBuf(pData, iDataLen, pos, &nalu);
		len = ReadOneNaluFromBuf(pData, iDataLen, pos, &nalu);
		//printf("len(%d)\n", len);
		if (0==len)
		{
			break;
		}
		pos += len;
		//printf("pos(%d)\n", pos);
		//解析出nalu
		//printf("## nalu type(%d)\n", nalu.nal_unit_type);
        //printf("NALU size:%8d\n",nalu.len);

		xop::AVFrame videoFrame = {0};
		videoFrame.type = (DMC_MEDIA_SUBTYPE_IFRAME == iFrmType) ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P; // 建议确定帧类型。I帧(xop::VIDEO_FRAME_I) P帧(xop::VIDEO_FRAME_P)
		videoFrame.size = nalu.len;  // 视频帧大小 
		// videoFrame.timestamp = xop::H265Source::GetTimestamp(); // 时间戳, 建议使用编码器提供的时间戳
		videoFrame.timestamp = xop::H264Source::GetTimestamp(); // 时间戳, 建议使用编码器提供的时间戳
		videoFrame.buffer.reset(new uint8_t[videoFrame.size]);                    
		memcpy(videoFrame.buffer.get(), nalu.buf, videoFrame.size);					
		if (!s_rtspThreadQuit && rtsp_server)
		{
			rtsp_server->PushFrame(session_id_sub, xop::channel_0, videoFrame); //送到服务器进行转发, 接口线程安全
		}
	}
	
	return 0;
}

int RtspPutFrame_Audio(int iFrmType, unsigned long long *pullTimestamp, char *pData, int iDataLen)
{
	int size = iDataLen/2;
	unsigned char buff[size];
	
	int j = 0;
	int i = 0;
	for (i = 0; i < iDataLen; )
	{
		short *p = (short *)(pData+i);
		if (!p)
		{
			AppErr("the pointer is null\n");
			return -1;
		}
		short val = *p;
		buff[j] = linear2alaw(val);
		j++;
		i+=2;
	}
	//获取一帧 G711A, 打包
	xop::AVFrame audioFrame = {0};
	audioFrame.type = xop::AUDIO_FRAME;
	audioFrame.size = size;  /* 音频帧大小 */
	audioFrame.timestamp = xop::G711ASource::GetTimestamp(); // 时间戳
	audioFrame.buffer.reset(new uint8_t[audioFrame.size]);                    
	memcpy(audioFrame.buffer.get(), buff, audioFrame.size);
	if (!s_rtspThreadQuit && rtsp_server)
	{
		rtsp_server->PushFrame(session_id_main, xop::channel_1, audioFrame); // 送到服务器进行转发, 接口线程安全
	}

	return 0;
}

int RtspPutFrame_Audio1(int iFrmType, unsigned long long *pullTimestamp, char *pData, int iDataLen)
{
	int size = iDataLen/2;
	unsigned char buff[size];
	
	int j = 0;
	int i = 0;
	for (i = 0; i < iDataLen; )
	{
		short *p = (short *)(pData+i);
		if (!p)
		{
			AppErr("the pointer is null\n");
			return -1;
		}
		short val = *p;
		buff[j] = linear2alaw(val);
		j++;
		i+=2;
	}
	//获取一帧 G711A, 打包
	xop::AVFrame audioFrame = {0};
	audioFrame.type = xop::AUDIO_FRAME;
	audioFrame.size = size;  /* 音频帧大小 */
	audioFrame.timestamp = xop::G711ASource::GetTimestamp(); // 时间戳
	audioFrame.buffer.reset(new uint8_t[audioFrame.size]);                    
	memcpy(audioFrame.buffer.get(), buff, audioFrame.size);
	if (!s_rtspThreadQuit && rtsp_server)
	{
		rtsp_server->PushFrame(session_id_sub, xop::channel_1, audioFrame); // 送到服务器进行转发, 接口线程安全
	}

	return 0;
}



