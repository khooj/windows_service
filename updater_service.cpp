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
#define WRITE_EVENT_DEBUG(x) do { WriteToEventLog((x), EVENTLOG_WARNING_TYPE); } while(0)
#else
#define DEBUG_LOG(x) (void)0
#define WRITE_EVENT_DEBUG(x) (void)0
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
    , exit_(false)
    , count_(0)
    , interval_(0)
{
    ProcessArgs(argc, argv);
}

void UpdaterService::OnStart(DWORD argc, TCHAR* argv[])
{
    ProcessArgs(static_cast<int>(argc), static_cast<char**>(argv));

    if (!CheckArgs())
    {
        std::exit(-1);
    }

    updater_arguments_ = updater_filepath_ + " " + updater_arguments_;
    exit_ = false;
    WriteToEventLog("Started");
    thread_ = std::make_unique<std::thread>(std::bind(&UpdaterService::Work, this));
}

void UpdaterService::OnStop()
{
    exit_ = true;
    WriteToEventLog("Stopped");
    if (thread_->joinable())
        thread_->join();
}

void UpdaterService::Work()
{
    uint32_t current_count{ 0 };
    while (!exit_)
    {
        WRITE_EVENT_DEBUG("New cycle");
        std::this_thread::sleep_for(5s);
        ++current_count;
        if (current_count <= count_)
            continue;
        current_count = 0;
        DWORD ret = -1;
        if (!LaunchApp(std::string(), ret))
        {
            std::string g{ "Error while launching updater: " + std::to_string(GetLastError()) };
            WriteToEventLog(g.c_str(), EVENTLOG_ERROR_TYPE);
            break;
        }

        if (ret == 0)
        {
            WRITE_EVENT_DEBUG("No updates");
            continue;
        }

        if (ret == 1)
        {
            // we have updates
            if (!LaunchApp(std::string("-u"), ret))
            {
                std::string g{ "Error while launching updater with -u: " + std::to_string(GetLastError()) };
                WriteToEventLog(g.c_str(), EVENTLOG_ERROR_TYPE);
                break;
            }

            if (ret == 0)
            {
                WRITE_EVENT_DEBUG("Update successful");
            }
        }
    }

    WRITE_EVENT_DEBUG("Exiting");
    SetStatus(SERVICE_STOPPED);
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
                WriteToEventLog("Wrong name args", EVENTLOG_ERROR_TYPE);
                return;
            }

            t = argv[i + 1];
            SetName(_T(t.c_str()));
            std::string g{ "Proc name: " + t };
            WRITE_EVENT_DEBUG(g.c_str());
            DEBUG_LOG(g);
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
                WriteToEventLog("Wrong updater args", EVENTLOG_ERROR_TYPE);
                return;
            }

            updater_filepath_ = argv[i + 1];
            std::string g{ "Updater filepath: " + updater_filepath_ };
            WRITE_EVENT_DEBUG(g.c_str());
            DEBUG_LOG(g);
        }

        if (t == "--args")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong updater arguments", EVENTLOG_ERROR_TYPE);
                return;
            }

            updater_arguments_ = argv[i + 1];
            std::string g{ "Updater args: " + updater_arguments_ };
            WRITE_EVENT_DEBUG(g.c_str());
            DEBUG_LOG(g);
        }

        if (t == "--interval")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong interval args", EVENTLOG_ERROR_TYPE);
                return;
            }

            t = argv[i + 1];
            unsigned long tmp = std::strtoul(t.c_str(), nullptr, 10);
            interval_ = std::chrono::seconds{ tmp };
            if (interval_ < 5s)
                interval_ = 5s;
            count_ = interval_ / 5s;
            std::string g{ "Interval: " + std::to_string(interval_.count()) };
            WRITE_EVENT_DEBUG(g.c_str());
            DEBUG_LOG(g);
        }

        if (t == "--user")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong user args", EVENTLOG_ERROR_TYPE);
                return;
            }

            user_runas_ = argv[i + 1];
            std::string g{ "User: " + user_runas_ };
            WRITE_EVENT_DEBUG(g.c_str());
            DEBUG_LOG(g);
        }

        if (t == "--pass")
        {
            if (i + 1 > argc)
            {
                WriteToEventLog("Wrong user pass", EVENTLOG_ERROR_TYPE);
                return;
            }

            user_pass_ = argv[i + 1];
            std::string g{ "User pass: " + user_pass_ };
            WRITE_EVENT_DEBUG(g.c_str());
            DEBUG_LOG(g);
        }
    }
}

