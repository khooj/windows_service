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
#include <reproc++/sink.hpp>
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
    , max_count_(0)
    , interval_(0)
{
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
        if (current_count <= max_count_)
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
            max_count_ = interval_ / 5s;
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
        max_count_ = interval_ / 5s;
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

    if (max_count_ == 0)
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
    uint64_t count = 0;
    unsigned exit_status = 0;
    while (count < max_count_)
    {
        WRITE_EVENT_DEBUG("Waiting cycle");
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
        if (err || ret == 3)
        {
            if (err)
                WriteToEventLog(std::string{ "Error value: " + std::to_string(err.value()) }.c_str(), EVENTLOG_ERROR_TYPE);
            std::string sink_string;
            std::error_code ec = updater.drain(reproc::stream::out, reproc::string_sink(sink_string));
            if (!ec)
                WriteToEventLog(std::string{ "Program output: " + sink_string }.c_str(), EVENTLOG_WARNING_TYPE);
            else
                WriteToEventLog(std::string{ "Cannot print program output: " + std::to_string(ec.value()) }.c_str(), EVENTLOG_ERROR_TYPE);
        }
        break;
    }

    WRITE_EVENT_DEBUG(std::string{ "Error value: " + std::to_string(err.value()) }.c_str());
    return !bool(err);
}
