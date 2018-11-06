#include "updater_service.h"
#include <functional>
#include <experimental/filesystem>
#include <winsvc.h>
#include <winnt.h>
#include <tchar.h>
#include <cstdlib>

using namespace std::string_literals;
using namespace std::chrono_literals;

UpdaterService::UpdaterService(int argc, char *argv[])
    : ServiceBase(
        _T("UpdaterService"),
        _T("Updater service"),
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        SERVICE_ACCEPT_STOP)
{
    parsed_ = false;
    ProcessArgs(argc, argv);
}

void UpdaterService::OnStart(DWORD argc, TCHAR* argv[])
{
    ProcessArgs(static_cast<int>(argc), static_cast<char**>(argv));

    if (!parsed_)
    {
        std::exit(-1);
        return;
    }

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
        std::this_thread::sleep_for(interval_);
        if (exit_)
        {
            WriteToEventLog(_T("Exiting"));
            return;
        }
    }
}

void UpdaterService::ProcessArgs(int argc, char* argv[])
{
    //skipping executable name
    for (int i = 1; i < argc; ++i)
    {
        std::string t(argv[i]);

        if (t == "--updater")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong updater args");
                return;
            }

            updater_filepath_ = argv[i + 1];
        }

        if (t == "--name")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong name args");
                return;
            }

            t = argv[i + 1];
            SetName(_T(t.c_str()));
        }

        if (t == "--interval")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong interval args");
                return;
            }

            t = argv[i + 1];
            unsigned long tmp = std::strtoul(t.c_str(), nullptr, 10);
            interval_ = std::chrono::seconds{ tmp };
        }
    }

    parsed_ = CheckArgs();
}

bool UpdaterService::CheckArgs() const
{
    namespace fs = std::experimental::filesystem;
    const fs::path p(updater_filepath_);
    if (!fs::exists(p))
        return false;
    if (interval_.count() == 0ULL)
        return false;

    return true;
}
