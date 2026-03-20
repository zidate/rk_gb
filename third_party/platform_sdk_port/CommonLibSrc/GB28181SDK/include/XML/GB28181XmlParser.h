#ifndef CGB28181XMLPARSER_H
#define CGB28181XMLPARSER_H
#include "GB28181Defs.h"
#include  "Interlocked.h"
#include  <vector>
#include <string>


enum ProtocolType
{

    kInvailProco = -1,
    kControlProco  = 0,
    kPtzControlProco,
    kTeleBootControlProco,
    kDeviceUpgradeControlProco,
    kRecordControlProco,
    kGurdControlProco,
    kAlarmResetControlProco,
    kZoomInControlProco,
    kZoomOutControlProco,
    kDevConfigControlProco,
    kIFameControlProco,
    kHomePositionControlProco,


    kQueryProco  = 100 ,				        //QUERY请求命令
    kDevStatusQueryProco ,				    //设备状态查询请求
    kCatalogQueryProco ,					    //设备目录信息查询请求
    kDevInfoQueryProco ,					    //设备信息查询请求
    kRecordIndexQueryProco ,					//文件目录检索请求
    kConfigDownloadQueryProco,				//设备配置查询
    kPresetQueryProco ,				            //设备预置位查询
    kAlarmQueryProco,						    //报警查询
    kMobilePositionQueryProco ,				//移动设备位置查询
	kCatalogSubscribeProco,                 //目录订阅请求
	kCatalogSubscribeExpireProco,                 //目录订阅超时
	kAlarmSubscribeProco,                   //报警事件订阅
	kAlarmSubscribeExpireProco,             //报警事件订阅超时
	kMobilePositionSubscribeProco,          //移动设备位置订阅
	kMobilePositionSubscribeExpireProco,    //移动设备位置订阅超时
	kCancelSubscribeProco,                  //取消订阅

    kNotifyProco = 200 ,				         //NOTIFY命令
    kKeepaliveNotifyProco ,					    //状态信息报送
    kAlarmNotifyProco ,						    //报警通知
    kMediaStatusNotifyProco ,				    // 媒体通知
    kBroadcastNotifyProco ,					    // 广播通知
    kMobilePositionNotifyProco ,			    // 移动设备位置数据通知
    kCatalogNotifyProco,                        //目录通知

    kResponseProco   = 300          ,		            //应答命令
    kControlResponseProco      ,			           //设备控制应答
    kAlarmResponseProco       ,					   //报警通知应答
    kCatalogResponseProco    ,					   //设备目录信息查询应答
    kDevInfoResponseProco  ,				           //设备信息查询应答
    kDevStatusResponseProco ,			           //设备状态信息查询应答
    kRecordIndexResponseProco ,				   //文件目录检索应答
    kDevConfSetResponseProco ,			           //设备配置应答
    kConfingDownloadResponseProco,		   //设备配置查询应答
    kPresetResponseProco ,				               //设备预置位查询应答
    kBroacastResponseProco ,				          //语音广播应答
};

extern  QueryType    GetQueryType( ProtocolType  type );
extern  ControlType  GetControlType( ProtocolType  type );
extern  NotifyType     GetNotifyType( ProtocolType  type );


class CMarkupSTL;

class CGB28181XmlParser
{
public:
    CGB28181XmlParser();
    void GetCmdType(const std::string& xml, std::string &result);
    int  GetControlType( CMarkupSTL* parse);
    int  GetProtocolType(const std::string& xml);
    int  GetSn();
   /*
    void PackBaseProcotol(const std::string&  request_type,  const std::string& cmd_type,  const std::string id , std::string &result);
    bool UnPackBaseProcotol(const std::string &xml_str, BaseProco* proco);
  */

    int PackCatalogQuery(const CatalogQueryParam* param, std::string &result);
    bool UnPackCatalogQuery(const std::string &xml_str, int& sn, QueryParam* param);

    int PackConfigDownloadQuery(const ConfigDownloadQuery* param, std::string &result);
    bool UnPackConfigDownloadQuery(const std::string &xml_str, int& sn, QueryParam* param);

    void PackConfigDownloadResponse(int sn,const DeviceConfigDownload* param, std::string &result);
    bool UnPackConfigDownloadResponse(const std::string &xml_str, int& sn, QueryParam* param);



    void PackCatalogResponse(int sn , int start, int end,const DeviceCatalogList* param, std::string &result);
    bool UnPackCatalogResponse(const std::string &xml_str, int& sn , int& SumList,  DeviceCatalogList* param);


    void PackKeepalive(const KeepaliveInfo*  list, std::string &result);
    bool UnPackKeepalive(const std::string &xml_str, std::string& code, KeepaliveInfo*  list);


    bool UnPackNotifyInfo(const std::string &xml_str,int& sn, NotifyInfo&  info);


    bool UnPackDeviceControl( const std::string &xml_str, int &sn, DevControlCmd& cmd  );


    bool UnPackDeviceQuery( const std::string &xml_str, int &sn,  QueryParam& param  );


    bool UnPackQuery(const std::string &xml_str, int &sn, std::string& id);

    int PackDeviceInfoQuery(const char* id, std::string &result);
    void PackDeviceInfoResponse(int sn ,const DeviceInfo* device_info, std::string &result);
    bool UnPackDeviceInfoResponse(const std::string &xml_str, int& sn, DeviceInfo* device_info);


