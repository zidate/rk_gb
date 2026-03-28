#ifndef _GAT1400_CAPTURE_CONTROL_H_
#define _GAT1400_CAPTURE_CONTROL_H_

#include <cstddef>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <vector>

#define ConnectParam GAT1400ConnectParam
#include "CMS1400Struct.h"
#undef ConnectParam

namespace media
{

enum GAT1400CaptureObjectType
{
    GAT1400_CAPTURE_OBJECT_NONE = 0,
    GAT1400_CAPTURE_OBJECT_FACE = 1,
    GAT1400_CAPTURE_OBJECT_MOTOR_VEHICLE = 2
};

struct GAT1400CaptureEvent
{
    std::string trace_id;
    GAT1400CaptureObjectType object_type;
    GAT_1400_Face face;
    GAT_1400_Motor motor_vehicle;
    std::list<GAT_1400_ImageSet> image_list;
    std::list<GAT_1400_VideoSliceSet> video_slice_list;
    std::list<GAT_1400_FileSet> file_list;

    GAT1400CaptureEvent()
        : object_type(GAT1400_CAPTURE_OBJECT_NONE)
    {
    }
};

class GAT1400CaptureObserver
{
public:
    virtual ~GAT1400CaptureObserver() {}
    virtual void OnGAT1400CaptureQueued() = 0;
};

class GAT1400CaptureControl
{
public:
    static GAT1400CaptureControl& Instance();

    // Usage:
    // 1. 编码/算法侧产出抓拍结果后，组好 Face/Motor 与关联 ImageSet/VideoSliceSet/FileSet。
    // 2. 人脸走 SubmitFaceCapture()，机动车走 SubmitMotorCapture()，更复杂场景可直接 Submit()。
    // 3. 当前只负责进程内排队与通知，不保证断电恢复；真正对平台上传由 GAT1400ClientService 消费完成。
    int Submit(const GAT1400CaptureEvent& event);
    int SubmitFaceCapture(const GAT_1400_Face& face,
                          const std::list<GAT_1400_ImageSet>& imageList,
                          const std::list<GAT_1400_VideoSliceSet>& videoSliceList,
                          const std::list<GAT_1400_FileSet>& fileList,
                          const std::string& traceId = "");
    int SubmitMotorCapture(const GAT_1400_Motor& motorVehicle,
                           const std::list<GAT_1400_ImageSet>& imageList,
                           const std::list<GAT_1400_VideoSliceSet>& videoSliceList,
                           const std::list<GAT_1400_FileSet>& fileList,
                           const std::string& traceId = "");

    bool PopPending(GAT1400CaptureEvent* event);
    size_t PendingCount() const;

    void AddObserver(GAT1400CaptureObserver* observer);
    void RemoveObserver(GAT1400CaptureObserver* observer);

private:
    GAT1400CaptureControl();
    GAT1400CaptureControl(const GAT1400CaptureControl&);
    GAT1400CaptureControl& operator=(const GAT1400CaptureControl&);

private:
    mutable std::mutex m_mutex;
    std::deque<GAT1400CaptureEvent> m_pending_events;
    std::vector<GAT1400CaptureObserver*> m_observers;
};

} // namespace media

#endif
