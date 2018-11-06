#include "updater_service.h"
#include <functional>
#include <chrono>

using namespace std::chrono_literals;

UpdaterService::UpdaterService()
    : ServiceBase(
        _T("UpdaterService"),
        _T("Updater service"),
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        SERVICE_ACCEPT_STOP)
{
}

void UpdaterService::OnStart(DWORD argc, TCHAR * argv[])
{
    mut_.unlock();
    thread_ = std::make_unique<std::thread>(std::bind(&UpdaterService::Work, this));
    thread_->detach();
}

void UpdaterService::OnStop()
{
    mut_.lock();
    if (thread_->joinable())
        thread_->join();
}

void UpdaterService::Work()
{
    while (true)
    {
        std::this_thread::sleep_for(5s);
        if (mut_.try_lock())
            return;
    }
}
