#include "updater_service.h"

int main(int argc, char* argv[])
{
    UpdaterService service(argc, argv);
    service.Run();
    return 0;
}
