#ifndef __PROTOCOL_MANAGER_H__

#define __PROTOCOL_MANAGER_H__



#include <memory>

#include <mutex>

#include <stddef.h>

#include <stdint.h>

#include <string>

#include <thread>

#include <atomic>

#include <vector>



#include "ProtocolService.h"

#include "LocalConfigProvider.h"

#include "GB28181BroadcastBridge.h"

#include "GB28181ListenBridge.h"

#include "GB28181RtpPsSender.h"

#include "GB28181ClientReceiverAdapter.h"

#include "Storage_api.h"



class GB28181ClientSDK;

namespace protocol
{

class GAT1400ClientService;

class ProtocolManager : public ProtocolService

{

public:

    typedef int (*GbMediaPlayInfoResponder)(StreamHandle handle, const MediaInfo* info, void* userData);

    struct GbTimeSyncInfo
    {
        std::string raw_date;
        std::string utc_time;
        std::string local_time;
        int64_t epoch_sec;
        bool dry_run;

        GbTimeSyncInfo()
            : epoch_sec(0),
              dry_run(true)
        {
        }
    };

    typedef int (*GbTimeSyncHook)(const GbTimeSyncInfo& info, void* userData);



    ProtocolManager();

    ~ProtocolManager();



    virtual int Init(const std::string& configEndpoint);

    virtual void UnInit();



    virtual int Start();

    virtual void Stop();

    int GBManager_Start();

    int GBManager_Stop();

    void OnGbConfigChanged();

    virtual int ReloadExternalConfig();

    const ProtocolExternalConfig& GetConfig() const;



    int PushLiveVideoEsFrame(const uint8_t* data, size_t size, uint64_t pts90k, int mediaType, bool keyFrame);

    int PushLiveAudioEsFrame(const uint8_t* data, size_t size, uint64_t pts90k);

    int PushListenAudioFrame(const uint8_t* data, size_t size, uint64_t timestamp);



    int ApplyGbBroadcastSdpOffer(const std::string& sdp, const std::string& remoteIpHint);

    int ApplyGbBroadcastTransportHint(const std::string& remoteIp,

                                      int remotePort,

                                      int payloadType,

                                      const std::string& codec,

                                      int transportType);

    std::string BuildGbBroadcastAnswerSdp(const std::string& localIp) const;

    int BuildGbBroadcastMediaInfo(const std::string& localIp,

                                  const std::string& deviceId,

                                  MediaInfo& out,

                                  RtpMap& outMap) const;

    int HandleGbBroadcastNotify(const char* gbCode, const BroadcastInfo* info);
    int HandleGbBroadcastNotifyResponse(const char* gbCode, const BroadcastInfo* info, bool ok);



    int HandleGbAudioStreamRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input);

    int HandleGbLiveStreamRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input);

    int HandleGbPlaybackRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input);

    int HandleGbDownloadRequest(StreamHandle handle, const char* gbCode, const MediaInfo* input);

    int HandleGbStreamAck(StreamHandle handle);

    int HandleGbPlayControl(StreamHandle handle, const PlayCtrlCmd* cmd);

    int HandleGbDeviceControl(ResponseHandle handle, const DevControlCmd* cmd);

    int HandleGbSubscribe(SubscribeHandle handle, SubscribeType type, const char* gbCode, void* info);

    int HandleGbQueryRequest(ResponseHandle handle, const QueryParam* param);

    int NotifyGbCatalog(const char* gbCode);

    int NotifyGbAlarm(AlarmNotifyInfo* info);

    int NotifyGbMobilePosition(const MobilePositionInfo* info);

    void HandleGbStopStreamRequest(StreamHandle handle, const char* gbCode);



    void SetGbMediaPlayInfoResponder(GbMediaPlayInfoResponder responder, void* userData);

    void SetGbTimeSyncHook(GbTimeSyncHook hook, void* userData);

    void SetGbBroadcastPcmCallback(GB28181BroadcastBridge::PcmFrameCallback cb, void* userData);



    void BindGbClientSdk(GB28181ClientSDK* sdk);

    void UnbindGbClientSdk();

    GAT1400ClientService* GetGatClientService();

    const GAT1400ClientService* GetGatClientService() const;



    GB28181ClientReceiverAdapter* GetGbClientReceiver();

    const GB28181ClientReceiverAdapter* GetGbClientReceiver() const;



