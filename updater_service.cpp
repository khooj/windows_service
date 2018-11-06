#include "updater_service.h"
#include <functional>
#include <chrono>
#include <experimental/filesystem>
#include <winsvc.h>
#include <winnt.h>
#include <tchar.h>

using namespace std::chrono_literals;

UpdaterService::UpdaterService(int argc, char *argv[])
    : ServiceBase(
        _T("UpdaterService"),
        _T("Updater service"),
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        SERVICE_ACCEPT_STOP)
{
}

void UpdaterService::OnStart(DWORD argc, TCHAR* argv[])
{
    WriteToEventLog(_T("Starting"));
    for (DWORD i = 0; i < argc; ++i)
        WriteToEventLog(argv[i]);

    exit_ = false;
    WriteToEventLog(_T("Started!"));
    thread_ = std::make_unique<std::thread>(std::bind(&UpdaterService::Work, this));
}

void UpdaterService::OnStop()
{
    exit_ = true;
    WriteToEventLog(_T("Stopped!"));
    if (thread_->joinable())
        thread_->join();
}

void UpdaterService::Work()
{
    namespace fs = std::experimental::filesystem;
    while (true)
    {
        WriteToEventLog(_T("Cycle"));
        std::this_thread::sleep_for(5s);
        if (exit_)
        {
            WriteToEventLog(_T("Exiting"));
            return;
        }
    }
}
