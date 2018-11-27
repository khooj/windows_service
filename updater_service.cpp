#include "updater_service.h"
#include <functional>
#include <experimental/filesystem>
#include <winsvc.h>
#include <winnt.h>
#include <tchar.h>
#include <cstdlib>
#include <fstream>
#include "json.hpp"
#include <reproc++/reproc.hpp>
#include <string>
#include <sstream>
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

std::string executable_filepath()
{
    char p[1024];
    DWORD real_size = GetModuleFileName(NULL, p, 1024);
    return std::string{ p, real_size };
}

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
}

UpdaterService::HandleOwner::HandleOwner() : h_(NULL)
{
}

UpdaterService::HandleOwner::HandleOwner(HANDLE handle) : h_(handle)
{
}


UpdaterService::HandleOwner::~HandleOwner()
{
    if (h_ != NULL)
        CloseHandle(h_);
}

UpdaterService::HandleOwner& UpdaterService::HandleOwner::operator=(HANDLE handle)
{
    if (h_ != NULL)
        CloseHandle(h_);
    h_ = handle;
    return *this;
}

UpdaterService::HandleOwner::operator HANDLE() const
{
    return h_;
}

UpdaterService::HandleOwner::operator PHANDLE()
{
    return &h_;
}

void UpdaterService::OnStart(DWORD argc, TCHAR* argv[])
{
    if (argc > 1)
        WriteToEventLog("Executable arguments not supported! Use config instead", EVENTLOG_WARNING_TYPE);

    ProcessConfig();

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

        if (ret == 3)
        {
            std::string g{ "Updater returned error" };
            WriteToEventLog(g.c_str(), EVENTLOG_ERROR_TYPE);
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

void UpdaterService::ProcessConfig()
{
    using nlohmann::json;
    namespace fs = std::experimental::filesystem;
    std::string exec = executable_filepath();
    if (exec.empty())
    {
        WriteToEventLog(std::string{ "Cannot get executable path: " + std::to_string(GetLastError()) }.c_str(), EVENTLOG_ERROR_TYPE);
        std::exit(-1);
    }

    fs::path config_path{ exec };
    config_path = config_path.parent_path() / "config_updater.json";

    if (!exists(config_path))
    {
        WriteToEventLog(std::string{ "config_updater.json file dont exist: " + config_path.string() }.c_str(), EVENTLOG_ERROR_TYPE);
        std::exit(-1);
    }

    json options;
    std::fstream file(config_path.string(), std::ios::in);
    if (!file.is_open())
    {
        WriteToEventLog(std::string{ "Cannot open config file" + config_path.string() }.c_str(), EVENTLOG_ERROR_TYPE);
        std::exit(-1);
    }

    try
    {
        file >> options;
        std::string name = options["name"].get<std::string>();
        SetName(_T(name.c_str()));
        updater_filepath_ = options["updater"].get<std::string>();
        updater_arguments_ = options["args"].get<std::string>();
        auto temp_interval = options["interval"].get<unsigned long>();
        interval_ = std::chrono::seconds{ temp_interval };
        if (interval_ < 5s)
            interval_ = 5s;
        count_ = interval_ / 5s;
        if (options.count("user") != 0)
            user_runas_ = options["user"].get<std::string>();
        if (options.count("pass") != 0)
            user_pass_ = options["pass"].get<std::string>();
    }
    catch (json::exception &e)
    {
        WriteToEventLog(std::string{ "Caught exception: " + std::string{ e.what() } }.c_str(), EVENTLOG_ERROR_TYPE);
        SetStatus(SERVICE_STOPPED);
        std::exit(-1);
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
    reproc::process updater;
    std::string args{ updater_arguments_ + " " + additional_args };
    std::vector<std::string> a;
    std::stringstream str;
    str << args;
    std::string tmp;
    while (str >> tmp)
        a.push_back(tmp);
    std::error_code err = updater.start(a);

    std::chrono::milliseconds time_chunk{ 5s };
    const uint64_t max_count = 5min / time_chunk;
    uint64_t count = 0;
    unsigned exit_status = 0;
    while (count < max_count)
    {
        ++count;
        err = updater.wait(time_chunk.count(), &exit_status);
        if (exit_)
        {
            err = updater.terminate(time_chunk.count(), &exit_status);
            if (err)
                updater.kill(0, &exit_status);
            return false;
        }

        if (err == reproc::errc::wait_timeout)
            continue;

        ret = exit_status;
        if (err)
            WriteToEventLog(std::string{ "Error value: " + std::to_string(err.value()) }.c_str(), EVENTLOG_ERROR_TYPE);
        break;
    }

    WRITE_EVENT_DEBUG(std::string{ "Error value: " + std::to_string(err.value()) }.c_str());
    return !bool(err);

    
    std::wstring args_w = s2ws(args);

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    child_out_wr = NULL;
    child_out_rd = NULL;
    
    if (!CreatePipe(child_out_rd, child_out_wr, &saAttr, 0))
        return false;
    if (!SetHandleInformation(child_out_rd, HANDLE_FLAG_INHERIT, 0))
        return false;

    const auto readOutput = [&] {
        DWORD dwRead;
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
        res = LaunchAppWithoutLogon(args, ret);
    else
        res = LaunchAppWithLogon(args_w, ret);

    if (!res)
    {
        child_out_wr = NULL;
        readOutput();
    }

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

    HandleOwner proc;
    HandleOwner thread;

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

    proc = pi.hProcess;
    thread = pi.hThread;

    bool exit = WaitForProcess(proc, 5min);
    if (exit)
        exit = GetExitCodeProcess(proc, &ret) != 0;

    DEBUG_LOG("Exit LaunchApp");
    return exit;
}

bool UpdaterService::LaunchAppWithoutLogon(std::string& args, DWORD& ret) const
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

    HandleOwner proc;
    HandleOwner thread;

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

    proc = pi.hProcess;
    thread = pi.hThread;

    bool exit = WaitForProcess(proc, 5min);
    if (exit)
        exit = GetExitCodeProcess(proc, &ret) != 0;
    DEBUG_LOG("Exit LaunchApp");
    return exit;
}

bool UpdaterService::WaitForProcess(const HandleOwner& process, std::chrono::milliseconds msecs) const
{
    std::chrono::milliseconds timeout_chunk{ 5s };
    const uint64_t max_count = (msecs >= 5s ? msecs : 5s) / timeout_chunk;
    uint64_t count{ 0 };
    while (count < max_count)
    {
        ++count;
        DWORD res = WaitForSingleObject(process, static_cast<DWORD>(timeout_chunk.count()));
        if (res == WAIT_TIMEOUT)
        {
            if (!exit_)
            {
                WRITE_EVENT_DEBUG("Waiting timed out, new cycle");
                continue;
            }

            WRITE_EVENT_DEBUG("Waiting timeout out, terminating process");
            TerminateProcess(process, 3);
            return true;
        }
        if (res == WAIT_FAILED)
        {
            if (GetLastError() == 109)
            {
                // app wrote nothing to stdout and closed pipe
                // probably it exited successfully
                WRITE_EVENT_DEBUG("Broken pipe in waiting for app. Exiting with true");
                return true;
            }

            DEBUG_LOG("Wait for updater failed");
            WriteToEventLog(std::string{ "Waiting for process failed: " + std::to_string(GetLastError()) }.c_str(), EVENTLOG_WARNING_TYPE);
            return false;
        }
        if (res == WAIT_OBJECT_0)
        {
            WRITE_EVENT_DEBUG("Waited successfully");
            return true;
        }
    }

    if (count == max_count) // timed out
    {
        DEBUG_LOG("Wait for updater timed out");
        WriteToEventLog("Waiting for process timeout out", EVENTLOG_WARNING_TYPE);
        return false;
    }

    return true;
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
