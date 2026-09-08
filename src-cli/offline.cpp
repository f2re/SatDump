#include "offline.h"
#include "common/detect_header.h"
#include "init.h"
#include "common/cli_utils.h"
#include "core/pipeline.h"
#include "reprocess.h"
#include <filesystem>
#include "nlohmann/json.hpp"
#include "logger.h"

int main_offline(int argc, char *argv[])
{
    if (argc < 5)
    {
        logger->error("Usage: satdump <pipeline_id> <input_level> <input_file> <output_directory> [options]");
        logger->error("Options: --samplerate RATE --baseband_format s16 --offline --processing_config FILE");
        return 2;
    }
    try
    {
        const std::string pipeline_name = argv[1];
        const std::string input_level = argv[2];
        const std::string input_file = argv[3];
        const std::string output_file = argv[4];
        nlohmann::json parameters = parse_common_flags(argc - 5, &argv[5]);
        try_get_params_from_input_file(parameters, input_file);
        satdump::tle_file_override = parameters.value("tle_override", std::string());
        if (parameters.value("offline", false))
            satdump::tle_do_update_on_init = false;
        satdump::initSatdump();
        completeLoggerInit();
        station_processing_config(parameters.value("processing_config", std::string()));
        auto pipeline = satdump::getPipelineFromName(pipeline_name);
        if (!pipeline.has_value())
        {
            logger->critical("Pipeline " + pipeline_name + " does not exist!");
            return 1; // Do not report a successful unattended job for an invalid id.
        }
        if (!std::filesystem::exists(output_file))
            std::filesystem::create_directories(output_file);
        logger->info("Starting processing pipeline " + pipeline_name + "...");
        pipeline.value().run(input_file, output_file, parameters, input_level);
        return 0;
    }
    catch (const std::exception &error)
    {
        logger->error("Fatal error running pipeline: %s", error.what());
        return 1;
    }
}
