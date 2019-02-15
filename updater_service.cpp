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
#include <curl/curl.h>
#include <thread>
#include <windows.h>

#define EVENTLOG_MY_DEBUG 0x1000

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

std::string machine_name()
{
    char p[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof p;
    BOOL result = GetComputerName(p, &size);
    if (result)
        return { p, size };
    return {};
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
        WriteToEventLog("Executable arguments not supported! Use config instead", EVENTLOG_INFORMATION_TYPE);

    ProcessConfig();

    if (!CheckArgs())
    {
        std::exit(-1);
    }

    updater_arguments_ = updater_filepath_ + " " + updater_arguments_;
    exit_ = false;
    WriteToEventLog("Started", EVENTLOG_INFORMATION_TYPE);
    thread_ = std::make_unique<std::thread>(std::bind(&UpdaterService::Work, this));
}

void UpdaterService::OnStop()
{
    exit_ = true;
    WriteToEventLog("Stopped", EVENTLOG_INFORMATION_TYPE);
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
            Log(g, EVENTLOG_ERROR_TYPE);
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
            Log(g, EVENTLOG_ERROR_TYPE);
            continue;
        }

        if (ret == 1)
        {
            // we have updates
            if (!LaunchApp(std::string("-u"), ret))
            {
                std::string g{ "Error while launching updater with -u: " + std::to_string(GetLastError()) };
                Log(g, EVENTLOG_ERROR_TYPE);
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
                Log("Wrong name args", EVENTLOG_ERROR_TYPE);
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
                Log("Wrong updater args", EVENTLOG_ERROR_TYPE);
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
                Log("Wrong updater arguments", EVENTLOG_ERROR_TYPE);
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
                Log("Wrong interval args", EVENTLOG_ERROR_TYPE);
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
                Log("Wrong user args", EVENTLOG_ERROR_TYPE);
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
                Log("Wrong user pass", EVENTLOG_ERROR_TYPE);
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
        Log(std::string{ "Cannot get executable path: " + std::to_string(GetLastError()) }, EVENTLOG_ERROR_TYPE);
        std::exit(-1);
    }

    fs::path config_path{ exec };
    config_path = config_path.parent_path() / "config_updater.json";

    if (!exists(config_path))
    {
        Log(std::string{ "config_updater.json file dont exist, creating default: " + config_path.string() }, EVENTLOG_WARNING_TYPE);
        CreateDefaultConfig("config_updater.json");
    }

    json options;
    std::fstream file(config_path.string(), std::ios::in);
    if (!file.is_open())
    {
        Log(std::string{ "Cannot open config file" + config_path.string() }, EVENTLOG_ERROR_TYPE);
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
        if (options.count("log_server") != 0)
            logger_server_ = options["log_server"].get<std::string>();
    }
    catch (json::exception &e)
    {
        Log(std::string{ "Caught exception: " + std::string{ e.what() } }, EVENTLOG_ERROR_TYPE);
        SetStatus(SERVICE_STOPPED);
        std::exit(-1);
    }
}

void UpdaterService::CreateDefaultConfig(const std::string& filename)
{
    namespace fs = std::experimental::filesystem;
    using nlohmann::json;

    fs::path config{ executable_filepath() };
    config = config.parent_path() / filename;
    json options;
    options["name"] = "AgentUpdater";
    options["updater"] = "C:\\miner\\NAppUpdate.Updater.Standalone.exe";
    options["args"] = "-f ftp://10.7.5.32/distro/miner/feed.xml -c read-ftp:Aa123456";
    options["interval"] = 300;
    options["log_server"] = "http://gilmutdinov.ru:9001/api/events/raw";

    std::fstream file{ config.c_str(), std::ios::out };
    try
    {
        file << options;
    } catch (std::exception &e)
    {
        Log(std::string{ "Caught exception: " + std::string{ e.what() } }, EVENTLOG_ERROR_TYPE);
        SetStatus(SERVICE_STOPPED);
        std::exit(-1);
    }
}

void UpdaterService::Log(const std::string& message, WORD level, bool wait) const
{
    if (level == EVENTLOG_MY_DEBUG)
        WRITE_EVENT_DEBUG(message.c_str());
    else
        WriteToEventLog(message, level);

    WRITE_EVENT_DEBUG("log server addr");
    WRITE_EVENT_DEBUG(logger_server_);
    //trying to post log to seq
    if (!logger_server_.empty())
    {
        using nlohmann::json;

        std::string seqLevel;
        switch (level)
        {
        case EVENTLOG_ERROR_TYPE: seqLevel = "Error"; break;
        case EVENTLOG_WARNING_TYPE: seqLevel = "Warning"; break;
        case EVENTLOG_INFORMATION_TYPE: seqLevel = "Information"; break;
        case EVENTLOG_MY_DEBUG: seqLevel = "Debug"; break;
        default:
            return;
        }

        std::time_t timestamp = std::time(nullptr);
        char buf[64] = { 0 };
        strftime(buf, 32, "%Y-%m-%dT%T.00000%z", localtime(&timestamp));
        json body;
        body["Level"] = seqLevel;
        body["Timestamp"] = buf;
        body["MessageTemplate"] = "(windows_updater: {machine_name}) {msg}";
        json prop;
        prop["machine_name"] = machine_name();
        prop["msg"] = message;
        body["Properties"] = prop;
        json wrapper;
        wrapper["Events"] = json::array({ body });

        WRITE_EVENT_DEBUG("wrapper json");
        WRITE_EVENT_DEBUG(wrapper.dump().c_str());

        // thougth about async call but wont do it now
        const auto log_send = [&](std::string json, std::string srv)
        {
            CURL *curl;
            CURLcode res;
            curl_global_init(CURL_GLOBAL_ALL);
            curl = curl_easy_init();
            if (curl)
            {
                WRITE_EVENT_DEBUG("curl initialized");
                curl_easy_setopt(curl, CURLOPT_URL, srv.c_str());
                curl_easy_setopt(curl, CURLOPT_POST, 1);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json.size());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());

                struct curl_slist *hs = nullptr;
                hs = curl_slist_append(hs, "Content-Type: application/json");
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hs);
                res = curl_easy_perform(curl);
                if (res != CURLE_OK)
                {
                    std::string err = curl_easy_strerror(res);
                    WriteToEventLog("Log send error: "s + err, EVENTLOG_ERROR_TYPE);
                }
                curl_easy_cleanup(curl);
            }
            curl_global_cleanup();
        };

        std::thread perform_log_send{ log_send, wrapper.dump(-1, ' ', true), logger_server_ };
        if (wait)
            perform_log_send.join();
        else
            perform_log_send.detach();
    }
}

