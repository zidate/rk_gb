#ifndef __GAT1400_CLIENT_SERVICE_H__
#define __GAT1400_CLIENT_SERVICE_H__

#include <atomic>
#include <ctime>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Media/GAT1400CaptureControl.h"
#include "ProtocolExternalConfig.h"
#define ConnectParam GAT1400ConnectParam
#include "CMS1400Struct.h"
#undef ConnectParam
#include "LowerGAT1400Observer.h"

namespace protocol
{

class GAT1400ClientService
{
public:
    struct HttpResponse
    {
        int status_code;
        std::map<std::string, std::string> headers;
        std::string body;

        HttpResponse() : status_code(0) {}
    };

    struct PendingUploadItem
    {
        std::string id;
        std::string method;
        std::string path;
        std::string content_type;
        std::string response_kind;
        std::string body;
        std::string category;
        std::string object_id;
        std::string last_error;
        std::string persist_path;
        int attempt_count;
        time_t enqueue_time;

        PendingUploadItem() : attempt_count(0), enqueue_time(0) {}
    };

    GAT1400ClientService();
    ~GAT1400ClientService();

    int Start(const ProtocolExternalConfig& cfg, const GbRegisterParam& gbRegister);
    void Stop();
    int Reload(const ProtocolExternalConfig& cfg, const GbRegisterParam& gbRegister);

    bool IsStarted() const;

    void AddSubscribeObserver(CLower1400SubscribeObserver* observer);
    void RemoveSubscribeObserver(CLower1400SubscribeObserver* observer);
    void AddRegistStatusObserver(CLower1400RegistStatusObserver* observer);
    void RemoveRegistStatusObserver(CLower1400RegistStatusObserver* observer);

    int GetTime(std::string& outTime);
    int Register();
    int Unregister();
    int Keepalive();
    int SendHttpCommand(const std::string& url, unsigned int method, unsigned int format, const std::string& content);
    int PostFaces(const std::list<GAT_1400_Face>& faceList);
    int PostPersons(const std::list<GAT_1400_Person>& personList);
    int PostMotorVehicles(const std::list<GAT_1400_Motor>& motorList);
    int PostNonMotorVehicles(const std::list<GAT_1400_NonMotor>& nonMotorList);
    int PostThings(const std::list<GAT_1400_Thing>& thingList);
    int PostScenes(const std::list<GAT_1400_Scene>& sceneList);
    int PostVideoSlices(const std::list<GAT_1400_VideoSliceSet>& videoSliceList);
    int PostImages(const std::list<GAT_1400_ImageSet>& imageList);
    int PostFiles(const std::list<GAT_1400_FileSet>& fileList);
    int PostAnalysisRules(const std::list<GAT_1400_AnalysisRule>& ruleList);
    int PostVideoLabels(const std::list<GAT_1400_VideoLabel>& labelList);
    int PostDispositions(const std::list<GAT_1400_Disposition>& dispositionList);
    int PostDispositionNotifications(const std::list<GAT_1400_Disposition_Notification>& notificationList);
    int PostSubscribes(const std::list<GAT_1400_Subscribe>& subscribeList);
    int PostSubscribeNotifications(const std::list<GAT_1400_Subscribe_Notification>& notificationList,
                                   const std::string& url);
    int PostApes(const std::list<GAT_1400_Ape>& apeList);
    int NotifyCaptureEvent(const media::GAT1400CaptureEvent& event);

    std::list<GAT_1400_Subscribe> GetSubscriptions() const;
    int PostJsonWithResponseList(const char* action,
                                 const char* path,
                                 const std::string& body,
                                 const std::string* overrideUrl = NULL);

private:
    int StartServerLocked();
    void StopServerLocked();
    void ServerLoop();
    void HandleServerConnection(int clientFd);
    int HandleSubscribeHttp(const std::string& method,
                            const std::string& path,
                            const std::string& query,
                            const std::string& body,
                            int& outStatus,
                            std::string& outBody);

    int RegisterNow();
    int UnregisterNow();
    int SendKeepaliveNow();
    void HeartbeatLoop();

    int ExecuteRequest(const ProtocolExternalConfig& cfg,
                       const std::string& deviceId,
                       const std::string& method,
                       const std::string& path,
                       const std::string& contentType,
                       const std::string& body,
                       HttpResponse& response,
                       const std::string* overrideUrl = NULL) const;

    int PostJsonWithResponseStatus(const char* action,
                                   const char* path,
                                   const std::string& body,
                                   const std::string* overrideUrl = NULL);
    int PostBinaryData(const char* action,
                       const std::string& path,
                       const std::string& data,
                       const std::string* overrideUrl = NULL);
    int EnqueuePendingUpload(const char* action,
                             const std::string& method,
                             const std::string& path,
                             const std::string& contentType,
                             const std::string& responseKind,
                             const std::string& body,
                             const std::string& objectId,
                             const std::string& lastError);
    void LoadPendingUploadsLocked(const ProtocolExternalConfig& cfg);
    void ClearPendingUploadsLocked();
    int ReplayPendingUploads();
    int ReplayPendingUploadsIfDue();
    int DrainPendingCaptureEvents();
    int PostCaptureEvent(const media::GAT1400CaptureEvent& event);
    int SendKeepaliveDemoUploadOnce(const std::string& deviceId);

    int SnapshotConfig(ProtocolExternalConfig& cfg, std::string& deviceId) const;
    void UpdateRegistState(regist_state state);

    void DispatchSubscribeAdd(const GAT_1400_Subscribe& subscribe);
    void DispatchSubscribeUpdate(const GAT_1400_Subscribe& subscribe);
    void DispatchSubscribeDelete(const std::string& subscribeId);

private:
    mutable std::mutex m_state_mutex;
    ProtocolExternalConfig m_cfg;
    bool m_started;
    bool m_registered;
    regist_state m_regist_state;
    int m_listen_fd;
    std::thread m_server_thread;
    std::atomic<bool> m_server_running;
    std::thread m_heartbeat_thread;
    std::atomic<bool> m_heartbeat_running;

    mutable std::mutex m_subscribe_mutex;
    std::map<std::string, GAT_1400_Subscribe> m_subscriptions;
    mutable std::mutex m_pending_mutex;
    std::list<PendingUploadItem> m_pending_uploads;
    unsigned long long m_pending_seq;
    time_t m_last_replay_time;
    std::mutex m_capture_upload_mutex;
    std::atomic<bool> m_keepalive_demo_upload_attempted;

    mutable std::mutex m_observer_mutex;
    std::vector<CLower1400SubscribeObserver*> m_subscribe_observers;
    std::vector<CLower1400RegistStatusObserver*> m_regist_observers;
};

}

#endif


