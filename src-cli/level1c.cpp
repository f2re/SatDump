#include "level1c.h"

#include "init.h"
#include "logger.h"
#include "products/products.h"
#include "products/image_products.h"
#include "common/projection/sat_proj/sat_proj.h"
#include "common/tracking/tracking.h"
#include "common/geodetic/geodetic_coordinates.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    constexpr uint16_t QUALITY_GEOLOCATION_INVALID = 1u << 0;
    constexpr uint16_t QUALITY_TIME_INVALID = 1u << 1;
    constexpr uint16_t QUALITY_CALIBRATION_INVALID = 1u << 2;

    struct Options
    {
        fs::path product;
        fs::path output;
        std::set<std::string> channels;
        std::string instrument;
        std::string satellite;
        int stride = 1;
        bool require_calibrated = false;
        bool overwrite = false;
    };

    std::string usage(const char *program)
    {
        std::ostringstream text;
        text << "Usage: " << program
             << " <product-dir|product.cbor> <output-dir> [options]\n"
             << "  --channels 1,2,3       Export only named channels\n"
             << "  --instrument NAME      Override SatProf instrument id\n"
             << "  --satellite NAME       Override satellite name\n"
             << "  --stride N             Sample each Nth pixel in X/Y\n"
             << "  --require-calibrated   Fail without complete TB calibration\n"
             << "  --overwrite            Replace an existing export\n";
        return text.str();
    }

    std::set<std::string> split_channels(const std::string &value)
    {
        std::set<std::string> result;
        std::stringstream stream(value);
        std::string item;
        while (std::getline(stream, item, ','))
            if (!item.empty())
                result.insert(item);
        return result;
    }

    Options parse_options(int argc, char *argv[])
    {
        if (argc < 3)
            throw std::invalid_argument(usage(argv[0]));
        Options options;
        options.product = fs::absolute(argv[1]);
        options.output = fs::absolute(argv[2]);
        for (int index = 3; index < argc; ++index)
        {
            const std::string argument = argv[index];
            auto value = [&]() -> std::string
            {
                if (index + 1 >= argc)
                    throw std::invalid_argument("Missing value after " + argument);
                return argv[++index];
            };
            if (argument == "--channels")
                options.channels = split_channels(value());
            else if (argument == "--instrument")
                options.instrument = value();
            else if (argument == "--satellite")
                options.satellite = value();
            else if (argument == "--stride")
                options.stride = std::stoi(value());
            else if (argument == "--require-calibrated")
                options.require_calibrated = true;
            else if (argument == "--overwrite")
                options.overwrite = true;
            else if (argument == "-h" || argument == "--help")
                throw std::invalid_argument(usage(argv[0]));
            else
                throw std::invalid_argument("Unknown option: " + argument);
        }
        if (options.stride < 1 || options.stride > 1024)
            throw std::invalid_argument("--stride must be between 1 and 1024");
        return options;
    }

    std::string satprof_instrument(const std::string &source)
    {
        if (source == "mtvza")
            return "mtvza_gy";
        if (source == "msu_gs" || source == "msugs")
            return "msugs";
        if (source == "ikfs_2" || source == "ikfs2")
            return "ikfs2";
        return source;
    }

    uint32_t crc32(const uint8_t *data, size_t size)
    {
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t index = 0; index < size; ++index)
        {
            crc ^= data[index];
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

    template <typename T>
    nlohmann::json write_array(const fs::path &directory,
                               const std::string &filename,
                               const std::string &dtype,
                               const std::vector<size_t> &shape,
                               const std::string &units,
                               const std::vector<T> &values)
    {
        static_assert(std::is_trivially_copyable<T>::value, "binary type required");
        const fs::path path = directory / filename;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Could not create " + path.string());
        if (!values.empty())
            output.write(reinterpret_cast<const char *>(values.data()),
                         static_cast<std::streamsize>(values.size() * sizeof(T)));
        output.close();
        if (!output)
            throw std::runtime_error("Could not write " + path.string());
        const auto *bytes = reinterpret_cast<const uint8_t *>(values.data());
        const size_t byte_count = values.size() * sizeof(T);
        std::ostringstream checksum;
        checksum << std::hex << std::setw(8) << std::setfill('0')
                 << (byte_count ? crc32(bytes, byte_count) : 0u);
        return {{"path", filename},
                {"dtype", dtype},
                {"endian", "little"},
                {"shape", shape},
                {"units", units},
                {"bytes", byte_count},
                {"crc32", checksum.str()}};
    }

    double timestamp_at(const satdump::ImageProducts &products,
                        int channel,
                        int x,
                        int y,
                        int width,
                        int height,
                        const std::vector<double> &timestamps)
    {
        if (timestamps.empty())
            return std::numeric_limits<double>::quiet_NaN();
        if (timestamps.size() == 1)
            return timestamps.front();
        if (products.timestamp_type == satdump::ImageProducts::TIMESTAMP_LINE &&
            static_cast<int>(timestamps.size()) == height)
            return timestamps[std::min(y, height - 1)];
        if (products.timestamp_type == satdump::ImageProducts::TIMESTAMP_MULTIPLE_LINES)
        {
            const int block = std::max(1, products.images[channel].ifov_y > 0
                                             ? products.images[channel].ifov_y
                                             : products.ifov_y);
            return timestamps[std::min(timestamps.size() - 1,
                                       static_cast<size_t>(y / block))];
        }
        if (products.timestamp_type == satdump::ImageProducts::TIMESTAMP_IFOV)
        {
            const int block_x = std::max(1, products.images[channel].ifov_x > 0
                                               ? products.images[channel].ifov_x
                                               : products.ifov_x);
            const int block_y = std::max(1, products.images[channel].ifov_y > 0
                                               ? products.images[channel].ifov_y
                                               : products.ifov_y);
            const int blocks_x = std::max(1, (width + block_x - 1) / block_x);
            const size_t position = static_cast<size_t>((y / block_y) * blocks_x + x / block_x);
            return timestamps[std::min(timestamps.size() - 1, position)];
        }
        if (products.timestamp_type == satdump::ImageProducts::TIMESTAMP_SINGLE_IMAGE)
            return timestamps.front();
        const double fraction = height > 1 ? double(y) / double(height - 1) : 0.0;
        return timestamps[std::min(timestamps.size() - 1,
                                   static_cast<size_t>(std::llround(
                                       fraction * double(timestamps.size() - 1))))];
    }

    std::array<double, 3> spherical_ecef(const geodetic::geodetic_coords_t &position)
    {
        geodetic::geodetic_coords_t radians = position;
        radians = radians.toRads();
        const double radius = 6378.137 + position.alt;
        const double cos_latitude = std::cos(radians.lat);
        return {radius * cos_latitude * std::cos(radians.lon),
                radius * cos_latitude * std::sin(radians.lon),
                radius * std::sin(radians.lat)};
    }

    float satellite_zenith(const geodetic::geodetic_coords_t &ground,
                           satdump::SatelliteTracker *tracker,
                           double timestamp)
    {
        if (tracker == nullptr || !std::isfinite(timestamp) || timestamp <= 0)
            return std::numeric_limits<float>::quiet_NaN();
        try
        {
            const auto satellite = tracker->get_sat_position_at(timestamp);
            const auto ground_xyz = spherical_ecef(ground);
            const auto satellite_xyz = spherical_ecef(satellite);
            const std::array<double, 3> line = {
                satellite_xyz[0] - ground_xyz[0],
                satellite_xyz[1] - ground_xyz[1],
                satellite_xyz[2] - ground_xyz[2]};
            const double ground_norm = std::sqrt(ground_xyz[0] * ground_xyz[0] +
                                                 ground_xyz[1] * ground_xyz[1] +
                                                 ground_xyz[2] * ground_xyz[2]);
            const double line_norm = std::sqrt(line[0] * line[0] +
                                               line[1] * line[1] +
                                               line[2] * line[2]);
            if (ground_norm <= 0 || line_norm <= 0)
                return std::numeric_limits<float>::quiet_NaN();
            const double cosine = std::max(-1.0, std::min(1.0,
                (line[0] * ground_xyz[0] + line[1] * ground_xyz[1] +
                 line[2] * ground_xyz[2]) / (line_norm * ground_norm)));
            return static_cast<float>(std::acos(cosine) * RAD_TO_DEG);
        }
        catch (...)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
    }

    uint16_t raw_count(const satdump::ImageProducts &products,
                       int channel,
                       int x,
                       int y)
    {
        const image::Image &image = products.images[channel].image;
        const int difference = image.depth() - products.bit_depth;
        uint16_t value = image.get(0, x, y);
        if (difference >= 0)
            value >>= difference;
        else
            value <<= -difference;
        return value;
    }

    bool can_export_temperature(satdump::ImageProducts &products, int channel)
    {
        if (!products.has_calibation() || products.images[channel].abs_index == -2)
            return false;
        try
        {
            return products.get_calibration_type(channel) ==
                       satdump::ImageProducts::CALIB_RADIANCE &&
                   products.get_wavenumber(channel) > 0;
        }
        catch (...)
        {
            return false;
        }
    }

    nlohmann::json export_group(satdump::ImageProducts &products,
                                const Options &options,
                                const std::vector<int> &channel_indices,
                                int width,
                                int height,
                                const fs::path &directory,
                                const std::string &satellite_name)
    {
        fs::create_directories(directory);
        const int sampled_width = (width + options.stride - 1) / options.stride;
        const int sampled_height = (height + options.stride - 1) / options.stride;
        const size_t fov_count = size_t(sampled_width) * size_t(sampled_height);
        const size_t channel_count = channel_indices.size();

        std::vector<uint16_t> counts(fov_count * channel_count);
        std::vector<float> temperature(fov_count * channel_count,
                                       std::numeric_limits<float>::quiet_NaN());
        std::vector<float> latitude(fov_count, std::numeric_limits<float>::quiet_NaN());
        std::vector<float> longitude(fov_count, std::numeric_limits<float>::quiet_NaN());
        std::vector<double> observation_time(fov_count,
                                             std::numeric_limits<double>::quiet_NaN());
        std::vector<float> scan_position(fov_count,
                                         std::numeric_limits<float>::quiet_NaN());
        std::vector<float> zenith(fov_count, std::numeric_limits<float>::quiet_NaN());
        std::vector<uint16_t> quality(fov_count, 0);
        std::vector<uint32_t> source_x(fov_count, 0);
        std::vector<uint32_t> source_y(fov_count, 0);

        const int reference = channel_indices.front();
        const std::vector<double> timestamps = products.get_timestamps(reference);
        std::shared_ptr<satdump::SatelliteProjection> projection;
        std::unique_ptr<satdump::SatelliteTracker> tracker;
        double projection_ratio_x = 1.0;
        double projection_ratio_y = 1.0;
        if (products.has_proj_cfg() && products.has_tle())
        {
            try
            {
                projection = satdump::get_sat_proj(products.get_proj_cfg(),
                                                   products.get_tle(),
                                                   timestamps,
                                                   true);
                tracker = std::make_unique<satdump::SatelliteTracker>(products.get_tle());
                projection_ratio_x = std::max(1.0,
                    std::round(double(projection->img_size_x) / double(width)));
                projection_ratio_y = std::max(1.0,
                    std::round(double(projection->img_size_y) / double(height)));
            }
            catch (const std::exception &error)
            {
                logger->warn("Could not initialise geolocation: %s", error.what());
                projection.reset();
                tracker.reset();
            }
        }

        std::vector<bool> calibrated(channel_count, false);
        if (products.has_calibation())
        {
            products.init_calibration();
            for (size_t channel = 0; channel < channel_count; ++channel)
                calibrated[channel] = can_export_temperature(products,
                                                              channel_indices[channel]);
        }

        size_t valid_temperature = 0;
        for (int sampled_y = 0; sampled_y < sampled_height; ++sampled_y)
        {
            const int y = std::min(height - 1, sampled_y * options.stride);
            for (int sampled_x = 0; sampled_x < sampled_width; ++sampled_x)
            {
                const int x = std::min(width - 1, sampled_x * options.stride);
                const size_t fov = size_t(sampled_y) * sampled_width + sampled_x;
                source_x[fov] = uint32_t(x);
                source_y[fov] = uint32_t(y);
                scan_position[fov] = width > 1
                                         ? float(2.0 * x / double(width - 1) - 1.0)
                                         : 0.0f;
                observation_time[fov] = timestamp_at(products, reference, x, y,
                                                     width, height, timestamps);
                if (!std::isfinite(observation_time[fov]) || observation_time[fov] <= 0)
                    quality[fov] |= QUALITY_TIME_INVALID;

                geodetic::geodetic_coords_t position;
                const int offset_x = products.images[reference].offset_x;
                const int projection_x = int(std::llround(
                    double(x - offset_x) * projection_ratio_x));
                const int projection_y = int(std::llround(double(y) * projection_ratio_y));
                if (projection &&
                    !projection->get_position(projection_x, projection_y, position))
                {
                    position = position.toDegs();
                    latitude[fov] = float(position.lat);
                    longitude[fov] = float(position.lon);
                    zenith[fov] = satellite_zenith(position, tracker.get(),
                                                   observation_time[fov]);
                }
                else
                {
                    quality[fov] |= QUALITY_GEOLOCATION_INVALID;
                }

                bool any_temperature = false;
                for (size_t channel = 0; channel < channel_count; ++channel)
                {
                    const int index = channel_indices[channel];
                    const int channel_x = std::min<int>(
                        products.images[index].image.width() - 1, x);
                    const int channel_y = std::min<int>(
                        products.images[index].image.height() - 1, y);
                    counts[fov * channel_count + channel] =
                        raw_count(products, index, channel_x, channel_y);
                    if (!calibrated[channel])
                        continue;
                    const double value = products.get_calibrated_value(
                        index, channel_x, channel_y, true);
                    if (std::isfinite(value) && value != CALIBRATION_INVALID_VALUE &&
                        value > 0 && value < 1000)
                    {
                        temperature[fov * channel_count + channel] = float(value);
                        ++valid_temperature;
                        any_temperature = true;
                    }
                }
                if (!any_temperature)
                    quality[fov] |= QUALITY_CALIBRATION_INVALID;
            }
        }

        const double valid_fraction = fov_count && channel_count
                                          ? double(valid_temperature) /
                                                double(fov_count * channel_count)
                                          : 0.0;
        const std::string calibration_state = valid_fraction >= 0.95
                                                  ? "calibrated"
                                                  : valid_fraction > 0.0
                                                        ? "partial"
                                                        : "raw_counts";
        if (options.require_calibrated && calibration_state != "calibrated")
            throw std::runtime_error(
                "Selected channels do not provide complete brightness-temperature calibration");

        nlohmann::json channels = nlohmann::json::array();
        for (size_t channel = 0; channel < channel_count; ++channel)
        {
            const int index = channel_indices[channel];
            double wavenumber = -1.0;
            try
            {
                if (products.has_calibation())
                    wavenumber = products.get_wavenumber(index);
            }
            catch (...)
            {
            }
            channels.push_back({{"id", products.images[index].channel_name},
                                {"product_index", index},
                                {"source_file", products.images[index].filename},
                                {"abs_index", products.images[index].abs_index},
                                {"offset_x", products.images[index].offset_x},
                                {"wavenumber_cm-1", wavenumber},
                                {"brightness_temperature_available", calibrated[channel]}});
        }

        nlohmann::json arrays;
        arrays["raw_counts"] = write_array(directory, "raw_counts.u16", "uint16",
                                             {fov_count, channel_count}, "count", counts);
        arrays["brightness_temperature"] = write_array(
            directory, "brightness_temperature.f32", "float32",
            {fov_count, channel_count}, "K", temperature);
        arrays["latitude"] = write_array(directory, "latitude.f32", "float32",
                                          {fov_count}, "degree_north", latitude);
        arrays["longitude"] = write_array(directory, "longitude.f32", "float32",
                                           {fov_count}, "degree_east", longitude);
        arrays["observation_time"] = write_array(
            directory, "observation_time.f64", "float64", {fov_count},
            "seconds since 1970-01-01T00:00:00Z", observation_time);
        arrays["scan_position"] = write_array(directory, "scan_position.f32",
                                               "float32", {fov_count}, "1",
                                               scan_position);
        arrays["satellite_zenith"] = write_array(
            directory, "satellite_zenith.f32", "float32", {fov_count},
            "degree", zenith);
        arrays["quality_flag"] = write_array(directory, "quality_flag.u16",
                                              "uint16", {fov_count}, "bitmask",
                                              quality);
        arrays["source_x"] = write_array(directory, "source_x.u32", "uint32",
                                          {fov_count}, "pixel", source_x);
        arrays["source_y"] = write_array(directory, "source_y.u32", "uint32",
                                          {fov_count}, "pixel", source_y);

        const std::string instrument = options.instrument.empty()
                                           ? satprof_instrument(products.instrument_name)
                                           : options.instrument;
        nlohmann::json manifest = {
            {"schema", "satprof.level1c-binary/1"},
            {"producer", "SatDump 1.2.2"},
            {"instrument", instrument},
            {"satdump_instrument", products.instrument_name},
            {"satellite", satellite_name},
            {"calibration_state", calibration_state},
            {"valid_brightness_temperature_fraction", valid_fraction},
            {"source_shape", {height, width}},
            {"sampled_shape", {sampled_height, sampled_width}},
            {"stride", options.stride},
            {"fov_count", fov_count},
            {"channel_count", channel_count},
            {"channels", channels},
            {"arrays", arrays},
            {"quality_flag_masks",
             {{"geolocation_invalid", QUALITY_GEOLOCATION_INVALID},
              {"time_invalid", QUALITY_TIME_INVALID},
              {"calibration_invalid", QUALITY_CALIBRATION_INVALID}}}};
        if (products.has_tle())
            manifest["tle"] = products.get_tle();
        const fs::path manifest_path = directory / "satprof-level1c.json";
        std::ofstream output(manifest_path, std::ios::trunc);
        output << std::setw(2) << manifest << '\n';
        if (!output)
            throw std::runtime_error("Could not write " + manifest_path.string());
        return {{"manifest", fs::relative(manifest_path, options.output).generic_string()},
                {"source_shape", {height, width}},
                {"sampled_shape", {sampled_height, sampled_width}},
                {"channels", channels},
                {"calibration_state", calibration_state}};
    }
}

