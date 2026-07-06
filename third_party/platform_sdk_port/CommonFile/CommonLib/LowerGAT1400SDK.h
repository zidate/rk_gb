#ifndef __LOWER_GAT_1400_SDK_H_
#define __LOWER_GAT_1400_SDK_H_

#include "LowerGAT1400Observer.h"
#include "base_type_define.h"

#ifdef WIN32
#ifdef GAT1400_EXPORTS
#define EXPORT_GAT1400_SDK  __declspec(dllexport)
#else
#define EXPORT_GAT1400_SDK __declspec(dllimport)
#endif

#else
#define EXPORT_GAT1400_SDK  __attribute__ ((visibility("default")))
#endif


EXPORT_GAT1400_SDK int LOWER_1400_START(const REGIST_PARAM_S* regist, const CONNECT_PARAM_S* connect, tuint32 listenPort);
EXPORT_GAT1400_SDK void LOWER_1400_STOP();
EXPORT_GAT1400_SDK int LOWER_1400_REGISTER();//注册
EXPORT_GAT1400_SDK int LOWER_1400_UNREGISTER();//注销
EXPORT_GAT1400_SDK int LOWER_1400_KEEPALIVE();//保活
EXPORT_GAT1400_SDK int LOWER_1400_GET_TIME(std::string& strTime);//获取时间
EXPORT_GAT1400_SDK int LOWER_1400_POST_FACES(const std::list<GAT_1400_Face>& FaceList);//上报人脸
EXPORT_GAT1400_SDK int LOWER_1400_POST_PERSONS(const std::list<GAT_1400_Person>& PersonList);//上报人
EXPORT_GAT1400_SDK int LOWER_1400_POST_MOTORVEHICLES(const std::list<GAT_1400_Motor>& MotorVehicleList);//上报机动车
EXPORT_GAT1400_SDK int LOWER_1400_NOTIFY_PLATEDETECTIONS(const std::list<GAT_1400_Motor>& MotorVehicleList);//异步上报车牌检测
EXPORT_GAT1400_SDK int LOWER_1400_POST_NONMOTORVEHICLES(const std::list<GAT_1400_NonMotor>& NonmotorVehicleList);//上报非机动车
EXPORT_GAT1400_SDK int LOWER_1400_POST_THINGS(const std::list<GAT_1400_Thing>& ThingList);//上报物品
EXPORT_GAT1400_SDK int LOWER_1400_POST_SCENES(const std::list<GAT_1400_Scene>& SceneList);//上报场景
EXPORT_GAT1400_SDK int LOWER_1400_POST_VIDEOSLICES(const std::list<GAT_1400_VideoSliceSet>& VideoSliceList);//上报批量视频片段集合
EXPORT_GAT1400_SDK int LOWER_1400_POST_IMAGES(const std::list<GAT_1400_ImageSet>& ImageList);//上报采集图像集合
EXPORT_GAT1400_SDK int LOWER_1400_POST_FILES(const std::list<GAT_1400_FileSet>& FileList);//上报采集文件集合
EXPORT_GAT1400_SDK int LOWER_1400_POST_ANALYSISRULES(const std::list<GAT_1400_AnalysisRule>& RuleList);//上报视频图像分析规则
EXPORT_GAT1400_SDK int LOWER_1400_POST_VIDEOLABELS(const std::list<GAT_1400_VideoLabel>& LabelList);//上报视频图像标签
EXPORT_GAT1400_SDK int LOWER_1400_POST_DISPOSITIONS(const std::list<GAT_1400_Disposition>& DispositionList);//上报批量布控
EXPORT_GAT1400_SDK int LOWER_1400_POST_DISPOSITIONNOTIFICATIONS(const std::list<GAT_1400_Disposition_Notification>& DispositionNotificationList);//上报批量告警
EXPORT_GAT1400_SDK int LOWER_1400_POST_SUBSCRIBES(const std::list<GAT_1400_Subscribe>& SubscribeList);//上报批量订阅
EXPORT_GAT1400_SDK int LOWER_1400_POST_SUBSCRIBENOTIFICATIONS(const std::list<GAT_1400_Subscribe_Notification>& SubscribeNotificationList, const std::string& url);//上报订阅通知
EXPORT_GAT1400_SDK int LOWER_1400_SET_1400INHTTPCMD(const std::string& url, tuint32 nMethod, tuint32 nFormat, const std::string& strContent);//透传1400In上报的数据
EXPORT_GAT1400_SDK int LOWER_1400_POST_APES(const std::list<GAT_1400_Ape>& ApeList);//上报采集设备
EXPORT_GAT1400_SDK void LOWER_1400_ADD_SUBSCRIBE_OBSERVER(CLower1400SubscribeObserver* pSubscribeObserver);//添加订阅观察者
EXPORT_GAT1400_SDK void LOWER_1400_DEL_SUBSCRIBE_OBSERVER(CLower1400SubscribeObserver* pSubscribeObserver);//删除订阅观察者
EXPORT_GAT1400_SDK void LOWER_1400_ADD_REGIST_OBSERVER(CLower1400RegistStatusObserver* pRegistObserver);//添加注册状态观察者
EXPORT_GAT1400_SDK void LOWER_1400_DEL_REGIST_OBSERVER(CLower1400RegistStatusObserver* pRegistObserver);//删除注册状态观察者


#endif
