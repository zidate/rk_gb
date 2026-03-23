#include "LowerGAT1400SDK.h"

#include "GAT1400ClientService.h"

namespace protocol
{
class ProtocolManager
{
public:
    const ProtocolExternalConfig& GetConfig() const;
    GAT1400ClientService* GetGatClientService();
    const GAT1400ClientService* GetGatClientService() const;
};
}

class CSofia
{
public:
    static CSofia* instance();
    protocol::ProtocolManager* GetProtocolManager();
    const protocol::ProtocolManager* GetProtocolManager() const;
};

namespace
{

protocol::ProtocolManager* GetProtocolManagerInstance()
{
    CSofia* app = CSofia::instance();
    return app != NULL ? app->GetProtocolManager() : NULL;
}

protocol::GAT1400ClientService* GetGatClientServiceInstance()
{
    protocol::ProtocolManager* manager = GetProtocolManagerInstance();
    return manager != NULL ? manager->GetGatClientService() : NULL;
}

int FillStartConfig(const REGIST_PARAM_S* regist,
                    const CONNECT_PARAM_S* connect,
                    tuint32 listenPort,
                    protocol::ProtocolExternalConfig& cfg,
                    protocol::GbRegisterParam& gbRegister)
{
    if (regist == NULL || connect == NULL || connect->ip[0] == '\0' || connect->port == 0) {
        return -1;
    }

    cfg.gat_register.scheme = cfg.gat_register.scheme.empty() ? "http" : cfg.gat_register.scheme;
    cfg.gat_register.server_ip = connect->ip;
    cfg.gat_register.server_port = connect->port;
    cfg.gat_register.device_id = connect->GBCode;
    cfg.gat_register.listen_port = listenPort > 0 ? static_cast<int>(listenPort) : cfg.gat_register.listen_port;
    cfg.gat_register.expires_sec = regist->expires > 0 ? static_cast<int>(regist->expires) : cfg.gat_register.expires_sec;
    cfg.gat_register.username = regist->username;
    cfg.gat_register.password = regist->password;
    if (regist->auth[0] != '\0') {
        cfg.gat_register.auth_method = regist->auth;
    }

    if (gbRegister.device_id.empty()) {
        gbRegister.device_id = connect->GBCode;
    }
    if (gbRegister.username.empty()) {
        gbRegister.username = regist->username;
    }

    return 0;
}

template <typename Fn>
int InvokeWithGatService(const Fn& fn)
{
    protocol::GAT1400ClientService* service = GetGatClientServiceInstance();
    if (service == NULL) {
        return -1;
    }
    return fn(*service);
}

}  // namespace

int LOWER_1400_START(const REGIST_PARAM_S* regist, const CONNECT_PARAM_S* connect, tuint32 listenPort)
{
    protocol::ProtocolManager* manager = GetProtocolManagerInstance();
    protocol::GAT1400ClientService* service = GetGatClientServiceInstance();
    if (manager == NULL || service == NULL) {
        return -1;
    }

    protocol::ProtocolExternalConfig cfg = manager->GetConfig();
    protocol::GbRegisterParam gbRegister = cfg.gb_register;
    const int fillRet = FillStartConfig(regist, connect, listenPort, cfg, gbRegister);
    if (fillRet != 0) {
        return fillRet;
    }

    return service->Start(cfg, gbRegister);
}

void LOWER_1400_STOP()
{
    protocol::GAT1400ClientService* service = GetGatClientServiceInstance();
    if (service != NULL) {
        service->Stop();
    }
}

int LOWER_1400_REGISTER()
{
    return InvokeWithGatService([](protocol::GAT1400ClientService& service) { return service.Register(); });
}

int LOWER_1400_UNREGISTER()
{
    return InvokeWithGatService([](protocol::GAT1400ClientService& service) { return service.Unregister(); });
}

int LOWER_1400_KEEPALIVE()
{
    return InvokeWithGatService([](protocol::GAT1400ClientService& service) { return service.Keepalive(); });
}

int LOWER_1400_GET_TIME(std::string& strTime)
{
    protocol::GAT1400ClientService* service = GetGatClientServiceInstance();
    if (service == NULL) {
        return -1;
    }
    return service->GetTime(strTime);
}

