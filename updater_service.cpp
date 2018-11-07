#include "updater_service.h"
#include <functional>
#include <experimental/filesystem>
#include <winsvc.h>
#include <winnt.h>
#include <tchar.h>
#include <cstdlib>

#include <iostream>
#ifdef _DEBUG
#define DEBUG_LOG(x) do { std::cout << (x) << std::endl; } while(0)
#else
#define DEBUG_LOG(x) (void)0
#endif

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
        WriteToEventLog("Args not parsed!");
        std::exit(-1);
        return;
    }

    updater_arguments_ = updater_filepath_ + " " + updater_arguments_;
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
    
    while (!exit_)
    {
        std::this_thread::sleep_for(interval_);
        DWORD ret = -1;
        if (!LaunchApp(std::string(), ret))
        {
            std::string g{ "Error while launching updater: " + std::to_string(GetLastError()) };
            WriteToEventLog(g.c_str());
            break;
        }

        if (ret == 0)
        {
            WriteToEventLog("No updates");
            continue;
        }

        if (ret == 1)
        {
            // we have updates
            if (!LaunchApp(std::string("-u"), ret))
            {
                std::string g{ "Error while launching updater with -u: " + std::to_string(GetLastError()) };
                WriteToEventLog(g.c_str());
                break;
            }

            if (ret == 0)
            {
                WriteToEventLog("Update successful");
            }
        }
    }

    WriteToEventLog(_T("Exiting"));
}

void UpdaterService::ProcessArgs(int argc, char* argv[])
{
    //skipping executable name
    //parsing service name first
    for (int i = 1; i < argc; ++i)
    {
        std::string t{ argv[i] };
        if (t == "--name")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong name args");
                return;
            }

            t = argv[i + 1];
            SetName(_T(t.c_str()));
            std::string g{ "Proc name: " + t };
            WriteToEventLog(g.c_str());
            DEBUG_LOG("Proc name: " + t);
            break;
        }
    }

    for (int i = 1; i < argc; ++i)
    {
        std::string t{argv[i]};

        if (t == "--updater")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong updater args");
                return;
            }

            updater_filepath_ = argv[i + 1];
            std::string g{ "Updater filepath: " + updater_filepath_ };
            WriteToEventLog(g.c_str());
            DEBUG_LOG("Updater filepath: " + updater_filepath_);
        }

        if (t == "--args")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong updater arguments");
                return;
            }

            updater_arguments_ = argv[i + 1];
            std::string g{ "Updater args: " + updater_arguments_ };
            WriteToEventLog(g.c_str());
            DEBUG_LOG("Updater args: " + updater_arguments_);
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
            std::string g{ "Interval: " + t };
            WriteToEventLog(g.c_str());
            DEBUG_LOG("Interval: " + t);
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

bool UpdaterService::LaunchApp(const std::string& additional_args, DWORD& ret) const
{
    namespace fs = std::experimental::filesystem;
    fs::path updater_path(updater_filepath_);
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(si);
    si.lpDesktop = "winsta0\\default";
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::string args{ updater_arguments_ + " " + additional_args };
    if (CreateProcess(
        updater_path.generic_string().c_str(),
        const_cast<LPSTR>(&args.data()[0]),
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        updater_path.parent_path().generic_string().c_str(),
        &si,
        &pi
    ) == 0)
    {
        return false;
    }

    std::chrono::milliseconds m{ 10min };
    DWORD res = WaitForSingleObject(pi.hProcess, m.count());
    if (res == WAIT_TIMEOUT || res == WAIT_FAILED)
        return false;

    return GetExitCodeProcess(pi.hProcess, &ret) != 0;
}
