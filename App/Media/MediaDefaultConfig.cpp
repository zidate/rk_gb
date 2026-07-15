#include "MediaDefaultConfig.h"

extern int g_nCapture;
extern int g_nDecorder;
extern int g_nDigital;
PATTERN_SINGLETON_IMPLEMENT(CMediaDefaultConfig);

CMediaDefaultConfig::CMediaDefaultConfig()
{
}

CMediaDefaultConfig::~CMediaDefaultConfig()
{

}

bool CMediaDefaultConfig::start()
{
	setRecord();
	setMotionDetect();
	setMotionTrack();
	setPir();
	setVoice();
	setSiren();
	setIpcVol();
	setVideo();
	setAudio();
	setOSDTime();
	setOSDText();
	setCmiot();
	setCmiotIntrude();
	return true;
}

void CMediaDefaultConfig::setRecord()
{
	//////////////////////////////////////////////////////////////////////////
	/// Record 录像相关默认配置
	CConfigTable Record;

	for (int i = 0; i < 1; i++)
	{
		Record[i][Json::StaticString("PacketLength")] = 60;
		Record[i][Json::StaticString("PreRecord")] = 5;
		Record[i][Json::StaticString("Redundancy")] = false;
		Record[i][Json::StaticString("MuteRecord")] = false;
		Record[i][Json::StaticString("RecordEnable")] = true;
		Record[i][Json::StaticString("RecordMode")] = 3;//全天录像 RECORD_MODE_FULLTIME_ALARM
		
		for (int j = 0; j < N_WEEKS; j++)
		{
			for (int k = 0; k < N_TSECT; k++)
			{
				if (k == 0)
				{
					// 默认开通，Common, Alarm, Motion录像
					Record[i][Json::StaticString("Mask")][j][k] = Json::StaticString("0x00000007");
					Record[i][Json::StaticString("TimeSection")][j][k] = Json::StaticString("1 00:00:00-24:00:00");
				}
				else
				{
					Record[i][Json::StaticString("Mask")][j][k] = Json::StaticString("0x00000000");
					Record[i][Json::StaticString("TimeSection")][j][k] = Json::StaticString("0 00:00:00-24:00:00");
				}
			}
		}
	}
	g_configManager.setDefault(getConfigName(CFG_RECORD), Record);
}


void CMediaDefaultConfig::setMotionDetect()
{
	//////////////////////////////////////////////////////////////////////////
	/// MotionDetect 动态检测相关默认配置
#if 01 	//<shang>
	CConfigTable MotionDetect;

	for (int i = 0; i < 1; i++)
	{
		MotionDetect[i][Json::StaticString("Enable")] = true;
		MotionDetect[i][Json::StaticString("bRegionEnable")] = false;
		MotionDetect[i][Json::StaticString("Level")] = 0;
        MotionDetect[i][Json::StaticString("Interval")] = 60;
		
		MotionDetect[i][Json::StaticString("TimerCount")] = 0;
		for(int j = 0; j < 3; j++)
		{
			CConfigTable MotionDetectTimePoint;
			MotionDetectTimePoint[Json::StaticString("Hour")] = 0;
			MotionDetectTimePoint[Json::StaticString("Minute")] = 0;
			CConfigTable MotionDetectTimePeriod;
			MotionDetectTimePeriod[Json::StaticString("Begin")] = MotionDetectTimePoint;
			MotionDetectTimePeriod[Json::StaticString("End")] = MotionDetectTimePoint;
			MotionDetect[i][Json::StaticString("TimePeriod")][j] = MotionDetectTimePeriod;
		}

		MotionDetect[i][Json::StaticString("Region")][0] = Json::StaticString("0x00150028");// x:0,xlen:100;y:0,ylen:100 //Json::StaticString("0xFFFFFFFF");
		for (int j = 1; j < MD_REGION_ROW; j++)
		{
			MotionDetect[i][Json::StaticString("Region")][j] = Json::StaticString("0x00000000");// x:0,xlen:100;y:0,ylen:100 //Json::StaticString("0xFFFFFFFF");
		}
//		SetEventHandler(MotionDetect[i][Json::StaticString("EventHandler")]);
		
		MotionDetect[i][Json::StaticString("PersonFilterEnable")] = true;
		MotionDetect[i][Json::StaticString("PersonDetectEnable")] = false;
		MotionDetect[i][Json::StaticString("VehicleDetectEnable")] = false;
		MotionDetect[i][Json::StaticString("NonVehicleDetectEnable")] = false;
	}
	g_configManager.setDefault(getConfigName(CFG_MOTIONDETECT), MotionDetect);
#else
	CConfigTable MotionDetect;

	MotionDetect["Enable"] = false;
	MotionDetect["Sensitivity"] = 80; 		//灵敏度，0-100
	MotionDetect["Percentage"] = 30; 		//百分百，0-100
	MotionDetect["FrameInterval"] = 7; 	//触发间隔，0-7
printf("CMediaDefaultConfig::setMotionDetect()--------------->\n");
	g_configManager.setDefault(getConfigName(CFG_MOTIONDETECT), MotionDetect);
#endif
}



