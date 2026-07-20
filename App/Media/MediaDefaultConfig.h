#ifndef __MEDIA_DEFAULTCONFIG_H__
#define __MEDIA_DEFAULTCONFIG_H__

#include "Common.h"


/// 默认配置事务类，设置配置的默认配置
/// 目前暂时统一处理，等后续各个模块的耦合性降低以后将由各个模块内部自己管理2008-12-10
class CMediaDefaultConfig
{
	PATTERN_SINGLETON_DECLARE(CMediaDefaultConfig);

public:

	/// 设置默认配置
	bool start();
	void setMotionDetect();
	void setRecord();
	void setMotionTrack();
	void setPir();
	void setLuxLevel();
	void setVoice();
	void setSiren();
	void setIpcVol();
	void setVideo();
	void setAudio();
	void setOSDTime();
	void setOSDText();
	void setCmiot();
	void setCmiotIntrude();
	void setCmiotOsd();
private:
	void SetEventHandler(CConfigTable &table);
};

#endif
