#include "logger.h"
#include "live.h"
#include "offline.h"
#include "record.h"
#include "autotrack/autotrack.h"
#include "level1c.h"
#include "reprocess.h"
#include "sdr_probe.h"
#include "project/project.h"

int main(int argc, char *argv[])
{
    initLogger();
    if (argc < 2)
    {
        logger->error("Please specify live/record/reprocess or a pipeline name!");
        return 1;
    }
    const std::string command = argv[1];
    if (command == "reprocess")
        return main_reprocess(argc, argv);
    if (command == "live")
    {
        int ret = main_live(argc, argv);
        if (ret != 0) return ret;
    }
    else if (command == "record")
    {
        int ret = main_record(argc, argv);
        if (ret != 0) return ret;
    }
    else if (command == "autotrack")
    {
        int ret = main_autotrack(argc, argv);
        if (ret != 0) return ret;
    }
    else if (command == "project")
    {
        try
        {
            int ret = main_project(argc - 2, argv + 2);
            if (ret != 0) return ret;
        }
        catch (std::exception &e)
        {
            logger->error("Error running project! %s", e.what());
            return 1;
        }
    }
    else if (command == "level1c")
        return main_level1c(argc - 1, argv + 1);
    else if (command == "version" || command == "--v")
    {
        logger->info("This is SatDump v" + (std::string)SATDUMP_VERSION);
        return 0;
    }
    else if (command == "sdr_probe")
        sdr_probe();
    else
    {
        int ret = main_offline(argc, argv);
        if (ret != 0) return ret;
    }
    logger->info("Done! Goodbye");
    return 0;
}