void CMediaDefaultConfig::SetEventHandler(CConfigTable &table)
{
	table[Json::StaticString("AlarmOutEnable")] = false;
	table[Json::StaticString("AlarmOutLatch")] = 10;
	table[Json::StaticString("AlarmOutMask")] = Json::StaticString("0x00000000");
	table[Json::StaticString("BeepEnable")] = false;
	table[Json::StaticString("EventLatch")] = 1;
	table[Json::StaticString("FTPEnable")] = false;
	table[Json::StaticString("LogEnable")] = false;
	table[Json::StaticString("MailEnable")] = false;
	table[Json::StaticString("MatrixEnable")] = false;
	table[Json::StaticString("MatrixMask")] = Json::StaticString("0x00000000");
	table[Json::StaticString("MessageEnable")] = false;
	table[Json::StaticString("MsgtoNetEnable")] = false;
	table[Json::StaticString("PtzEnable")] = false;
	for (int i = 0; i < N_SYS_CH; i++)
	{
		table[Json::StaticString("PtzLink")][i][0u] = Json::StaticString("None");
		table[Json::StaticString("PtzLink")][i][1] = 0;
	}
	table[Json::StaticString("RecordEnable")] = false;
	table[Json::StaticString("RecordLatch")] = 10;
	table[Json::StaticString("RecordMask")] = Json::StaticString("0x00000000");
	table[Json::StaticString("SnapEnable")] = false;
	table[Json::StaticString("SnapShotMask")] = Json::StaticString("0x00000000");
	table[Json::StaticString("TipEnable")] = false;
	table[Json::StaticString("TourEnable")] = false;
	table[Json::StaticString("TourMask")] = Json::StaticString("0x00000000");
	table[Json::StaticString("VoiceEnable")] = false;
	for (int j = 0; j < N_WEEKS; j++)
	{
		for (int k = 0; k < N_TSECT; k++)
		{
			if (k == 0)
			{
				table[Json::StaticString("TimeSection")][j][k] = Json::StaticString("1 00:00:00-24:00:00");
			}
			else
			{
				table[Json::StaticString("TimeSection")][j][k] = Json::StaticString("0 00:00:00-24:00:00");
			}
		}
	}
	table[Json::StaticString("ShowInfo")] = false;
	table[Json::StaticString("ShowInfoMask")] = Json::StaticString("0x00000000");
	table[Json::StaticString("AlarmInfo")] = Json::StaticString("");

	table[Json::StaticString("ShortMsgEnable")] = false;
	table[Json::StaticString("MultimediaMsgEnable")] = false;
}


void CMediaDefaultConfig::setMotionTrack()
{
	//////////////////////////////////////////////////////////////////////////
	/// MotionTrack 人体追踪相关默认配置
	CConfigTable MotionTrack;

	MotionTrack[Json::StaticString("Enable")] = false;
	MotionTrack[Json::StaticString("PersonEnable")] = false;
	g_configManager.setDefault(getConfigName(CFG_MOTIONTRACK), MotionTrack);
}

void CMediaDefaultConfig::setPir()
{
	//////////////////////////////////////////////////////////////////////////
	/// pir默认配置
	CConfigTable pir;

	pir[Json::StaticString("bpir1")] = 0;
	pir[Json::StaticString("bpir2")] = 0;
	pir[Json::StaticString("bpir3")] = 0;

	pir[Json::StaticString("Sensitivity")] = 0;//0-灵敏度低 1-灵敏度中 2-灵敏度高
	pir[Json::StaticString("PirLightOnTime")] = 0;//pir触发亮灯时长 1分钟 5分钟 10分钟
	
	g_configManager.setDefault(getConfigName(CFG_PIR), pir);
}

void CMediaDefaultConfig::setLuxLevel()
{
	//////////////////////////////////////////////////////////////////////////
	/// lux默认配置
	CConfigTable lux;
	lux[Json::StaticString("mode")] = 1;//Value: 0, 1, 2 0=Low，1=Middle（默认），2=High
	g_configManager.setDefault(getConfigName(CFG_LUX), lux);
}