bool UpdaterService::CheckArgs() const
{
    namespace fs = std::experimental::filesystem;
    const fs::path p(updater_filepath_);
    if (!fs::exists(p))
    {
        WriteToEventLog("Executable path not exists", EVENTLOG_ERROR_TYPE);
        return false;
    }

    if (count_ == 0)
    {
        WriteToEventLog("Interval is invalid", EVENTLOG_ERROR_TYPE);
        return false;
    }

    return true;
}

bool UpdaterService::LaunchApp(const std::string& additional_args, DWORD& ret)
{
    std::string args{ updater_arguments_ + " " + additional_args };
    std::wstring args_w = s2ws(args);

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    child_out_rd = NULL;
    child_out_wr = NULL;
    
    if (!CreatePipe(&child_out_rd, &child_out_wr, &saAttr, 0))
        return false;
    if (!SetHandleInformation(child_out_rd, HANDLE_FLAG_INHERIT, 0))
        return false;

    const auto readOutput = [&] {
        DWORD dwRead, dwWritten;
        CHAR buf[4096];
        BOOL success = FALSE;
        DEBUG_LOG("Reading app stdout");
        for (;;)
        {
            success = ReadFile(child_out_rd, buf, 4096, &dwRead, NULL);
            if (!success || dwRead == 0)
                break;
            std::string g{ "Process stdout: " + std::string(buf, dwRead) };
            WriteToEventLog(g.c_str());
        }
    };

    bool res;
    if (user_runas_.empty())
        res = LaunchAppWithoutLogin(args, ret);
    else
        res = LaunchAppWithLogon(args_w, ret);

    if (!res)
        readOutput();

    CloseHandle(child_out_rd);
    CloseHandle(child_out_wr);
    return res;
}

bool UpdaterService::LaunchAppWithLogon(std::wstring& args, DWORD &ret) const
{
    namespace fs = std::experimental::filesystem;
    fs::path updater_path(updater_filepath_);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(STARTUPINFOW));
    si.cb = sizeof(STARTUPINFOW);
    si.hStdError = child_out_wr;
    si.hStdOutput = child_out_wr;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessWithLogonW(
        s2ws(user_runas_).c_str(),
        NULL,
        s2ws(user_pass_).c_str(),
        0,
        updater_path.generic_wstring().c_str(),
        const_cast<LPWSTR>(&args.data()[0]),
        0,
        NULL,
        updater_path.parent_path().generic_wstring().c_str(),
        &si,
        &pi
    ) == 0)
    {
        if (GetLastError() != 0)
        {
            DEBUG_LOG("Error launching app");
            WriteToEventLog("Error launching app", EVENTLOG_ERROR_TYPE);
            return false;
        }
    }

    std::chrono::milliseconds m{ 5min };
    DWORD res = WaitForSingleObject(pi.hProcess, m.count());
    if (res == WAIT_TIMEOUT || res == WAIT_FAILED)
    {
        DEBUG_LOG("Wait for updater timeout or failed");
        WriteToEventLog("Waiting for process failed or timed out", EVENTLOG_WARNING_TYPE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }

    bool exit = GetExitCodeProcess(pi.hProcess, &ret) != 0;

    DEBUG_LOG("Exit LaunchApp");
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exit;
}

bool UpdaterService::LaunchAppWithoutLogin(std::string& args, DWORD& ret) const
{
    namespace fs = std::experimental::filesystem;
    fs::path updater_path(updater_filepath_);

    STARTUPINFO si;
    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.hStdError = child_out_wr;
    si.hStdOutput = child_out_wr;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcess(
        updater_path.generic_string().c_str(),
        const_cast<LPSTR>(args.c_str()),
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
        if (GetLastError() != 0)
        {
            DEBUG_LOG("Error launching app");
            WriteToEventLog("Error launching app", EVENTLOG_ERROR_TYPE);
            return false;
        }
    }

    std::chrono::milliseconds m{ 5min };
    DWORD res = WaitForSingleObject(pi.hProcess, m.count());
    if (res == WAIT_TIMEOUT || res == WAIT_FAILED)
    {
        DEBUG_LOG("Wait for updater timeout or failed");
        WriteToEventLog("Waiting for process failed or timed out", EVENTLOG_WARNING_TYPE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return false;
    }

    bool exit = GetExitCodeProcess(pi.hProcess, &ret) != 0;

    DEBUG_LOG("Exit LaunchApp");
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exit;
}

std::wstring UpdaterService::s2ws(const std::string& s) const
{
    int len;
    int slength = (int)s.length() + 1;
    len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;
    return r;
}