private:

    struct GbReplaySession

    {

        bool active;

        bool download;

        StreamHandle stream_handle;

        int storage_handle;

        std::string gb_code;

        uint64_t start_time;

        uint64_t end_time;

        uint64_t last_pts90k;

        bool acked;

        uint32_t sent_video_frames;

        uint32_t sent_audio_frames;

        uint64_t generation;



        GbReplaySession()

            : active(false),

              download(false),

              stream_handle(NULL),

              storage_handle(-1),

              start_time(0),

              end_time(0),

              last_pts90k(0),

              acked(false),

              sent_video_frames(0),

              sent_audio_frames(0),

              generation(0)

        {

        }

    };

    struct GbReplayCallbackContext

    {

        ProtocolManager* manager;

        uint64_t generation;

        bool download;

        GbReplayCallbackContext()

            : manager(NULL),

              generation(0),

              download(false)

        {

        }

    };



    struct GbLiveSession

    {

        bool active;

        StreamHandle stream_handle;

        std::string gb_code;

        bool audio_requested;

        bool audio_enabled;

        bool prefer_sub_stream;

        bool acked;

        bool wait_first_idr;

        uint32_t recv_video_frames;

        uint32_t recv_audio_frames;

        uint32_t sent_video_frames;

        uint32_t sent_audio_frames;

        uint64_t last_capture_log_ms;

        uint64_t last_wait_idr_log_ms;



        GbLiveSession()

            : active(false),

              stream_handle(NULL),

              audio_requested(false),

              audio_enabled(false),

              prefer_sub_stream(false),

              acked(false),

              wait_first_idr(false),

              recv_video_frames(0),

              recv_audio_frames(0),

              sent_video_frames(0),

              sent_audio_frames(0),

              last_capture_log_ms(0),

              last_wait_idr_log_ms(0)

        {

        }

    };

    struct GbBroadcastSession

    {

        bool active;

        bool acked;

        StreamHandle stream_handle;

        std::string gb_code;

        std::string source_id;

        std::string target_id;

        std::string remote_ip;

        std::string local_ip;

        int remote_port;

        int local_port;

        int payload_type;

        int transport_type;

        std::string codec;

        uint64_t notify_ts_ms;

        uint64_t invite_ts_ms;

        GbBroadcastSession()

            : active(false),

              acked(false),

              stream_handle(NULL),

              remote_port(0),

              local_port(0),

              payload_type(-1),

              transport_type(kRtpOverUdp),

              notify_ts_ms(0),

              invite_ts_ms(0)

        {

        }

    };

    struct GbTalkSession

    {

        bool active;

        bool acked;

        bool uplink_started;

        StreamHandle stream_handle;

        std::string gb_code;

        std::string remote_ip;

        std::string local_ip;

        int remote_port;

        int local_port;

        int payload_type;

        int request_transport_type;

        int answer_transport_type;

        int sample_rate;

        std::string codec;

        uint64_t invite_ts_ms;

        uint64_t ack_ts_ms;

        GbTalkSession()

            : active(false),

              acked(false),

              uplink_started(false),

              stream_handle(NULL),

              remote_port(0),

              local_port(0),

              payload_type(-1),

              request_transport_type(kRtpOverUdp),

              answer_transport_type(kRtpOverUdp),

              sample_rate(8000),

              invite_ts_ms(0),

              ack_ts_ms(0)

        {

        }

    };



    static int OnGbMediaPlayInfoRespond(StreamHandle handle, const MediaInfo* info, void* userData);

    static void OnGbReplayStorageFrame(unsigned char* data, int size, Mp4DemuxerFrameInfo_s* frameInfo, void* userData);

    static int OnGbLiveCapture(int media_chn,

                                  int media_type,

                                  int media_subtype,

                                  unsigned long long frame_pts,

                                  unsigned char* frame_data,

                                  int frame_len,

                                  int frame_end_flag);



    int StartGbReplaySession(StreamHandle handle, const char* gbCode, const MediaInfo* input, bool download);

    int ReconfigureGbLiveSender(const MediaInfo* input, const char* gbCode, StreamRequestType requestType);

    int RespondGbMediaPlayInfo(StreamHandle handle, const char* gbCode, StreamRequestType requestType, const MediaInfo* input);

    int BuildGbResponseMediaInfo(const char* gbCode,

                                 StreamRequestType requestType,

                                 const MediaInfo* input,

                                 MediaInfo& out,

                                 RtpMap& outMap) const;

    int BuildGbTalkMediaInfo(const char* gbCode,

                            const MediaInfo* input,

                            const std::string& localIp,

                            MediaInfo& out,

                            RtpMap& outMap) const;

    int StartGbClientLifecycle();

    void StopGbClientLifecycle();

    int RegisterGbClient(bool force);

    int QueryGbRegisterDate(std::string& outDate) const;

    int PrepareGbRegisterTimeSync(const std::string& rawDate);

    int SendGbHeartbeatOnce();

    void GbHeartbeatLoop();

    int HandleGbPtzControl(const DevControlCmd* cmd);

    int HandleGbGuardControl(const DevControlCmd* cmd);

    int HandleGbAlarmResetControl(const DevControlCmd* cmd);

    int HandleGbConfigControl(const DevControlCmd* cmd);

    int HandleGbHomePositionControl(const DevControlCmd* cmd);

    int HandleGbTeleBootControl(const DevControlCmd* cmd);

    int HandleGbDeviceUpgradeControl(const DevControlCmd* cmd);
    static void* GbUpgradeApplyThread(void* arg);

    int NotifyGbUpgradeResult(const char* gbCode,
                              const char* sessionId,
                              const char* firmware,
                              bool result,
                              const char* description);

    int PersistGbUpgradePendingState(const char* gbCode,
                                     const char* sessionId,
                                     const char* firmware,
                                     const char* description,
                                     bool pending);

    int ReportPendingGbUpgradeResult();

    int NotifyGbCatalogInternal(const char* gbCode);

    int ResponseGbQueryDeviceInfo(ResponseHandle handle, const QueryParam* param);

    int ResponseGbQueryDeviceStatus(ResponseHandle handle, const QueryParam* param);

    int ResponseGbQueryCatalog(ResponseHandle handle, const QueryParam* param);

    int ResponseGbQueryRecord(ResponseHandle handle, const QueryParam* param);

    int ResponseGbQueryConfig(ResponseHandle handle, const QueryParam* param);

    int ResponseGbQueryPreset(ResponseHandle handle, const QueryParam* param);

    void HandleGbReplayStorageFrame(unsigned char* data,
                                    int size,
                                    Mp4DemuxerFrameInfo_s* frameInfo,
                                    const GbReplayCallbackContext* context);

    int HandleGbLiveCaptureInternal(int media_chn,

                                       int media_type,

                                       int media_subtype,

                                       unsigned long long frame_pts,

                                       unsigned char* frame_data,

                                       int frame_len,

                                       int frame_end_flag);

    int StartGbLiveCapture();

    void StopGbLiveCapture();

    void ClearGbReplaySessionState();

    void ClearGbBroadcastSessionState();

    void ClearGbTalkSessionState();

    int RestartGbBroadcastBridge(const char* reason);

    int RestartGbTalkBridge(const char* reason);

    int NotifyGbMediaStatus(StreamHandle handle, const std::string& gbCode, int notifyType, const char* reason);