void CMediaDefaultConfig::setVoice()
{
	CConfigTable voice;
	voice[Json::StaticString("voiceonoff")] = 0;//0-关闭 1-打开
	voice[Json::StaticString("voicesentivity")] = 0;//声音检测灵敏度
	g_configManager.setDefault(getConfigName(CFG_VOICE), voice);
}

void CMediaDefaultConfig::setSiren()
{
	/// siren默认配置
	CConfigTable siren;
	siren[Json::StaticString("autosiren")] = 0;//联动蜂鸣器开关
	siren[Json::StaticString("sirenlist")] = 0;//报警音列表
	siren[Json::StaticString("sirenduration")] = 60;//报警时长
	siren[Json::StaticString("sirenvolume")] = 100;//蜂鸣器音量
	g_configManager.setDefault(getConfigName(CFG_SIREN), siren);
}

void CMediaDefaultConfig::setIpcVol()
{
	/// siren默认配置
	CConfigTable ipcvol;
	ipcvol[Json::StaticString("ipcvolume")] = 100;//蜂鸣器音量
	g_configManager.setDefault(getConfigName(CFG_IPC_VOL), ipcvol);
}

void CMediaDefaultConfig::setVideo()
{
	/// video默认配置
	CConfigTable video;
	for (int i = 0; i < 2; i++)
	{
		video[i][Json::StaticString("enc_type")] = 1;
		if (0 == i)
			video[i][Json::StaticString("bit_rate")] = 2048;
		else
			video[i][Json::StaticString("bit_rate")] = 1024;
		video[i][Json::StaticString("frmae_rate")] = 15;
		video[i][Json::StaticString("gop")] = 50;
	}
	g_configManager.setDefault(getConfigName(CFG_VIDEO), video);
}

void CMediaDefaultConfig::setAudio()
{
	/// audio默认配置
	CConfigTable audio;
	audio[Json::StaticString("mic_enable")] = 1;
	audio[Json::StaticString("enc_type")] = 0;
	g_configManager.setDefault(getConfigName(CFG_AUDIO), audio);
}

void CMediaDefaultConfig::setOSDTime()
{
	/// OSD time默认配置
	CConfigTable table;
	table[Json::StaticString("date_type")] = 0;
	table[Json::StaticString("time_type")] = 0;
	table[Json::StaticString("display_week_enabled")] = 0;
	table[Json::StaticString("x")] = 0;
	table[Json::StaticString("y")] = 0;
	table[Json::StaticString("show")] = 1;
	table[Json::StaticString("alignment")] = 0;
	table[Json::StaticString("font_size")] = 32;
	table[Json::StaticString("font_color_mode")] = "customize";
	table[Json::StaticString("font_color")] = "#fff799";
	g_configManager.setDefault(getConfigName(CFG_OSD_TIME), table);
}

void CMediaDefaultConfig::setOSDText()
{
	/// OSD text默认配置
	CConfigTable table;
	for (int i = 0; i < 7; i++)
	{
		table[i][Json::StaticString("text")] = "";
		table[i][Json::StaticString("x")] = 0;
		table[i][Json::StaticString("y")] = 0;
		table[i][Json::StaticString("show")] = 0;
		table[i][Json::StaticString("alignment")] = 0;
	}
	g_configManager.setDefault(getConfigName(CFG_OSD_TEXT), table);
}

void CMediaDefaultConfig::setCmiot()
{
	/// 千里眼默认配置
	CConfigTable table;
	table[Json::StaticString("video_quality")] = 1;
	g_configManager.setDefault(getConfigName(CFG_CMIOT), table);
}

void CMediaDefaultConfig::setCmiotIntrude()
{
	/// 千里眼入侵默认配置
	CConfigTable table;
	table["rect_point"][0]["x"] = 0;
	table["rect_point"][0]["y"] = 0;
	table["rect_point"][1]["x"] = 1000;
	table["rect_point"][1]["y"] = 0;
	table["rect_point"][2]["x"] = 1000;
	table["rect_point"][2]["y"] = 1000;
	table["rect_point"][3]["x"] = 0;
	table["rect_point"][3]["y"] = 1000;
	table["person_report_en"] = 1;
	table["person_report_interval"] = 60;
	table["intrude_report_en"] = 1;
	table["intrude_report_interval"] = 60;
	table["real_time_frame"] = 0;
	table["intrude_sound_light_alarm"] = 0;
	g_configManager.setDefault(getConfigName(CFG_CMIOT_INTRUDE), table);
 }



