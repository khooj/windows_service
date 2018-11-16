#ifndef UPDATER_SERVICE_H
#define UPDATER_SERVICE_H

#include "service_base.h"
#include <thread>
#include <memory>
#include <string>
#include <chrono>

class UpdaterService : public ServiceBase
{
public:
    UpdaterService(int argc, char *argv[]);

    UpdaterService(const UpdaterService&) = delete;
    UpdaterService& operator=(const UpdaterService&) = delete;

    UpdaterService(UpdaterService&&) = delete;
    UpdaterService& operator=(UpdaterService&&) = delete;
    virtual ~UpdaterService() = default;
    
private:
    void Work();
    void OnStart(DWORD argc, TCHAR* argv[]) override;
    void OnStop() override;
    
    void ProcessArgs(int argc, char *argv[]);
    bool CheckArgs() const;
    bool LaunchApp(const std::string& additional_args, DWORD &ret);
    bool LaunchAppWithLogon(std::wstring& args, DWORD& ret) const;
    bool LaunchAppWithoutLogin(std::string& args, DWORD& ret) const;

    std::wstring s2ws(const std::string& s) const;

    std::unique_ptr<std::thread> thread_;
    bool exit_;
    std::string updater_filepath_;
    std::string updater_arguments_;
    std::string user_runas_;
    std::string user_pass_;
    uint32_t count_;
    std::chrono::seconds interval_;
    HANDLE child_out_rd { NULL };
    HANDLE child_out_wr { NULL };
};

#endif