private:

    std::shared_ptr<IExternalConfigProvider> m_provider;

    ProtocolExternalConfig m_cfg;

    std::string m_gb_device_name;
    bool m_gb_osd_time_enabled;
    bool m_gb_osd_event_enabled;
    bool m_gb_osd_alert_enabled;

    GB28181BroadcastBridge m_broadcast;

    GB28181BroadcastBridge m_talk_broadcast;

    GB28181ListenBridge m_listen;

    GB28181RtpPsSender m_rtp_ps_sender;

    GB28181ClientReceiverAdapter m_gb_receiver;

    std::unique_ptr<GAT1400ClientService> m_gat_client;



    GB28181ClientSDK* m_gb_client_sdk;

    GbMediaPlayInfoResponder m_gb_media_responder;

    void* m_gb_media_responder_user;

    GbTimeSyncHook m_gb_time_sync_hook;

    void* m_gb_time_sync_user;

    std::mutex m_gb_lifecycle_mutex;

    std::thread m_gb_heartbeat_thread;

    std::atomic<bool> m_gb_heartbeat_running;
    std::atomic<bool> m_gb_upgrade_running;

    bool m_gb_client_started;

    bool m_gb_client_registered;
    std::atomic<uint64_t> m_gb_last_teleboot_ms;
    uint64_t m_gb_register_success_ms;
    uint32_t m_gb_register_expires_sec;

    std::mutex m_gb_subscribe_mutex;

    SubscribeHandle m_gb_catalog_subscribe_handle;

    SubscribeHandle m_gb_alarm_subscribe_handle;

    SubscribeHandle m_gb_mobile_position_subscribe_handle;

    std::mutex m_gb_replay_mutex;

    GbReplaySession m_gb_replay_session;

    uint64_t m_gb_replay_generation;

    std::vector<GbReplayCallbackContext*> m_gb_replay_callback_contexts;

    std::mutex m_gb_live_mutex;

    GbLiveSession m_gb_live_session;

    mutable std::mutex m_gb_broadcast_mutex;

    GbBroadcastSession m_gb_broadcast_session;

    mutable std::mutex m_gb_talk_mutex;

    GbTalkSession m_gb_talk_session;

    uint32_t m_gb_current_media_ssrc;
    int m_gb_current_media_port;

    bool m_gb_live_capture_started;



    bool m_started;

};



}



#endif































