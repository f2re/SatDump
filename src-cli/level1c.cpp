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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
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
#include <tuple>
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
        std::string instrument_override;
        std::string satellite_override;
        int stride = 1;
        bool require_calibrated = false;
        bool overwrite = false;
    };

    std::string usage(const char *program)
    {
        std::ostringstream out;
        out << "Usage: " << program << " <product-dir|product.cbor> <output-dir> [options]\n"
            << "Options:\n"
            << "  --channels 1,2,3       Export only named channels\n"
            << "  --instrument NAME      Override SatProf instrument id\n"
            << "  --satellite NAME       Override satellite name\n"
            << "  --stride N             Sample each Nth pixel in X and Y\n"
            << "  --require-calibrated   Fail if no brightness-temperature channel exists\n"
            << "  --overwrite            Replace existing exporter files\n";
        return out.str();
    }

    std::set<std::string> split_channels(const std::string &text)
    {
        std::set<std::string> values;
        std::stringstream stream(text);
        std::string item;
        while (std::getline(stream, item, ','))
        {
            if (!item.empty())
                values.insert(item);
        }
        return values;
    }

    Options parse_options(int argc, char *argv[])
    {
        if (argc < 3)
            throw std::invalid_argument(usage(argv[0]));
        Options options;
        options.product = fs::absolute(argv[1]);
        options.output = fs::absolute(argv[2]);
        for (int i = 3; i < argc; ++i)
        {
            const std::string argument = argv[i];
            auto require_value = [&]() -> std::string
            {
                if (i + 1 >= argc)
                    throw std::invalid_argument("Missing value after " + argument);
                return argv[++i];
            };
            if (argument == "--channels")
                options.channels = split_channels(require_value());
            else if (argument == "--instrument")
                options.instrument_override = require_value();
            else if (argument == "--satellite")
                options.satellite_override = require_value();
            else if (argument == "--stride")
                options.stride = std::stoi(require_value());
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

    std::string satprof_instrument_name(const std::string &source)
    {
        if (source == "mtvza")
            return "mtvza_gy";
        if (source == "msu_gs" || source == "msugs")
            return "msugs";
        if (source == "ikfs_2" || source == "ikfs2")
            return "ikfs2";
        return source;
    }

    uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
    {
        crc = ~crc;
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
                               const std::string &name,
                               const std::string &dtype,
                               const std::vector<size_t> &shape,
                               const std::string &units,
                               const std::vector<T> &values)
    {
        static_assert(std::is_trivially_copyable<T>::value, "binary arrays require trivial types");
        const fs::path path = directory / name;
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
            throw std::runtime_error("Could not create " + path.string());
        if (!values.empty())
            stream.write(reinterpret_cast<const char *>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(T)));
        stream.close();
        if (!stream)
            throw std::runtime_error("Could not write " + path.string());
        const uint32_t crc = values.empty()
                                 ? 0u
                                 : crc32_update(0, reinterpret_cast<const uint8_t *>(values.data()), values.size() * sizeof(T));
        std::ostringstream crc_text;
        crc_text << std::hex << std::setw(8) << std::setfill('0') << crc;
        return {
            {"path", name},
            {"dtype", dtype},
            {"endian", "little"},
            {"shape", shape},
            {"units", units},
            {"bytes", values.size() * sizeof(T)},
            {"crc32", crc_text.str()},
        };
    }

    double timestamp_at(const satdump::ImageProducts &products,
                        int channel_index,
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
        switch (products.timestamp_type)
        {
        case satdump::ImageProducts::TIMESTAMP_LINE:
            if (static_cast<int>(timestamps.size()) == height)
                return timestamps[std::min(y, height - 1)];
            break;
        case satdump::ImageProducts::TIMESTAMP_MULTIPLE_LINES:
        {
            const int ifov_y = std::max(1, products.images[channel_index].ifov_y > 0
                                               ? products.images[channel_index].ifov_y
                                               : products.ifov_y);
            const size_t index = std::min(timestamps.size() - 1, static_cast<size_t>(y / ifov_y));
            return timestamps[index];
        }
        case satdump::ImageProducts::TIMESTAMP_IFOV:
        {
            const int ifov_x = std::max(1, products.images[channel_index].ifov_x > 0
                                               ? products.images[channel_index].ifov_x
                                               : products.ifov_x);
            const int ifov_y = std::max(1, products.images[channel_index].ifov_y > 0
                                               ? products.images[channel_index].ifov_y
                                               : products.ifov_y);
            const int blocks_x = std::max(1, (width + ifov_x - 1) / ifov_x);
            const size_t index = std::min(timestamps.size() - 1,
                                          static_cast<size_t>((y / ifov_y) * blocks_x + x / ifov_x));
            return timestamps[index];
        }
        case satdump::ImageProducts::TIMESTAMP_SINGLE_IMAGE:
            return timestamps.front();
        }
        const double relative = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.0;
        const size_t index = std::min(timestamps.size() - 1,
                                      static_cast<size_t>(std::llround(relative * static_cast<double>(timestamps.size() - 1))));
        return timestamps[index];
    }

    std::array<double, 3> spherical_ecef(const geodetic::geodetic_coords_t &position)
    {
        geodetic::geodetic_coords_t radians = position;
        radians = radians.toRads();
        const double radius = 6378.137 + position.alt;
        const double cos_latitude = std::cos(radians.lat);
        return {
            radius * cos_latitude * std::cos(radians.lon),
            radius * cos_latitude * std::sin(radians.lon),
            radius * std::sin(radians.lat),
        };
    }

    float satellite_zenith(const geodetic::geodetic_coords_t &ground,
                           satdump::SatelliteTracker *tracker,
                           double timestamp)
    {
        if (tracker == nullptr || !std::isfinite(timestamp) || timestamp <= 0)
            return std::numeric_limits<float>::quiet_NaN();
        try
        {
            const geodetic::geodetic_coords_t satellite = tracker->get_sat_position_at(timestamp);
            const auto g = spherical_ecef(ground);
            const auto s = spherical_ecef(satellite);
            const double ground_norm = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
            const std::array<double, 3> line_of_sight = {s[0] - g[0], s[1] - g[1], s[2] - g[2]};
            const double los_norm = std::sqrt(line_of_sight[0] * line_of_sight[0] +
                                              line_of_sight[1] * line_of_sight[1] +
                                              line_of_sight[2] * line_of_sight[2]);
            if (ground_norm <= 0 || los_norm <= 0)
                return std::numeric_limits<float>::quiet_NaN();
            const double cosine = std::max(-1.0, std::min(1.0,
                (line_of_sight[0] * g[0] + line_of_sight[1] * g[1] + line_of_sight[2] * g[2]) /
                (los_norm * ground_norm)));
            return static_cast<float>(std::acos(cosine) * RAD_TO_DEG);
        }
        catch (...)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }
    }

    uint16_t raw_count(const satdump::ImageProducts &products, int channel_index, int x, int y)
    {
        const image::Image &image = products.images[channel_index].image;
        const int difference = image.depth() - products.bit_depth;
        uint16_t value = image.get(0, x, y);
        if (difference >= 0)
            value >>= difference;
        else
            value <<= -difference;
        return value;
    }

    nlohmann::json export_group(satdump::ImageProducts &products,
                                const Options &options,
                                const std::vector<int> &channel_indices,
                                int width,
                                int height,
                                const fs::path &group_directory,
                                const std::string &satellite_name)
    {
        fs::create_directories(group_directory);
        const int sampled_width = (width + options.stride - 1) / options.stride;
        const int sampled_height = (height + options.stride - 1) / options.stride;
        const size_t fov_count = static_cast<size_t>(sampled_width) * static_cast<size_t>(sampled_height);
        const size_t channel_count = channel_indices.size();

        std::vector<uint16_t> counts(fov_count * channel_count);
        std::vector<float> brightness_temperature(fov_count * channel_count,
                                                   std::numeric_limits<float>::quiet_NaN());
        std::vector<float> latitude(fov_count, std::numeric_limits<float>::quiet_NaN());
        std::vector<float> longitude(fov_count, std::numeric_limits<float>::quiet_NaN());
        std::vector<double> observation_time(fov_count, std::numeric_limits<double>::quiet_NaN());
        std::vector<float> scan_position(fov_count, std::numeric_limits<float>::quiet_NaN());
        std::vector<float> zenith(fov_count, std::numeric_limits<float>::quiet_NaN());
        std::vector<uint16_t> quality(fov_count, 0);
        std::vector<uint32_t> source_x(fov_count, 0);
        std::vector<uint32_t> source_y(fov_count, 0);

        const int reference_channel = channel_indices.front();
        const std::vector<double> timestamps = products.get_timestamps(reference_channel);
        std::shared_ptr<satdump::SatelliteProjection> projection;
        std::unique_ptr<satdump::SatelliteTracker> tracker;
        if (products.has_proj_cfg() && products.has_tle())
        {
            try
            {
                projection = satdump::get_sat_proj(products.get_proj_cfg(), products.get_tle(), timestamps, true);
                tracker = std::make_unique<satdump::SatelliteTracker>(products.get_tle());
            }
            catch (const std::exception &error)
            {
                logger->warn("Could not initialise geolocation: %s", error.what());
            }
        }

        std::vector<bool> can_temperature(channel_count, false);
        if (products.has_calibation())
        {
            products.init_calibration();
            for (size_t channel = 0; channel < channel_count; ++channel)
            {
                const int index = channel_indices[channel];
                can_temperature[channel] = products.images[index].abs_index != -2 &&
                                           products.get_calibration_type(index) == satdump::ImageProducts::CALIB_RADIANCE &&
                                           products.get_wavenumber(index) > 0;
            }
        }

        size_t valid_temperature_values = 0;
        for (int sy = 0; sy < sampled_height; ++sy)
        {
            const int y = std::min(height - 1, sy * options.stride);
            for (int sx = 0; sx < sampled_width; ++sx)
            {
                const int x = std::min(width - 1, sx * options.stride);
                const size_t fov = static_cast<size_t>(sy) * sampled_width + sx;
                source_x[fov] = static_cast<uint32_t>(x);
                source_y[fov] = static_cast<uint32_t>(y);
                scan_position[fov] = width > 1 ? static_cast<float>(2.0 * x / static_cast<double>(width - 1) - 1.0) : 0.0f;
                observation_time[fov] = timestamp_at(products, reference_channel, x, y, width, height, timestamps);
                if (!std::isfinite(observation_time[fov]) || observation_time[fov] <= 0)
                    quality[fov] |= QUALITY_TIME_INVALID;

                geodetic::geodetic_coords_t position;
                const int projection_x = x + products.images[reference_channel].offset_x;
                if (projection && !projection->get_position(projection_x, y, position))
                {
                    position = position.toDegs();
                    latitude[fov] = static_cast<float>(position.lat);
                    longitude[fov] = static_cast<float>(position.lon);
                    zenith[fov] = satellite_zenith(position, tracker.get(), observation_time[fov]);
                }
                else
                {
                    quality[fov] |= QUALITY_GEOLOCATION_INVALID;
                }

                bool calibrated_here = false;
                for (size_t channel = 0; channel < channel_count; ++channel)
                {
                    const int index = channel_indices[channel];
                    const int channel_x = std::min<int>(products.images[index].image.width() - 1, x);
                    const int channel_y = std::min<int>(products.images[index].image.height() - 1, y);
                    counts[fov * channel_count + channel] = raw_count(products, index, channel_x, channel_y);
                    if (can_temperature[channel])
                    {
                        const double value = products.get_calibrated_value(index, channel_x, channel_y, true);
                        if (std::isfinite(value) && value != CALIBRATION_INVALID_VALUE && value > 0 && value < 1000)
                        {
                            brightness_temperature[fov * channel_count + channel] = static_cast<float>(value);
                            ++valid_temperature_values;
                            calibrated_here = true;
                        }
                    }
                }
                if (!calibrated_here)
                    quality[fov] |= QUALITY_CALIBRATION_INVALID;
            }
        }

        const double valid_temperature_fraction = channel_count && fov_count
                                                      ? static_cast<double>(valid_temperature_values) /
                                                            static_cast<double>(channel_count * fov_count)
                                                      : 0.0;
        const std::string calibration_state = valid_temperature_fraction >= 0.95
                                                  ? "calibrated"
                                                  : valid_temperature_fraction > 0.0 ? "partial" : "raw_counts";
        if (options.require_calibrated && calibration_state != "calibrated")
            throw std::runtime_error("The selected product does not provide complete brightness-temperature calibration");

        nlohmann::json channel_metadata = nlohmann::json::array();
        for (size_t channel = 0; channel < channel_count; ++channel)
        {
            const int index = channel_indices[channel];
            channel_metadata.push_back({
                {"id", products.images[index].channel_name},
                {"product_index", index},
                {"source_file", products.images[index].filename},
                {"abs_index", products.images[index].abs_index},
                {"offset_x", products.images[index].offset_x},
                {"wavenumber_cm-1", products.has_calibation() ? products.get_wavenumber(index) : -1.0},
                {"brightness_temperature_available", can_temperature[channel]},
            });
        }

        nlohmann::json arrays;
        arrays["raw_counts"] = write_array(group_directory, "raw_counts.u16", "uint16", {fov_count, channel_count}, "count", counts);
        arrays["brightness_temperature"] = write_array(group_directory, "brightness_temperature.f32", "float32", {fov_count, channel_count}, "K", brightness_temperature);
        arrays["latitude"] = write_array(group_directory, "latitude.f32", "float32", {fov_count}, "degree_north", latitude);
        arrays["longitude"] = write_array(group_directory, "longitude.f32", "float32", {fov_count}, "degree_east", longitude);
        arrays["observation_time"] = write_array(group_directory, "observation_time.f64", "float64", {fov_count}, "seconds since 1970-01-01T00:00:00Z", observation_time);
        arrays["scan_position"] = write_array(group_directory, "scan_position.f32", "float32", {fov_count}, "1", scan_position);
        arrays["satellite_zenith"] = write_array(group_directory, "satellite_zenith.f32", "float32", {fov_count}, "degree", zenith);
        arrays["quality_flag"] = write_array(group_directory, "quality_flag.u16", "uint16", {fov_count}, "bitmask", quality);
        arrays["source_x"] = write_array(group_directory, "source_x.u32", "uint32", {fov_count}, "pixel", source_x);
        arrays["source_y"] = write_array(group_directory, "source_y.u32", "uint32", {fov_count}, "pixel", source_y);

        const std::string instrument = options.instrument_override.empty()
                                           ? satprof_instrument_name(products.instrument_name)
                                           : options.instrument_override;
        nlohmann::json manifest = {
            {"schema", "satprof.level1c-binary/1"},
            {"producer", "SatDump 1.2.2"},
            {"instrument", instrument},
            {"satdump_instrument", products.instrument_name},
            {"satellite", satellite_name},
            {"calibration_state", calibration_state},
            {"valid_brightness_temperature_fraction", valid_temperature_fraction},
            {"source_shape", {height, width}},
            {"sampled_shape", {sampled_height, sampled_width}},
            {"stride", options.stride},
            {"fov_count", fov_count},
            {"channel_count", channel_count},
            {"channels", channel_metadata},
            {"arrays", arrays},
            {"quality_flag_masks", {
                {"geolocation_invalid", QUALITY_GEOLOCATION_INVALID},
                {"time_invalid", QUALITY_TIME_INVALID},
                {"calibration_invalid", QUALITY_CALIBRATION_INVALID},
            }},
        };
        if (products.has_tle())
            manifest["tle"] = products.get_tle();
        const fs::path manifest_path = group_directory / "satprof-level1c.json";
        std::ofstream manifest_stream(manifest_path, std::ios::trunc);
        manifest_stream << std::setw(2) << manifest << '\n';
        if (!manifest_stream)
            throw std::runtime_error("Could not write " + manifest_path.string());
        return {
            {"manifest", fs::relative(manifest_path, options.output).generic_string()},
            {"source_shape", {height, width}},
            {"sampled_shape", {sampled_height, sampled_width}},
            {"channels", channel_metadata},
            {"calibration_state", calibration_state},
        };
    }
}