int LOWER_1400_POST_FACES(const std::list<GAT_1400_Face>& FaceList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostFaces(FaceList); });
}

int LOWER_1400_POST_PERSONS(const std::list<GAT_1400_Person>& PersonList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostPersons(PersonList); });
}

int LOWER_1400_POST_MOTORVEHICLES(const std::list<GAT_1400_Motor>& MotorVehicleList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostMotorVehicles(MotorVehicleList); });
}

int LOWER_1400_POST_NONMOTORVEHICLES(const std::list<GAT_1400_NonMotor>& NonmotorVehicleList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostNonMotorVehicles(NonmotorVehicleList); });
}

int LOWER_1400_POST_THINGS(const std::list<GAT_1400_Thing>& ThingList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostThings(ThingList); });
}

int LOWER_1400_POST_SCENES(const std::list<GAT_1400_Scene>& SceneList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostScenes(SceneList); });
}

int LOWER_1400_POST_VIDEOSLICES(const std::list<GAT_1400_VideoSliceSet>& VideoSliceList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostVideoSlices(VideoSliceList); });
}

int LOWER_1400_POST_IMAGES(const std::list<GAT_1400_ImageSet>& ImageList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostImages(ImageList); });
}

int LOWER_1400_POST_FILES(const std::list<GAT_1400_FileSet>& FileList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostFiles(FileList); });
}

int LOWER_1400_POST_ANALYSISRULES(const std::list<GAT_1400_AnalysisRule>& RuleList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostAnalysisRules(RuleList); });
}

int LOWER_1400_POST_VIDEOLABELS(const std::list<GAT_1400_VideoLabel>& LabelList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostVideoLabels(LabelList); });
}

int LOWER_1400_POST_DISPOSITIONS(const std::list<GAT_1400_Disposition>& DispositionList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostDispositions(DispositionList); });
}

int LOWER_1400_POST_DISPOSITIONNOTIFICATIONS(const std::list<GAT_1400_Disposition_Notification>& DispositionNotificationList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) {
        return service.PostDispositionNotifications(DispositionNotificationList);
    });
}

int LOWER_1400_POST_SUBSCRIBES(const std::list<GAT_1400_Subscribe>& SubscribeList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostSubscribes(SubscribeList); });
}

int LOWER_1400_POST_SUBSCRIBENOTIFICATIONS(const std::list<GAT_1400_Subscribe_Notification>& SubscribeNotificationList,
                                           const std::string& url)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) {
        return service.PostSubscribeNotifications(SubscribeNotificationList, url);
    });
}

int LOWER_1400_SET_1400INHTTPCMD(const std::string& url, tuint32 nMethod, tuint32 nFormat, const std::string& strContent)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) {
        return service.SendHttpCommand(url, nMethod, nFormat, strContent);
    });
}

int LOWER_1400_POST_APES(const std::list<GAT_1400_Ape>& ApeList)
{
    return InvokeWithGatService([&](protocol::GAT1400ClientService& service) { return service.PostApes(ApeList); });
}

void LOWER_1400_ADD_SUBSCRIBE_OBSERVER(CLower1400SubscribeObserver* pSubscribeObserver)
{
    protocol::GAT1400ClientService* service = GetGatClientServiceInstance();
    if (service != NULL) {
        service->AddSubscribeObserver(pSubscribeObserver);
    }
}

void LOWER_1400_DEL_SUBSCRIBE_OBSERVER(CLower1400SubscribeObserver* pSubscribeObserver)
{
    protocol::GAT1400ClientService* service = GetGatClientServiceInstance();
    if (service != NULL) {
        service->RemoveSubscribeObserver(pSubscribeObserver);
    }
}

void LOWER_1400_ADD_REGIST_OBSERVER(CLower1400RegistStatusObserver* pRegistObserver)
{
    protocol::GAT1400ClientService* service = GetGatClientServiceInstance();
    if (service != NULL) {
        service->AddRegistStatusObserver(pRegistObserver);
    }
}

void LOWER_1400_DEL_REGIST_OBSERVER(CLower1400RegistStatusObserver* pRegistObserver)
{
    protocol::GAT1400ClientService* service = GetGatClientServiceInstance();
    if (service != NULL) {
        service->RemoveRegistStatusObserver(pRegistObserver);
    }
}
