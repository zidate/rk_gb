#include "Common.h"


extern "C" {
int gb_rkipc_osd_common_set(int font_size, const char *font_color_mode, const char *font_color);
int gb_rkipc_osd_time_set(int date_type, int time_type, int display_week_enabled,
						  int x, int y, int show, int alignment);
int gb_rkipc_osd_text_set(int index, const char *text, int x, int y, int show, int alignment);
}

std::string strToHexAscii(const string &input);
std::string hexToStr(const string &hexStr);

CAVManager* CAVManager::_instance;

// CAVManager::CAVManager() : CThread("AVManager", TP_CAPTURE)
CAVManager::CAVManager()
{
	m_audioInit = false;
	m_videInit = false;
}

CAVManager::~CAVManager()
{
	
}

CAVManager* CAVManager::instance()
{
	if(NULL == CAVManager::_instance)
	{
		CAVManager::_instance = new CAVManager();
	}
	return CAVManager::_instance;
}

//视频
//extern int g_test_enc_type_change; //for debug
//extern int g_test_enc_type[2]; //for debug
void CAVManager::onConfigVideo(const CConfigTable &table, int &ret)
{
	VideoConf_S VideoConfig;
    TExchangeAL<VideoConf_S>::getConfig(table, VideoConfig);

	for (int i = 0; i < VIDEO_CHANNEL_MAX; i++)
	{
		if(memcmp(&m_VideoConfig.chan[i], &VideoConfig.chan[i], sizeof(VideoChannelConf_S)))
		{
			CaptureChangeEncParam(i, VideoConfig.chan[i].enc_type, VideoConfig.chan[i].bit_rate, VideoConfig.chan[i].frmae_rate, VideoConfig.chan[i].gop);
		}
	}
//	g_test_enc_type[0] = m_VideoConfig.chan[0].enc_type;
//	g_test_enc_type[1] = m_VideoConfig.chan[1].enc_type;
//	if (VideoConfig.chan[0].enc_type != m_VideoConfig.chan[0].enc_type || 
//		VideoConfig.chan[1].enc_type != m_VideoConfig.chan[1].enc_type)
//		g_test_enc_type_change = 1;
	m_VideoConfig = VideoConfig;
}

void CAVManager::onConfigOSDTime(const CConfigTable &table, int &ret)
{
	OSDTimeConf_S OSDTimeConfig;
    TExchangeAL<OSDTimeConf_S>::getConfig(table, OSDTimeConfig);

	gb_rkipc_osd_common_set(OSDTimeConfig.font_size,
							OSDTimeConfig.font_color_mode.c_str(),
							OSDTimeConfig.font_color.c_str());
	gb_rkipc_osd_time_set(OSDTimeConfig.date_type,
						  OSDTimeConfig.time_type,
						  OSDTimeConfig.display_week_enabled,
						  OSDTimeConfig.x,
						  OSDTimeConfig.y,
						  OSDTimeConfig.show,
						  OSDTimeConfig.alignment);
	m_OSDTimeConf = OSDTimeConfig;
}

void CAVManager::onConfigOSDText(const CConfigTable &table, int &ret)
{
	OSDTextAllConf_S OSDTextAllConfig;
    TExchangeAL<OSDTextAllConf_S>::getConfig(table, OSDTextAllConfig);

	for (int i = 0; i < OSD_TEXT_MAX; i++)
	{
		if (OSDTextAllConfig.osd_text[i].text != m_OSDTextAllConf.osd_text[i].text ||
			OSDTextAllConfig.osd_text[i].x != m_OSDTextAllConf.osd_text[i].x ||
			OSDTextAllConfig.osd_text[i].y != m_OSDTextAllConf.osd_text[i].y ||
			OSDTextAllConfig.osd_text[i].show != m_OSDTextAllConf.osd_text[i].show ||
			OSDTextAllConfig.osd_text[i].alignment != m_OSDTextAllConf.osd_text[i].alignment)
		{
			printf("onConfigOSDText -> [%s]\n", OSDTextAllConfig.osd_text[i].text.c_str());
			std::string src_str = hexToStr(OSDTextAllConfig.osd_text[i].text);
			gb_rkipc_osd_text_set(i,
								  src_str.c_str(),
								  OSDTextAllConfig.osd_text[i].x,
								  OSDTextAllConfig.osd_text[i].y,
								  OSDTextAllConfig.osd_text[i].show,
								  OSDTextAllConfig.osd_text[i].alignment);
		}
	}
	m_OSDTextAllConf = OSDTextAllConfig;
}