bool UpdaterService::CheckArgs() const
{
    namespace fs = std::experimental::filesystem;
    const fs::path p(updater_filepath_);
    if (!fs::exists(p))
    {
        Log("Executable path not exists", EVENTLOG_ERROR_TYPE);
        return false;
    }

    if (max_count_ == 0)
    {
        Log("Interval is invalid", EVENTLOG_ERROR_TYPE);
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
        err = updater.wait(time_chunk, &exit_status);
        if (exit_)
        {
            err = updater.terminate();
            if (err)
                updater.kill();
            return false;
        }

        if (err == reproc::errc::wait_timeout)
            continue;

        ret = exit_status;
        if (err || ret == 3)
        {
            if (err)
                Log(std::string{ "Error value: " + std::to_string(err.value()) }, EVENTLOG_ERROR_TYPE);
            std::string sink_string;
            std::error_code ec = updater.drain(reproc::stream::out, reproc::string_sink(sink_string));
            if (!ec)
                Log(std::string{ "Program output: " + sink_string }, EVENTLOG_ERROR_TYPE);
            else
                Log(std::string{ "Cannot print program output: " + std::to_string(ec.value()) }, EVENTLOG_ERROR_TYPE);
        }
        break;
    }

    WRITE_EVENT_DEBUG(std::string{ "Error value: " + std::to_string(err.value()) }.c_str());
    return !bool(err);
}
