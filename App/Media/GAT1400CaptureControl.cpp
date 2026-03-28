#include "GAT1400CaptureControl.h"

#include <algorithm>
#include <stdio.h>

namespace
{

static const size_t kMaxPendingCaptureEventCount = 128;

static bool HasAnyPayload(const media::GAT1400CaptureEvent& event)
{
    return event.object_type != media::GAT1400_CAPTURE_OBJECT_NONE ||
           !event.image_list.empty() ||
           !event.video_slice_list.empty() ||
           !event.file_list.empty();
}

static const char* ResolveCaptureObjectName(media::GAT1400CaptureObjectType type)
{
    switch (type) {
        case media::GAT1400_CAPTURE_OBJECT_FACE:
            return "face";
        case media::GAT1400_CAPTURE_OBJECT_MOTOR_VEHICLE:
            return "motor_vehicle";
        default:
            return "unknown";
    }
}

} // namespace

namespace media
{

GAT1400CaptureControl::GAT1400CaptureControl()
{
}

GAT1400CaptureControl& GAT1400CaptureControl::Instance()
{
    static GAT1400CaptureControl instance;
    return instance;
}

int GAT1400CaptureControl::Submit(const GAT1400CaptureEvent& event)
{
    if (!HasAnyPayload(event)) {
        return -1;
    }

    std::vector<GAT1400CaptureObserver*> observers;
    size_t pendingCount = 0;
    bool droppedOldest = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pending_events.size() >= kMaxPendingCaptureEventCount) {
            m_pending_events.pop_front();
            droppedOldest = true;
        }
        m_pending_events.push_back(event);
        pendingCount = m_pending_events.size();
        observers = m_observers;
    }

    printf("[GAT1400CaptureControl] queue event ret=0 object=%s trace=%s images=%zu videos=%zu files=%zu pending=%zu dropped=%d\n",
           ResolveCaptureObjectName(event.object_type),
           event.trace_id.empty() ? "-" : event.trace_id.c_str(),
           event.image_list.size(),
           event.video_slice_list.size(),
           event.file_list.size(),
           pendingCount,
           droppedOldest ? 1 : 0);

    for (size_t i = 0; i < observers.size(); ++i) {
        if (observers[i] != NULL) {
            observers[i]->OnGAT1400CaptureQueued();
        }
    }

    return 0;
}

int GAT1400CaptureControl::SubmitFaceCapture(const GAT_1400_Face& face,
                                             const std::list<GAT_1400_ImageSet>& imageList,
                                             const std::list<GAT_1400_VideoSliceSet>& videoSliceList,
                                             const std::list<GAT_1400_FileSet>& fileList,
                                             const std::string& traceId)
{
    GAT1400CaptureEvent event;
    event.trace_id = traceId;
    event.object_type = GAT1400_CAPTURE_OBJECT_FACE;
    event.face = face;
    event.image_list = imageList;
    event.video_slice_list = videoSliceList;
    event.file_list = fileList;
    return Submit(event);
}

int GAT1400CaptureControl::SubmitMotorCapture(const GAT_1400_Motor& motorVehicle,
                                              const std::list<GAT_1400_ImageSet>& imageList,
                                              const std::list<GAT_1400_VideoSliceSet>& videoSliceList,
                                              const std::list<GAT_1400_FileSet>& fileList,
                                              const std::string& traceId)
{
    GAT1400CaptureEvent event;
    event.trace_id = traceId;
    event.object_type = GAT1400_CAPTURE_OBJECT_MOTOR_VEHICLE;
    event.motor_vehicle = motorVehicle;
    event.image_list = imageList;
    event.video_slice_list = videoSliceList;
    event.file_list = fileList;
    return Submit(event);
}

bool GAT1400CaptureControl::PopPending(GAT1400CaptureEvent* event)
{
    if (event == NULL) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending_events.empty()) {
        return false;
    }

    *event = m_pending_events.front();
    m_pending_events.pop_front();
    return true;
}

size_t GAT1400CaptureControl::PendingCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending_events.size();
}

void GAT1400CaptureControl::AddObserver(GAT1400CaptureObserver* observer)
{
    if (observer == NULL) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (std::find(m_observers.begin(), m_observers.end(), observer) == m_observers.end()) {
        m_observers.push_back(observer);
    }
}

void GAT1400CaptureControl::RemoveObserver(GAT1400CaptureObserver* observer)
{
    if (observer == NULL) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_observers.erase(std::remove(m_observers.begin(), m_observers.end(), observer), m_observers.end());
}

} // namespace media