int main_level1c(int argc, char *argv[])
{
    if (argc == 2 &&
        (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))
    {
        logger->info("%s", usage(argv[0]).c_str());
        return 0;
    }

    Options options;
    try
    {
        options = parse_options(argc, argv);
    }
    catch (const std::invalid_argument &error)
    {
        logger->error("%s", error.what());
        return 2;
    }

    try
    {
        if (!fs::exists(options.product))
            throw std::runtime_error("Product does not exist: " +
                                     options.product.string());
        if (fs::exists(options.output))
        {
            const fs::path index = options.output / "satprof-level1c-index.json";
            if (!options.overwrite && fs::exists(index))
                throw std::runtime_error(
                    "Output already contains a Level-1C index; use --overwrite");
            if (options.overwrite)
            {
                const fs::path resolved = fs::weakly_canonical(options.output);
                if (resolved == resolved.root_path() || resolved.filename().empty())
                    throw std::runtime_error(
                        "Refusing to remove unsafe output path: " + resolved.string());
                fs::remove_all(options.output);
            }
        }
        fs::create_directories(options.output);

        satdump::initSatdump();
        completeLoggerInit();
        const auto loaded = satdump::loadProducts(options.product.string());
        const auto products =
            std::dynamic_pointer_cast<satdump::ImageProducts>(loaded);
        if (!products)
            throw std::runtime_error("Only SatDump image products are supported");
        if (products->images.empty())
            throw std::runtime_error("Product has no image channels");

        std::map<std::pair<int, int>, std::vector<int>> groups;
        for (size_t index = 0; index < products->images.size(); ++index)
        {
            const auto &holder = products->images[index];
            if (!options.channels.empty() &&
                options.channels.count(holder.channel_name) == 0)
                continue;
            if (holder.image.size() == 0)
            {
                logger->warn("Skipping empty channel %s",
                             holder.channel_name.c_str());
                continue;
            }
            groups[{int(holder.image.width()), int(holder.image.height())}].push_back(
                int(index));
        }
        if (groups.empty())
            throw std::runtime_error("No compatible channels were selected");

        std::string satellite = options.satellite;
        if (satellite.empty() && products->has_tle())
            satellite = products->get_tle().name;
        if (satellite.empty())
            satellite = "unknown";

        nlohmann::json index = {
            {"schema", "satprof.level1c-index/1"},
            {"source_product", options.product.string()},
            {"instrument", options.instrument.empty()
                               ? satprof_instrument(products->instrument_name)
                               : options.instrument},
            {"satellite", satellite},
            {"groups", nlohmann::json::array()}};
        int number = 0;
        for (const auto &entry : groups)
        {
            const int width = entry.first.first;
            const int height = entry.first.second;
            const std::string group_name = "group-" + std::to_string(++number) +
                                           "-" + std::to_string(width) + "x" +
                                           std::to_string(height);
            logger->info("Exporting %d channels at %dx%d",
                         int(entry.second.size()), width, height);
            index["groups"].push_back(export_group(
                *products, options, entry.second, width, height,
                options.output / group_name, satellite));
        }
        const fs::path index_path = options.output / "satprof-level1c-index.json";
        std::ofstream output(index_path, std::ios::trunc);
        output << std::setw(2) << index << '\n';
        if (!output)
            throw std::runtime_error("Could not write " + index_path.string());
        logger->info("SatProf Level-1C index: %s", index_path.string().c_str());
        return 0;
    }
    catch (const std::exception &error)
    {
        logger->error("Level-1C export failed: %s", error.what());
        return 1;
    }
}