bool CAVManager::VideoParamInit()
{
	CConfigTable table;
	g_configManager.getConfig(getConfigName(CFG_VIDEO), table);
    TExchangeAL<VideoConf_S>::getConfig(table, m_VideoConfig);
	g_configManager.attach(getConfigName(CFG_VIDEO), IConfigManager::Proc(&CAVManager::onConfigVideo, this));

	for (int i = 0; i < VIDEO_CHANNEL_MAX; i++)
	{
//		g_test_enc_type[i] = m_VideoConfig.chan[i].enc_type;
		CaptureInitEncParam(i, m_VideoConfig.chan[i].enc_type, m_VideoConfig.chan[i].bit_rate, m_VideoConfig.chan[i].frmae_rate, m_VideoConfig.chan[i].gop);
	}

	table.clear();
	g_configManager.getConfig(getConfigName(CFG_OSD_TIME), table);
    TExchangeAL<OSDTimeConf_S>::getConfig(table, m_OSDTimeConf);
	g_configManager.attach(getConfigName(CFG_OSD_TIME), IConfigManager::Proc(&CAVManager::onConfigOSDTime, this));

	gb_rkipc_osd_common_set(m_OSDTimeConf.font_size,
							m_OSDTimeConf.font_color_mode.c_str(),
							m_OSDTimeConf.font_color.c_str());
	gb_rkipc_osd_time_set(m_OSDTimeConf.date_type,
						  m_OSDTimeConf.time_type,
						  m_OSDTimeConf.display_week_enabled,
						  m_OSDTimeConf.x,
						  m_OSDTimeConf.y,
						  m_OSDTimeConf.show,
						  m_OSDTimeConf.alignment);
	
	table.clear();
	g_configManager.getConfig(getConfigName(CFG_OSD_TEXT), table);
    TExchangeAL<OSDTextAllConf_S>::getConfig(table, m_OSDTextAllConf);
	g_configManager.attach(getConfigName(CFG_OSD_TEXT), IConfigManager::Proc(&CAVManager::onConfigOSDText, this));
	
	for (int i = 0; i < OSD_TEXT_MAX; i++)
	{
		printf("VideoParamInit -> [%s]\n", m_OSDTextAllConf.osd_text[i].text.c_str());
		std::string src_str = hexToStr(m_OSDTextAllConf.osd_text[i].text);
		gb_rkipc_osd_text_set(i,
							  src_str.c_str(),
							  m_OSDTextAllConf.osd_text[i].x,
							  m_OSDTextAllConf.osd_text[i].y,
							  m_OSDTextAllConf.osd_text[i].show,
							  m_OSDTextAllConf.osd_text[i].alignment);
	}
	return true;
}

bool CAVManager::VideoInit()
{
	if (!m_videInit)
	{
		m_videInit = true;
		CaptureCreate(0);
		CaptureStart(0,0);
	}

	return true;
}

bool CAVManager::VideoDeInit()
{
	if (m_videInit)
	{
		m_videInit = false;
		CaptureStop(0,0);
		CaptureDestroy(0);
	}
	return true;
}

//音频
bool CAVManager::AudioInit()
{
	if (!m_audioInit)
	{
		m_audioInit = true;
		AudioCreate();
	}

	return true;
}

bool CAVManager::AudioDeInit()
{
	if (m_audioInit)
	{
		m_audioInit = false;
		AudioInDestroy(0);
	}
	
	return true;
}

//AV
bool CAVManager::RealTimeStreamStart(int media_type, dmc_media_input_fn proc)
{
	CaptureSetStreamCallBack((char*)"realtime",media_type,proc);
	return true;
}

bool CAVManager::RealTimeStreamStop(int media_type)
{
	CaptureSetStreamCallBack((char*)"realtime",media_type,NULL);
	return true;
}

bool CAVManager::RecordStreamStart(int media_type, dmc_media_input_fn proc)
{
	CaptureSetStreamCallBack((char*)"record",media_type,proc);
	return true;
}

bool CAVManager::RecordStreamStop(int media_type)
{
	CaptureSetStreamCallBack((char*)"record",media_type,NULL);
	return true;
}

// void CAVManager::ThreadProc()
// {
// 	while (m_bLoop) 
// 	{
// 		sleep(1);
// 	}
// }

bool CAVManager::PlayVoice(uchar *data, int length, int type, int format)
{
	AudioOutPutBuf(data,length);
	return true;
}

