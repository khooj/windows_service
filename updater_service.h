#ifndef UPDATER_SERVICE_H
#define UPDATER_SERVICE_H

#include "service_base.h"
#include <thread>
#include <mutex>
#include <memory>

class UpdaterService : public ServiceBase
{
public:
    explicit UpdaterService();

    UpdaterService(const UpdaterService&) = delete;
    UpdaterService& operator=(const UpdaterService&) = delete;

    UpdaterService(UpdaterService&&) = delete;
    UpdaterService& operator=(UpdaterService&&) = delete;

private:
    void OnStart(DWORD argc, TCHAR* argv[]) override;
    void OnStop() override;

    void Work();

    std::unique_ptr<std::thread> thread_;
    std::mutex mut_;
};

#endif