    int PackDeviceStatusQuery(const char* device_id, std::string &result);
    void PackDeviceStatusResponse(int sn ,const DeviceStatus* device_status, std::string &result);
    bool UnPackDeviceStatusResponse(const std::string &xml_str, int& sn,DeviceStatus* device_status);



    int PackDeviceRecordQuery(const RecordParam*  param, std::string &result);
    bool UnPackDeviceRecordQuery(const std::string &xml_str, int& sn ,QueryParam* param);


 ///   int PackConfigDownloadQuery(const ConfigDownloadQuery*  param, std::string &result);
    //bool UnPackConfigDownloadQuery(const std::string &xml_str, int& sn ,RecordParam* record);


    int PackQueryDevicePresetInfo(const char* device_id, std::string &result);
    void PackResponseDevicePresetInfo(int sn ,DeviceStatus* device_status, std::string &result);
    bool UnPackResponseDevicePresetInfo(const std::string &xml_str, DeviceStatus* device_status);


    void PackGurdControl(const char* device_id,bool opt, std::string &result);
    bool UnPackGurdControl(const std::string &xml_str, int& sn , DevControlCmd& cmd);

    void PackHomePositionControl(const char* device_id, const HomePositionCmd* cmd, std::string &result);
    bool UnPackHomePositionControl(const std::string &xml_str, int& sn , DevControlCmd& cmd);
    bool UnPackDeviceUpgradeControl(const std::string &xml_str, int& sn , DevControlCmd& cmd);

    void PackAlarmResetControl(const char* device_id,  const AlarmResetInfo* info,    std::string &result);
    bool UnPackAlarmResetControl(const std::string &xml_str, int& sn , DevControlCmd& cmd);


    void PackKeyFrameControl(const char* device_id, std::string &result);
    bool UnPackKeyFrameControl(const std::string &xml_str, int& sn,  DevControlCmd& cmd);


    void PackPTZControl(const char* device_id, const PtzCmd* cmd,std::string &result);
    bool UnPackPTZControl(const std::string &xml_str, int& sn,  DevControlCmd& cmd);


    void PackRecordControl(const char* device_id, bool start, std::string &result);
    bool UnPackRecordControl(const std::string &xml_str, int& sn, DevControlCmd& cmd);

    void PackZoomControl(const char* device_id, const ZoomCmd* cmd,  std::string &result);
    bool UnPackZoomControl(const std::string &xml_str, int& sn , DevControlCmd& cmd);

    void PackAlarmSubcribe(const AlarmSubcribeInfo* info,std::string &result);
    bool UnPackAlarmSubcribe(const std::string &xml_str, int& sn, std::string& code, AlarmSubcribeInfo* info);


    void PackAlarmNotify(int sn ,const AlarmNotifyInfo* info, std::string &result);
    bool UnPackAlarmNotify(const std::string &xml_str, int& sn,  std::string& code ,AlarmNotifyInfo* info);

    bool PackMediaStatusNotify(const char* id, int NotifyType, std::string &result);
    bool UnPackMediaStatusNotify(const std::string &xml_str, std::string code, MediaStatusNotify& status);
    bool PackDeviceUpgradeResultNotify(const char* device_id, const char* session_id, const char* firmware, bool result, const char* description, std::string &output);


    void PackResponse(const std::string& cmd, int sn,  bool result, const char* device_id, std::string & output);
    bool UnPackResponse(const std::string &xml_str,bool& result, std::string& device_id );


    void PackCatalogSubcribe(const CatalogSubcribeInfo* info, std::string &result);
    bool UnPackCatalogSubcribe(const std::string &xml_str, int& sn, std::string& code,CatalogSubcribeInfo* info);

	void PackCatalogNotify(int sn , int position,const DeviceCatalogList* info, std::string &result);
	bool UnPackCatalogNotify(const std::string &xml_str, int& sn ,std::string &code,DeviceCatalogList* info);


    void PackMobilePositionSubcribe(const MobilePositionSubInfo* info, std::string &result);
    bool UnPackMobilePositionSubcribe(const std::string &xml_str, int& sn, std::string& code, MobilePositionSubInfo* info);


    void PackMobilePositionNotify(int sn ,const MobilePositionInfo* info, std::string &result);
    bool UnPackMobilePositionNotify(const std::string &xml_str, int& sn, std::string& code, MobilePositionInfo* info);


    void PackDevicePresetResponse(int sn , const PresetInfo* info, std::string &result);
    bool UnPackDevicePresetResponse(const std::string &xml_str, int& sn,  PresetInfo* info);

    void PackDeviceRecordIndexResponse(int sn ,const RecordIndex* index, std::string &result);
    void PackDeviceRecordIndexResponseEx(int sn,
                                         const char* device_id,
                                         unsigned int total_num,
                                         const RecordParam* record_list,
                                         unsigned int record_num,
                                         std::string &result);
    bool UnPackDeviceRecordIndexResponse(const std::string &xml_str,  int& sn, int& sum,   RecordIndex* index);


	bool UnPackBroadcastNotify(const std::string &xml_str, int& sn, std::string& code, BroadcastInfo& meida_status);
	bool UnPackTeleBootControl(const std::string &xml_str, int& sn , DevControlCmd& cmd);
	bool UnPackConfigControl(const std::string &xml_str, int& sn , DevControlCmd& cmd);


    GB28181Version      m_version;
    std::string               m_local_code;
    Interlocked              m_sn;
    std::string               m_local_ip;
    int                         m_port;
};

#endif // CGB28181XMLPARSER_H