int main_level1c(int argc, char *argv[])
{
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))
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
            throw std::runtime_error("Product does not exist: " + options.product.string());
        if (fs::exists(options.output))
        {
            const fs::path index = options.output / "satprof-level1c-index.json";
            if (!options.overwrite && fs::exists(index))
                throw std::runtime_error("Output already contains a Level-1C index; use --overwrite");
            if (options.overwrite)
            {
                const fs::path resolved = fs::weakly_canonical(options.output);
                if (resolved == resolved.root_path() || resolved.filename().empty())
                    throw std::runtime_error("Refusing to remove unsafe output path: " + resolved.string());
                fs::remove_all(options.output);
            }
        }
        fs::create_directories(options.output);

        satdump::initSatdump();
        completeLoggerInit();
        std::shared_ptr<satdump::Products> loaded = satdump::loadProducts(options.product.string());
        std::shared_ptr<satdump::ImageProducts> products = std::dynamic_pointer_cast<satdump::ImageProducts>(loaded);
        if (!products)
            throw std::runtime_error("Only SatDump image products are supported");
        if (products->images.empty())
            throw std::runtime_error("The product has no channels");

        std::map<std::pair<int, int>, std::vector<int>> groups;
        for (size_t index = 0; index < products->images.size(); ++index)
        {
            const auto &holder = products->images[index];
            if (!options.channels.empty() && options.channels.count(holder.channel_name) == 0)
                continue;
            if (holder.image.size() == 0)
            {
                logger->warn("Skipping empty channel %s", holder.channel_name.c_str());
                continue;
            }
            groups[{static_cast<int>(holder.image.width()), static_cast<int>(holder.image.height())}].push_back(static_cast<int>(index));
        }
        if (groups.empty())
            throw std::runtime_error("No compatible channels were selected");

        std::string satellite_name = options.satellite_override;
        if (satellite_name.empty() && products->has_tle())
            satellite_name = products->get_tle().name;
        if (satellite_name.empty())
            satellite_name = "unknown";

        nlohmann::json index = {
            {"schema", "satprof.level1c-index/1"},
            {"source_product", options.product.string()},
            {"instrument", options.instrument_override.empty() ? satprof_instrument_name(products->instrument_name) : options.instrument_override},
            {"satellite", satellite_name},
            {"groups", nlohmann::json::array()},
        };
        int group_number = 0;
        for (const auto &entry : groups)
        {
            const int width = entry.first.first;
            const int height = entry.first.second;
            const std::string group_name = "group-" + std::to_string(++group_number) + "-" +
                                           std::to_string(width) + "x" + std::to_string(height);
            logger->info("Exporting %d channels at %dx%d", static_cast<int>(entry.second.size()), width, height);
            index["groups"].push_back(export_group(*products, options, entry.second, width, height,
                                                   options.output / group_name, satellite_name));
        }
        const fs::path index_path = options.output / "satprof-level1c-index.json";
        std::ofstream stream(index_path, std::ios::trunc);
        stream << std::setw(2) << index << '\n';
        if (!stream)
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
