#ifndef __DM_CLIENT_SERVICE_H__
#define __DM_CLIENT_SERVICE_H__

#include <pthread.h>

#include <atomic>
#include <mutex>

namespace dm
{

class DmClientService
{
public:
    static DmClientService& Instance();

    int Start();
    void Stop();
    int Restart();
    bool IsRunning() const;

private:
    DmClientService();
    ~DmClientService();
    DmClientService(const DmClientService&);
    DmClientService& operator=(const DmClientService&);

    static void* ThreadEntry(void* arg);
    void ThreadLoop();
    int RunLwm2mClientOnce();

private:
    mutable std::mutex m_mutex;
    pthread_t m_thread;
    bool m_thread_started;
    std::atomic<bool> m_stop_requested;
    std::atomic<bool> m_running;
};

int RestartDmClientService();

}

#endif
