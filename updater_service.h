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

private:
    void OnStart(DWORD argc, TCHAR* argv[]) override;
    void OnStop() override;

    void Work();
    void ProcessArgs(int argc, char *argv[]);
    bool CheckArgs() const;

    std::unique_ptr<std::thread> thread_;
    bool exit_;
    bool parsed_;
    std::string updater_filepath_;
    std::chrono::seconds interval_;
};

#endif
