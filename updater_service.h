#ifndef UPDATER_SERVICE_H
#define UPDATER_SERVICE_H

#include "service_base.h"
#include <thread>
#include <memory>

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


    std::unique_ptr<std::thread> thread_;
    bool exit_;
};

#endif
