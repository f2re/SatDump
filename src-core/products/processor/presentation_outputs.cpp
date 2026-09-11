#include "presentation_outputs.h"

#include "common/image/io.h"
#include "common/image/meta.h"
#include "common/projection/gcp_compute/gcp_compute.h"
#include "common/projection/projs2/proj_json.h"
#include "core/config.h"
#include "logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace satdump
{
    namespace product_presentation
    {
        namespace
        {
            using image::presentation::FrameKind;
            using image::presentation::LayoutKind;
            using image::presentation::OrientationInfo;
            using image::presentation::RasterTransform;

            std::string lowercase_ascii(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                               { return (char)std::tolower(character); });
                return value;
            }

            bool json_bool(const nlohmann::json &value, bool fallback)
            {
                try
                {
                    if (value.is_boolean())
                        return value.get<bool>();
                    if (value.is_object() && value.contains("value") && value["value"].is_boolean())
                        return value["value"].get<bool>();
                }
                catch (const std::exception &)
                {
                }
                return fallback;
            }

            std::string json_string(const nlohmann::json &value, const std::string &fallback = "")
            {
                try
                {
                    if (value.is_string())
                        return value.get<std::string>();
                    if (value.is_object() && value.contains("value") && value["value"].is_string())
                        return value["value"].get<std::string>();
                }
                catch (const std::exception &)
                {
                }
                return fallback;
            }

            image::presentation::Color parse_color(const nlohmann::json &value, const image::presentation::Color &fallback)
            {
                try
                {
                    if (value.is_array() && value.size() >= 3)
                    {
                        image::presentation::Color color = {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
                        const double maximum = std::max(color[0], std::max(color[1], color[2]));
                        if (maximum > 1.0)
                        {
                            color[0] /= 255.0;
                            color[1] /= 255.0;
                            color[2] /= 255.0;
                        }
                        for (double &component : color)
                            component = std::max(0.0, std::min(1.0, component));
                        return color;
                    }

                    if (value.is_string())
                    {
                        std::string text = value.get<std::string>();
                        if (!text.empty() && text.front() == '#')
                            text.erase(text.begin());
                        if (text.size() == 6 || text.size() == 8)
                        {
                            return {
                                (double)std::stoi(text.substr(0, 2), nullptr, 16) / 255.0,
                                (double)std::stoi(text.substr(2, 2), nullptr, 16) / 255.0,
                                (double)std::stoi(text.substr(4, 2), nullptr, 16) / 255.0};
                        }
                    }
                }
                catch (const std::exception &)
                {
                }
                return fallback;
            }

            void apply_theme_overrides(image::presentation::Theme &theme, const nlohmann::json &section)
            {
                if (!section.is_object())
                    return;
                if (section.contains("panel"))
                    theme.panel = parse_color(section["panel"], theme.panel);
                if (section.contains("panel_secondary"))
                    theme.panel_secondary = parse_color(section["panel_secondary"], theme.panel_secondary);
                if (section.contains("border"))
                    theme.border = parse_color(section["border"], theme.border);
                if (section.contains("text"))
                    theme.text = parse_color(section["text"], theme.text);
                if (section.contains("muted_text"))
                    theme.muted_text = parse_color(section["muted_text"], theme.muted_text);
                if (section.contains("accent"))
                    theme.accent = parse_color(section["accent"], theme.accent);
                if (section.contains("warning"))
                    theme.warning = parse_color(section["warning"], theme.warning);
                if (section.contains("error"))
                    theme.error = parse_color(section["error"], theme.error);
                if (section.contains("red_component"))
                    theme.red_component = parse_color(section["red_component"], theme.red_component);
                if (section.contains("green_component"))
                    theme.green_component = parse_color(section["green_component"], theme.green_component);
                if (section.contains("blue_component"))
                    theme.blue_component = parse_color(section["blue_component"], theme.blue_component);
                if (section.contains("reference_width") && section["reference_width"].is_number_integer())
                    theme.reference_width = std::max(320, section["reference_width"].get<int>());
                if (section.contains("minimum_scale") && section["minimum_scale"].is_number())
                    theme.minimum_scale = std::max(0.25, section["minimum_scale"].get<double>());
                if (section.contains("maximum_scale") && section["maximum_scale"].is_number())
                    theme.maximum_scale = std::max(theme.minimum_scale, section["maximum_scale"].get<double>());
            }

            void apply_layout_overrides(image::presentation::PresentationSpec &spec,
                                        const nlohmann::json &composite_preset,
                                        LayoutKind layout)
            {
                if (!composite_preset.is_object() || !composite_preset.contains("presentation") || !composite_preset["presentation"].is_object())
                    return;

                const nlohmann::json &presentation = composite_preset["presentation"];
                const char *key = layout == LayoutKind::Minimal ? "minimal" : "editorial";
                const nlohmann::json *layout_section = nullptr;
                if (presentation.contains(key) && presentation[key].is_object())
                    layout_section = &presentation[key];
                else if (layout == LayoutKind::Editorial && presentation.contains("presentational") && presentation["presentational"].is_object())
                    layout_section = &presentation["presentational"];

                if (layout_section == nullptr)
                    return;
                if (layout_section->contains("branding") && (*layout_section)["branding"].is_string())
                    spec.branding = (*layout_section)["branding"].get<std::string>();
                if (layout_section->contains("show_branding"))
                    spec.show_branding = json_bool((*layout_section)["show_branding"], spec.show_branding);
                if (layout_section->contains("theme"))
                    apply_theme_overrides(spec.theme, (*layout_section)["theme"]);
            }

            const nlohmann::json *at_path(const nlohmann::json &root, const std::vector<std::string> &path)
            {
                const nlohmann::json *current = &root;
                for (const std::string &part : path)
                {
                    if (!current->is_object() || !current->contains(part))
                        return nullptr;
                    current = &((*current)[part]);
                }
                return current;
            }

            std::string first_string(const nlohmann::json &primary,
                                     const nlohmann::json &secondary,
                                     const std::vector<std::vector<std::string>> &paths)
            {
                for (const nlohmann::json *root : {&primary, &secondary})
                {
                    for (const std::vector<std::string> &path : paths)
                    {
                        const nlohmann::json *value = at_path(*root, path);
                        if (value == nullptr)
                            continue;
                        const std::string text = json_string(*value);
                        if (!text.empty())
                            return text;
                    }
                }
                return "";
            }

            void apply_output_section(OutputSettings &settings, const nlohmann::json &section)
            {
                if (section.is_boolean())
                {
                    settings.enabled = section.get<bool>();
                    return;
                }
                if (!section.is_object())
                    return;

                if (section.contains("enabled"))
                    settings.enabled = json_bool(section["enabled"], settings.enabled);
                if (section.contains("save_minimal"))
                    settings.save_minimal = json_bool(section["save_minimal"], settings.save_minimal);
                if (section.contains("save_editorial"))
                    settings.save_editorial = json_bool(section["save_editorial"], settings.save_editorial);
                if (section.contains("save_presentation"))
                    settings.save_editorial = json_bool(section["save_presentation"], settings.save_editorial);
                if (section.contains("save_legacy_alias"))
                    settings.save_legacy_alias = json_bool(section["save_legacy_alias"], settings.save_legacy_alias);
                if (section.contains("online_board"))
                {
                    const nlohmann::json &board = section["online_board"];
                    if (board.is_boolean())
                        settings.prepare_online_board = board.get<bool>();
                    else if (board.is_object())
                    {
                        if (board.contains("enabled"))
                            settings.prepare_online_board = json_bool(board["enabled"], settings.prepare_online_board);
                        if (board.contains("directory"))
                            settings.online_board_directory = json_string(board["directory"], settings.online_board_directory);
                        if (board.contains("max_width") && board["max_width"].is_number_integer())
                            settings.online_board_max_width = std::max(320, board["max_width"].get<int>());
                        if (board.contains("max_height") && board["max_height"].is_number_integer())
                            settings.online_board_max_height = std::max(240, board["max_height"].get<int>());
                    }
                }
                if (section.contains("north_up"))
                    settings.north_up = json_bool(section["north_up"], settings.north_up);
                if (section.contains("orientation_mode"))
                    settings.orientation_mode = lowercase_ascii(json_string(section["orientation_mode"], settings.orientation_mode));

                if (section.contains("minimal"))
                {
                    const nlohmann::json &minimal = section["minimal"];
                    settings.save_minimal = minimal.is_object() && minimal.contains("enabled") ? json_bool(minimal["enabled"], settings.save_minimal) : json_bool(minimal, settings.save_minimal);
                }
                if (section.contains("editorial"))
                {
                    const nlohmann::json &editorial = section["editorial"];
                    settings.save_editorial = editorial.is_object() && editorial.contains("enabled") ? json_bool(editorial["enabled"], settings.save_editorial) : json_bool(editorial, settings.save_editorial);
                }

                if (section.contains("outputs"))
                {
                    const nlohmann::json &outputs = section["outputs"];
                    if (outputs.is_object())
                    {
                        if (outputs.contains("minimal"))
                            settings.save_minimal = json_bool(outputs["minimal"], settings.save_minimal);
                        if (outputs.contains("editorial"))
                            settings.save_editorial = json_bool(outputs["editorial"], settings.save_editorial);
                        if (outputs.contains("presentation"))
                            settings.save_editorial = json_bool(outputs["presentation"], settings.save_editorial);
                        if (outputs.contains("legacy_alias"))
                            settings.save_legacy_alias = json_bool(outputs["legacy_alias"], settings.save_legacy_alias);
                    }
                    else if (outputs.is_array())
                    {
                        settings.save_minimal = false;
                        settings.save_editorial = false;
                        settings.save_legacy_alias = false;
                        for (const nlohmann::json &entry : outputs)
                        {
                            const std::string name = lowercase_ascii(json_string(entry));
                            if (name == "minimal" || name == "compact")
                                settings.save_minimal = true;
                            else if (name == "editorial" || name == "presentation" || name == "presentational")
                                settings.save_editorial = true;
                            else if (name == "legacy" || name == "annotated")
                                settings.save_legacy_alias = true;
                        }
                    }
                }

                if (section.contains("orientation"))
                {
                    const nlohmann::json &orientation = section["orientation"];
                    if (orientation.is_string())
                        settings.orientation_mode = lowercase_ascii(orientation.get<std::string>());
                    else if (orientation.is_object())
                    {
                        if (orientation.contains("mode"))
                            settings.orientation_mode = lowercase_ascii(json_string(orientation["mode"], settings.orientation_mode));
                        if (orientation.contains("north_up"))
                            settings.north_up = json_bool(orientation["north_up"], settings.north_up);
                    }
                }
            }

            double median(std::vector<double> values)
            {
                if (values.empty())
                    return 0.0;
                std::sort(values.begin(), values.end());
                const size_t middle = values.size() / 2;
                if (values.size() % 2 == 0)
                    return (values[middle - 1] + values[middle]) / 2.0;
                return values[middle];
            }

            double longitude_delta(double left, double right)
            {
                double delta = std::fmod(right - left + 540.0, 360.0) - 180.0;
                return delta == -180.0 ? 180.0 : delta;
            }

            RasterTransform combined_transform(bool flip_vertical, bool flip_horizontal)
            {
                if (flip_vertical && flip_horizontal)
                    return RasterTransform::Rotate180;
                if (flip_vertical)
                    return RasterTransform::FlipVertical;
                if (flip_horizontal)
                    return RasterTransform::FlipHorizontal;
                return RasterTransform::None;
            }

            bool infer_standard_projection(const image::Image &source, OrientationInfo &info)
            {
                if (!image::has_metadata_proj_cfg(const_cast<image::Image &>(source)) || source.width() < 2 || source.height() < 2)
                    return false;

                try
                {
                    const nlohmann::json config = image::get_metadata_proj_cfg(source);
                    if (!config.is_object() || !config.contains("type"))
                        return false;

                    proj::projection_t projection;
                    projection = config;
                    if (proj::projection_setup(&projection))
                        return false;

                    std::vector<double> top_latitudes;
                    std::vector<double> bottom_latitudes;
                    std::vector<double> left_longitudes;
                    std::vector<double> right_longitudes;
                    const double top_y = std::max(0.0, (double)(source.height() - 1) * 0.05);
                    const double bottom_y = std::max(0.0, (double)(source.height() - 1) * 0.95);
                    for (double fraction : {0.20, 0.50, 0.80})
                    {
                        const double x = (double)(source.width() - 1) * fraction;
                        double lon = 0.0;
                        double lat = 0.0;
                        if (!proj::projection_perform_inv(&projection, x, top_y, &lon, &lat) && std::isfinite(lat))
                            top_latitudes.push_back(lat);
                        if (!proj::projection_perform_inv(&projection, x, bottom_y, &lon, &lat) && std::isfinite(lat))
                            bottom_latitudes.push_back(lat);
                    }
                    const double left_x = std::max(0.0, (double)(source.width() - 1) * 0.05);
                    const double right_x = std::max(0.0, (double)(source.width() - 1) * 0.95);
                    for (double fraction : {0.20, 0.50, 0.80})
                    {
                        const double y = (double)(source.height() - 1) * fraction;
                        double lon = 0.0;
                        double lat = 0.0;
                        if (!proj::projection_perform_inv(&projection, left_x, y, &lon, &lat) && std::isfinite(lon))
                            left_longitudes.push_back(lon);
                        if (!proj::projection_perform_inv(&projection, right_x, y, &lon, &lat) && std::isfinite(lon))
                            right_longitudes.push_back(lon);
                    }
                    proj::projection_free(&projection);

                    if (top_latitudes.empty() || bottom_latitudes.empty())
                        return false;
                    info.top_latitude = median(top_latitudes);
                    info.bottom_latitude = median(bottom_latitudes);
                    info.latitudes_valid = std::fabs(info.top_latitude - info.bottom_latitude) > 0.01;
                    if (!left_longitudes.empty() && !right_longitudes.empty())
                    {
                        info.left_longitude = median(left_longitudes);
                        info.right_longitude = median(right_longitudes);
                        info.longitudes_valid = std::fabs(longitude_delta(info.left_longitude, info.right_longitude)) > 0.01;
                    }
                    info.inferred_from_projection = info.latitudes_valid;
                    return info.latitudes_valid;
                }
                catch (const std::exception &)
                {
                    return false;
                }
            }

            bool infer_gcps(const image::Image &source,
                            ImageProducts &products,
                            const std::vector<double> &timestamps,
                            const nlohmann::json &product_metadata,
                            OrientationInfo &info)
            {
                if (!products.has_proj_cfg() || source.width() < 2 || source.height() < 2)
                    return false;

                try
                {
                    nlohmann::ordered_json config = products.get_proj_cfg();
                    config["metadata"] = product_metadata;
                    if (products.has_tle())
                        config["metadata"]["tle"] = products.get_tle();
                    if (!timestamps.empty())
                        config["metadata"]["timestamps"] = timestamps;

                    const std::vector<satdump::projection::GCP> gcps = satdump::gcp_compute::compute_gcps(config, source.width(), source.height());
                    if (gcps.size() < 4)
                        return false;

                    double minimum_y = gcps.front().y;
                    double maximum_y = gcps.front().y;
                    double minimum_x = gcps.front().x;
                    double maximum_x = gcps.front().x;
                    for (const satdump::projection::GCP &gcp : gcps)
                    {
                        minimum_y = std::min(minimum_y, gcp.y);
                        maximum_y = std::max(maximum_y, gcp.y);
                        minimum_x = std::min(minimum_x, gcp.x);
                        maximum_x = std::max(maximum_x, gcp.x);
                    }
                    const double span = maximum_y - minimum_y;
                    if (!std::isfinite(span) || span <= 0.0)
                        return false;

                    std::vector<double> top_latitudes;
                    std::vector<double> bottom_latitudes;
                    std::vector<double> left_longitudes;
                    std::vector<double> right_longitudes;
                    const double top_limit = minimum_y + span * 0.22;
                    const double bottom_limit = maximum_y - span * 0.22;
                    const double x_span = maximum_x - minimum_x;
                    const double left_limit = minimum_x + x_span * 0.22;
                    const double right_limit = maximum_x - x_span * 0.22;
                    for (const satdump::projection::GCP &gcp : gcps)
                    {
                        if (!std::isfinite(gcp.lat))
                            continue;
                        if (gcp.y <= top_limit)
                            top_latitudes.push_back(gcp.lat);
                        if (gcp.y >= bottom_limit)
                            bottom_latitudes.push_back(gcp.lat);
                        if (std::isfinite(gcp.lon) && gcp.x <= left_limit)
                            left_longitudes.push_back(gcp.lon);
                        if (std::isfinite(gcp.lon) && gcp.x >= right_limit)
                            right_longitudes.push_back(gcp.lon);
                    }
                    if (top_latitudes.empty() || bottom_latitudes.empty())
                        return false;

                    info.top_latitude = median(top_latitudes);
                    info.bottom_latitude = median(bottom_latitudes);
                    info.latitudes_valid = std::fabs(info.top_latitude - info.bottom_latitude) > 0.05;
                    if (!left_longitudes.empty() && !right_longitudes.empty())
                    {
                        info.left_longitude = median(left_longitudes);
                        info.right_longitude = median(right_longitudes);
                        info.longitudes_valid = std::fabs(longitude_delta(info.left_longitude, info.right_longitude)) > 0.05;
                    }
                    info.inferred_from_gcps = info.latitudes_valid;
                    return info.latitudes_valid;
                }
                catch (const std::exception &error)
                {
                    logger->debug("Presentation orientation: GCP inference failed: %s", error.what());
                    return false;
                }
            }

            std::string normalized_direction(const std::string &raw)
            {
                const std::string value = lowercase_ascii(raw);
                if (value.find("ascending") != std::string::npos || value == "asc" ||
                    value.find("northbound") != std::string::npos || value.find("восход") != std::string::npos || raw.find("Восход") != std::string::npos)
                    return "ascending";
                if (value.find("descending") != std::string::npos || value == "desc" ||
                    value.find("southbound") != std::string::npos || value.find("нисход") != std::string::npos || raw.find("Нисход") != std::string::npos)
                    return "descending";
                return value;
            }

            std::string localized_direction(const std::string &normalized)
            {
                if (normalized == "ascending")
                    return "восходящий пролёт";
                if (normalized == "descending")
                    return "нисходящий пролёт";
                return normalized;
            }

            OrientationInfo determine_orientation_impl(const image::Image &source,
                                                       ImageProducts &products,
                                                       const std::vector<double> &timestamps,
                                                       const nlohmann::json &product_metadata,
                                                       const std::string &source_variant,
                                                       const OutputSettings &settings)
            {
                OrientationInfo info;
                info.frame = image::presentation::classify_frame(source);
                info.north_up_requested = settings.north_up;

                const std::string raw_direction = first_string(products.contents, product_metadata,
                                                               {{"acquisition", "pass", "direction"},
                                                                {"pass", "direction"},
                                                                {"pass_direction"},
                                                                {"direction"}});
                info.pass_direction = normalized_direction(raw_direction);

                const std::string mode = lowercase_ascii(settings.orientation_mode);
                if (mode == "keep" || mode == "none" || mode == "source")
                {
                    info.transform = RasterTransform::None;
                    info.description = "исходная ориентация";
                    return info;
                }
                if (mode == "flip_vertical" || mode == "vertical")
                {
                    info.transform = RasterTransform::FlipVertical;
                    info.description = "вертикальное отражение по настройке";
                    return info;
                }
                if (mode == "flip_horizontal" || mode == "horizontal")
                {
                    info.transform = RasterTransform::FlipHorizontal;
                    info.description = "горизонтальное отражение по настройке";
                    return info;
                }
                if (mode == "rotate_180" || mode == "180")
                {
                    info.transform = RasterTransform::Rotate180;
                    info.description = "поворот на 180° по настройке";
                    return info;
                }

                if (!settings.north_up)
                {
                    info.transform = RasterTransform::None;
                    info.description = "автокоррекция север-сверху отключена";
                    return info;
                }

                const std::string variant = lowercase_ascii(source_variant);
                const bool projected_variant = variant.find("проекц") != std::string::npos || variant.find("projection") != std::string::npos;

                // Prefer the projection metadata attached to the actual raster. This
                // also detects a manually configured target projection with a positive
                // Y scalar (south at the top) instead of blindly trusting the preset.
                bool inferred = infer_standard_projection(source, info);
                if (!inferred && projected_variant)
                {
                    // Polar/oblique projections can have no globally monotonic
                    // top-to-bottom latitude. SatDump still renders their configured
                    // north direction consistently, so preserve the projection result.
                    info.transform = RasterTransform::None;
                    info.north_up_verified = true;
                    info.inferred_from_projection = true;
                    info.description = "ориентация сохранена · задана географической проекцией";
                    return info;
                }
                if (!inferred)
                    inferred = infer_gcps(source, products, timestamps, product_metadata, info);

                if (inferred)
                {
                    const bool flip_vertical = info.top_latitude < info.bottom_latitude;
                    const bool flip_horizontal = info.longitudes_valid &&
                                                 longitude_delta(info.left_longitude, info.right_longitude) < 0.0;
                    info.transform = combined_transform(flip_vertical, flip_horizontal);
                    if (flip_vertical && flip_horizontal)
                    {
                        info.description = "север сверху, восток справа · выполнен поворот на 180°";
                        if (info.pass_direction.empty())
                            info.pass_direction = "ascending";
                    }
                    else if (flip_vertical)
                    {
                        info.description = "север сверху · выполнено вертикальное отражение";
                    }
                    else if (flip_horizontal)
                        info.description = "восток справа · выполнено горизонтальное отражение";
                    else
                        info.description = "север сверху, восток справа · исходная ориентация корректна";
                    if (info.pass_direction.empty())
                        info.pass_direction = flip_vertical ? "ascending" : "descending";
                    info.north_up_verified = true;
                    return info;
                }

                if (info.pass_direction == "ascending")
                {
                    info.transform = RasterTransform::FlipVertical;
                    info.inferred_from_pass_direction = true;
                    info.north_up_verified = true;
                    info.description = "север сверху · ориентация определена по направлению пролёта";
                }
                else if (info.pass_direction == "descending")
                {
                    info.transform = RasterTransform::None;
                    info.inferred_from_pass_direction = true;
                    info.north_up_verified = true;
                    info.description = "север сверху · исходная ориентация подтверждена направлением пролёта";
                }
                else
                {
                    info.transform = RasterTransform::None;
                    info.description = "ориентация сохранена · недостаточно геоданных для проверки";
                }
                return info;
            }

            void append_detail(image::presentation::PresentationSpec &spec, const std::string &label, const std::string &value)
            {
                if (value.empty())
                    return;
                for (image::presentation::MetadataField &field : spec.pass.details)
                {
                    if (field.label == label)
                    {
                        field.value = value;
                        return;
                    }
                }
                spec.pass.details.push_back({label, value});
            }

            std::string legend_kind_name(image::presentation::LegendKind kind)
            {
                if (kind == image::presentation::LegendKind::Continuous)
                    return "continuous";
                if (kind == image::presentation::LegendKind::Categorical)
                    return "categorical";
                if (kind == image::presentation::LegendKind::Composite)
                    return "composite";
                return "none";
            }

            std::pair<double, double> board_timestamp_range(const std::vector<double> &timestamps,
                                                            ImageProducts &products)
            {
                const double anchor = products.has_product_timestamp()
                                          ? (double)products.get_product_timestamp()
                                          : NAN;
                constexpr double maximum_distance = 6.0 * 60.0 * 60.0;
                std::vector<double> valid;
                for (double timestamp : timestamps)
                    if (std::isfinite(timestamp) && timestamp > 0.0 &&
                        (!std::isfinite(anchor) || std::fabs(timestamp - anchor) <= maximum_distance))
                        valid.push_back(timestamp);

                if (!std::isfinite(anchor) && !valid.empty())
                {
                    std::sort(valid.begin(), valid.end());
                    size_t best_begin = 0;
                    size_t best_end = 0;
                    size_t begin = 0;
                    for (size_t end = 0; end < valid.size(); end++)
                    {
                        while (valid[end] - valid[begin] > maximum_distance)
                            begin++;
                        if (end - begin > best_end - best_begin)
                        {
                            best_begin = begin;
                            best_end = end;
                        }
                    }
                    valid = std::vector<double>(valid.begin() + best_begin,
                                                valid.begin() + best_end + 1);
                }

                if (valid.empty())
                {
                    if (std::isfinite(anchor))
                        return {anchor, anchor};
                    return {(double)NAN, (double)NAN};
                }
                const auto bounds = std::minmax_element(valid.begin(), valid.end());
                return {*bounds.first, *bounds.second};
            }

            std::string iso_utc(double timestamp)
            {
                if (!std::isfinite(timestamp) || timestamp <= 0.0)
                    return "";
                const time_t value = (time_t)std::llround(timestamp);
                std::tm utc{};
#ifdef _WIN32
                gmtime_s(&utc, &value);
#else
                gmtime_r(&value, &utc);
#endif
                std::ostringstream stream;
                stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
                return stream.str();
            }

            std::string utc_now()
            {
                return iso_utc((double)std::chrono::system_clock::to_time_t(
                    std::chrono::system_clock::now()));
            }

            std::string slug(std::string value)
            {
                std::string result;
                bool separator = false;
                for (unsigned char character : value)
                {
                    if (std::isalnum(character))
                    {
                        result.push_back((char)std::tolower(character));
                        separator = false;
                    }
                    else if (!result.empty() && !separator)
                    {
                        result.push_back('-');
                        separator = true;
                    }
                }
                while (!result.empty() && result.back() == '-')
                    result.pop_back();
                return result.empty() ? "frame" : result;
            }

            std::string short_hash(const std::string &value)
            {
                uint32_t hash = 2166136261u;
                for (unsigned char character : value)
                {
                    hash ^= character;
                    hash *= 16777619u;
                }
                std::ostringstream stream;
                stream << std::hex << std::setfill('0') << std::setw(8) << hash;
                return stream.str();
            }

            const std::string &generation_id()
            {
                static const std::string value = []()
                {
                    const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count();
                    return slug(utc_now()) + "_" + short_hash(std::to_string(ticks));
                }();
                return value;
            }

            image::Image display_derivative(const image::Image &source, int max_width, int max_height)
            {
                image::Image result = source;
                result = result.to8bits();
                result.to_rgb();
                const double scale = std::min(1.0, std::min(
                    (double)max_width / std::max<size_t>(1, result.width()),
                    (double)max_height / std::max<size_t>(1, result.height())));
                if (scale < 1.0)
                    result.resize_bilinear(
                        std::max(1, (int)std::floor(result.width() * scale)),
                        std::max(1, (int)std::floor(result.height() * scale)));
                return result;
            }

            std::string product_family(const std::string &raw_name,
                                       const image::presentation::PresentationSpec &spec)
            {
                const std::string value = lowercase_ascii(raw_name + " " + spec.pass.product);
                if (value.find("cloudtop") != std::string::npos ||
                    value.find("brightness temperature") != std::string::npos ||
                    value.find("температур") != std::string::npos)
                    return "thermal-ir";
                if (value.find("mcir") != std::string::npos ||
                    value.find("cloud composite") != std::string::npos ||
                    value.find("облачност") != std::string::npos)
                    return "cloud-composite";
                if (value.find("msa") != std::string::npos ||
                    value.find("visible") != std::string::npos ||
                    value.find("видим") != std::string::npos)
                    return "visible";
                if (spec.legend.kind == image::presentation::LegendKind::Continuous)
                    return "quantitative";
                return "other";
            }

            std::string board_product_title(const std::string &raw_name,
                                            const image::presentation::PresentationSpec &spec)
            {
                if (!spec.pass.product.empty() && lowercase_ascii(spec.pass.product) != lowercase_ascii(raw_name))
                    return spec.pass.product;
                const std::string value = lowercase_ascii(raw_name);
                if (value.find("cloudtop") != std::string::npos || value.find("cloud top") != std::string::npos)
                    return "Температура верхней границы облаков";
                if (value.find("mcir") != std::string::npos)
                    return "Облачность в инфракрасном диапазоне";
                if (value.find("msa") != std::string::npos)
                    return "Облачность и поверхность в видимом диапазоне";
                return raw_name.empty() ? "Спутниковый метеорологический продукт" : raw_name;
            }

            std::string board_product_description(const std::string &raw_name,
                                                  const image::presentation::PresentationSpec &spec)
            {
                const std::string value = lowercase_ascii(raw_name);
                if (value.find("cloudtop") != std::string::npos || value.find("cloud top") != std::string::npos)
                    return "Яркостная температура излучающей облачной поверхности";
                if (value.find("mcir") != std::string::npos)
                    return "Инфракрасное выделение облачности на картографической подложке";
                if (value.find("msa") != std::string::npos)
                    return "Облака и поверхность в отражённом солнечном свете";
                return spec.legend.subtitle;
            }

            int product_priority(const std::string &raw_name, const std::string &family)
            {
                const std::string value = lowercase_ascii(raw_name);
                if (value.find("cloudtop") != std::string::npos || family == "thermal-ir")
                    return 10;
                if (value.find("mcir") != std::string::npos || family == "cloud-composite")
                    return 20;
                if (value.find("msa") != std::string::npos || family == "visible")
                    return 30;
                return 100;
            }

            void write_json(const std::filesystem::path &path, const nlohmann::json &value)
            {
                std::ofstream output(path.string());
                if (!output)
                    throw std::runtime_error("не удалось открыть " + path.string());
                output << value.dump(4) << "\n";
                if (!output)
                    throw std::runtime_error("не удалось записать " + path.string());
            }

            void rebuild_pass_manifest(const std::filesystem::path &board_root,
                                       const nlohmann::json &event,
                                       const std::string &current_generation)
            {
                nlohmann::json manifest = {
                    {"schema", "satdump.meteoboard-pass/1"},
                    {"generatedAt", utc_now()},
                    {"event", event},
                    {"frames", nlohmann::json::array()}};
                const std::filesystem::path frames_root = board_root / "frames";
                if (std::filesystem::exists(frames_root))
                {
                    for (const auto &entry : std::filesystem::directory_iterator(frames_root))
                    {
                        const std::filesystem::path metadata_path = entry.path() / "metadata.json";
                        if (!entry.is_directory() || !std::filesystem::exists(metadata_path))
                            continue;
                        try
                        {
                            std::ifstream input(metadata_path.string());
                            nlohmann::json frame;
                            input >> frame;
                            if (frame.is_object() &&
                                frame.value("eventId", "") == event.value("id", "") &&
                                frame.value("generationId", "") == current_generation)
                                manifest["frames"].push_back(frame);
                        }
                        catch (const std::exception &error)
                        {
                            logger->warn("Online-board: пропущен повреждённый паспорт %s: %s",
                                         metadata_path.string().c_str(), error.what());
                        }
                    }
                }
                std::string first_start;
                std::string last_end;
                std::set<std::string> instruments;
                for (const nlohmann::json &frame : manifest["frames"])
                {
                    if (frame.contains("start") && frame["start"].is_string())
                    {
                        const std::string value = frame["start"].get<std::string>();
                        if (first_start.empty() || value < first_start)
                            first_start = value;
                    }
                    if (frame.contains("end") && frame["end"].is_string())
                    {
                        const std::string value = frame["end"].get<std::string>();
                        if (last_end.empty() || value > last_end)
                            last_end = value;
                    }
                    const std::string instrument = frame.value("instrument", "");
                    if (!instrument.empty())
                        instruments.insert(instrument);
                }
                manifest["event"]["start"] = first_start.empty() ? nlohmann::json(nullptr) : nlohmann::json(first_start);
                manifest["event"]["end"] = last_end.empty() ? nlohmann::json(nullptr) : nlohmann::json(last_end);
                manifest["event"]["instruments"] = instruments;
                std::sort(manifest["frames"].begin(), manifest["frames"].end(),
                          [](const nlohmann::json &left, const nlohmann::json &right)
                          {
                              const int left_priority = left.value("priority", 100);
                              const int right_priority = right.value("priority", 100);
                              if (left_priority != right_priority)
                                  return left_priority < right_priority;
                              return left.value("id", "") < right.value("id", "");
                          });
                const std::filesystem::path temporary = board_root / "manifest.json.tmp";
                const std::filesystem::path destination = board_root / "manifest.json";
                write_json(temporary, manifest);
                std::error_code error;
                std::filesystem::rename(temporary, destination, error);
                if (error)
                {
                    std::filesystem::remove(destination, error);
                    error.clear();
                    std::filesystem::rename(temporary, destination, error);
                }
                if (error)
                    throw std::runtime_error("не удалось опубликовать manifest.json: " + error.message());
            }

            image::Image ambient_derivative(const image::Image &source)
            {
                image::Image result = source;
                const double source_ratio = (double)result.width() / std::max<size_t>(1, result.height());
                constexpr double target_ratio = 16.0 / 9.0;
                if (source_ratio > target_ratio)
                {
                    const int width = std::max(1, (int)std::round(result.height() * target_ratio));
                    const int left = std::max(0, ((int)result.width() - width) / 2);
                    result.crop(left, 0, left + width, result.height());
                }
                else if (source_ratio < target_ratio)
                {
                    const int height = std::max(1, (int)std::round(result.width() / target_ratio));
                    const int top = std::max(0, ((int)result.height() - height) / 2);
                    result.crop(0, top, result.width(), top + height);
                }
                result.resize_bilinear(80, 45);
                result.resize_bilinear(640, 360);
                for (size_t pixel = 0; pixel < result.width() * result.height(); pixel++)
                {
                    const double r = result.getf(0, pixel);
                    const double g = result.getf(1, pixel);
                    const double b = result.getf(2, pixel);
                    const double gray = r * 0.2126 + g * 0.7152 + b * 0.0722;
                    for (int channel = 0; channel < 3; channel++)
                    {
                        const double original = channel == 0 ? r : (channel == 1 ? g : b);
                        const double desaturated = gray * 0.72 + original * 0.28;
                        result.setf(channel, pixel, std::max(0.0, std::min(1.0, 0.42 + (desaturated - 0.5) * 0.42)));
                    }
                }
                return result;
            }

            image::Image legend_derivative(const image::presentation::LegendSpec &legend)
            {
                const int width = 1200;
                const int height = 72;
                image::Image result(8, width, height, 3);
                result.fill(0);
                if (legend.kind == image::presentation::LegendKind::Continuous && !legend.color_stops.empty())
                {
                    std::vector<image::presentation::ColorStop> stops = legend.color_stops;
                    std::sort(stops.begin(), stops.end(), [](const auto &left, const auto &right)
                              { return left.position < right.position; });
                    for (int x = 0; x < width; x++)
                    {
                        const double position = (double)x / (double)(width - 1);
                        size_t right = 0;
                        while (right + 1 < stops.size() && stops[right + 1].position < position)
                            right++;
                        const size_t next = std::min(right + 1, stops.size() - 1);
                        const double span = stops[next].position - stops[right].position;
                        const double mix = span > 0.0 ? std::max(0.0, std::min(1.0, (position - stops[right].position) / span)) : 0.0;
                        for (int channel = 0; channel < 3; channel++)
                        {
                            const double left_value = channel < (int)stops[right].color.size() ? stops[right].color[channel] : 0.0;
                            const double right_value = channel < (int)stops[next].color.size() ? stops[next].color[channel] : left_value;
                            for (int y = 0; y < height; y++)
                                result.setf(channel, x, y, left_value + (right_value - left_value) * mix);
                        }
                    }
                    return result;
                }
                if (legend.kind == image::presentation::LegendKind::Categorical && !legend.categories.empty())
                {
                    for (size_t index = 0; index < legend.categories.size(); index++)
                    {
                        const int begin = (int)(index * width / legend.categories.size());
                        const int end = (int)((index + 1) * width / legend.categories.size());
                        for (int channel = 0; channel < 3; channel++)
                        {
                            const double value = channel < (int)legend.categories[index].color.size()
                                                     ? legend.categories[index].color[channel]
                                                     : 0.0;
                            for (int y = 0; y < height; y++)
                                for (int x = begin; x < end; x++)
                                    result.setf(channel, x, y, value);
                        }
                    }
                }
                return result;
            }

            nlohmann::json make_sidecar(const image::presentation::PresentationSpec &spec,
                                        const OrientationInfo &orientation,
                                        const image::Image &source,
                                        const image::Image &output,
                                        const std::string &layout)
            {
                nlohmann::json sidecar;
                sidecar["schema"] = "satdump.presentation/2";
                sidecar["layout"] = layout;
                sidecar["source"] = {
                    {"width", source.width()},
                    {"height", source.height()},
                    {"frame", image::presentation::frame_kind_name(orientation.frame)}};
                sidecar["output"] = {{"width", output.width()}, {"height", output.height()}};
                sidecar["orientation"] = {
                    {"north_up_requested", orientation.north_up_requested},
                    {"north_up_verified", orientation.north_up_verified},
                    {"transform", image::presentation::raster_transform_name(orientation.transform)},
                    {"description", orientation.description},
                    {"pass_direction", orientation.pass_direction},
                    {"inferred_from_projection", orientation.inferred_from_projection},
                    {"inferred_from_gcps", orientation.inferred_from_gcps},
                    {"inferred_from_pass_direction", orientation.inferred_from_pass_direction}};
                if (orientation.latitudes_valid)
                {
                    sidecar["orientation"]["top_latitude"] = orientation.top_latitude;
                    sidecar["orientation"]["bottom_latitude"] = orientation.bottom_latitude;
                }
                if (orientation.longitudes_valid)
                {
                    sidecar["orientation"]["left_longitude"] = orientation.left_longitude;
                    sidecar["orientation"]["right_longitude"] = orientation.right_longitude;
                }

                sidecar["pass"] = {
                    {"satellite", spec.pass.satellite},
                    {"instrument", spec.pass.instrument},
                    {"product", spec.pass.product},
                    {"acquisition_time", spec.pass.acquisition_time},
                    {"summary", spec.pass.pass_summary},
                    {"quality", spec.pass.quality},
                    {"quality_detail", spec.pass.quality_detail}};
                for (const image::presentation::MetadataField &field : spec.pass.details)
                    sidecar["pass"]["details"].push_back({{"label", field.label}, {"value", field.value}});

                sidecar["legend"] = {
                    {"kind", legend_kind_name(spec.legend.kind)},
                    {"title", spec.legend.title},
                    {"subtitle", spec.legend.subtitle},
                    {"unit", spec.legend.unit},
                    {"notes", spec.legend.notes}};
                for (const image::presentation::ColorStop &stop : spec.legend.color_stops)
                    sidecar["legend"]["color_stops"].push_back({{"position", stop.position}, {"color", stop.color}});
                for (const image::presentation::LegendTick &tick : spec.legend.ticks)
                    sidecar["legend"]["ticks"].push_back({{"position", tick.position}, {"label", tick.label}});
                for (const image::presentation::CategoryEntry &category : spec.legend.categories)
                    sidecar["legend"]["categories"].push_back({{"color", category.color}, {"label", category.label}});
                for (const image::presentation::CompositeComponent &component : spec.legend.components)
                    sidecar["legend"]["components"].push_back({
                        {"component", component.component},
                        {"channel", component.channel},
                        {"spectral_range", component.spectral_range},
                        {"quantity", component.quantity},
                        {"formula", component.formula},
                        {"description", component.description}});
                sidecar["branding"] = spec.branding;
                return sidecar;
            }

            bool save_online_board_package(const image::Image &oriented,
                                           ImageProducts &products,
                                           const image::presentation::PresentationSpec &spec,
                                           const OrientationInfo &orientation,
                                           const std::vector<double> &timestamps,
                                           const std::string &raw_product_name,
                                           const std::string &source_variant,
                                           const std::string &base_path,
                                           const OutputSettings &settings)
            {
                if (!settings.prepare_online_board || oriented.size() == 0)
                    return false;
                try
                {
                    const std::pair<double, double> range = board_timestamp_range(timestamps, products);
                    const std::string start = iso_utc(range.first);
                    const std::string end = iso_utc(range.second);
                    const std::filesystem::path product_file(base_path);
                    std::filesystem::path pass_root = product_file.parent_path();
                    if (pass_root.has_parent_path())
                        pass_root = pass_root.parent_path();
                    const std::string event_key = spec.pass.satellite + "|" + pass_root.filename().string();
                    const std::string event_id = slug(pass_root.filename().string()) + "_" + short_hash(event_key);
                    const std::string frame_key = event_key + "|" + products.instrument_name + "|" + raw_product_name;
                    const std::string frame_id = event_id + "_" + slug(raw_product_name) + "_" + short_hash(frame_key);
                    std::filesystem::path directory_name(settings.online_board_directory);
                    directory_name = directory_name.filename();
                    if (directory_name.empty() || directory_name == "." || directory_name == "..")
                        directory_name = "online-board";
                    const std::filesystem::path board_root = pass_root / directory_name;
                    const std::filesystem::path frame_root = board_root / "frames" / frame_id;
                    std::filesystem::create_directories(frame_root);

                    image::Image display = display_derivative(
                        oriented, settings.online_board_max_width, settings.online_board_max_height);
                    image::save_img(display, (frame_root / "imagery.png").string());
                    image::Image ambient = ambient_derivative(display);
                    image::save_img(ambient, (frame_root / "ambient.jpg").string());

                    const bool legend_required =
                        (spec.legend.kind == image::presentation::LegendKind::Continuous && !spec.legend.color_stops.empty()) ||
                        (spec.legend.kind == image::presentation::LegendKind::Categorical && !spec.legend.categories.empty());
                    if (legend_required)
                    {
                        image::Image legend = legend_derivative(spec.legend);
                        image::save_img(legend, (frame_root / "legend.png").string());
                    }

                    const std::string family = product_family(raw_product_name, spec);
                    const std::string product_title = board_product_title(raw_product_name, spec);
                    const std::string product_description = board_product_description(raw_product_name, spec);
                    const double ratio = (double)display.width() / std::max<size_t>(1, display.height());
                    nlohmann::json event = {
                        {"id", event_id}, {"satellite", spec.pass.satellite}, {"instrument", spec.pass.instrument},
                        {"start", start.empty() ? nlohmann::json(nullptr) : nlohmann::json(start)},
                        {"end", end.empty() ? nlohmann::json(nullptr) : nlohmann::json(end)},
                        {"passDirection", orientation.pass_direction.empty() ? nlohmann::json(nullptr) : nlohmann::json(orientation.pass_direction)},
                        {"passDirectionTitle", localized_direction(orientation.pass_direction)}};

                    nlohmann::json metadata = {
                        {"schema", "satdump.meteoboard-frame/1"}, {"id", frame_id}, {"eventId", event_id},
                        {"generationId", generation_id()}, {"satellite", spec.pass.satellite}, {"instrument", spec.pass.instrument},
                        {"product", {{"code", raw_product_name}, {"title", product_title}, {"family", family}, {"description", product_description}}},
                        {"start", start.empty() ? nlohmann::json(nullptr) : nlohmann::json(start)},
                        {"end", end.empty() ? nlohmann::json(nullptr) : nlohmann::json(end)},
                        {"acquisitionTitle", spec.pass.acquisition_time},
                        {"pass", {{"direction", orientation.pass_direction.empty() ? nlohmann::json(nullptr) : nlohmann::json(orientation.pass_direction)},
                                  {"directionTitle", localized_direction(orientation.pass_direction)}, {"summary", spec.pass.pass_summary}}},
                        {"image", {{"width", display.width()}, {"height", display.height()}, {"aspectRatio", ratio},
                                   {"composition", ratio >= 1.1 ? "landscape" : "portrait"},
                                   {"maxWidth", settings.online_board_max_width}, {"maxHeight", settings.online_board_max_height},
                                   {"scientificColorsPreserved", true}, {"crop", false}}},
                        {"orientation", {{"northUpRequested", orientation.north_up_requested}, {"northUpVerified", orientation.north_up_verified},
                                         {"transform", image::presentation::raster_transform_name(orientation.transform)}, {"description", orientation.description}}},
                        {"legend", {{"required", legend_required}, {"kind", legend_kind_name(spec.legend.kind)}, {"title", spec.legend.title},
                                    {"subtitle", spec.legend.subtitle}, {"unit", spec.legend.unit}, {"notes", spec.legend.notes}}},
                        {"details", nlohmann::json::array()}, {"sourceVariant", source_variant},
                        {"priority", product_priority(raw_product_name, family)},
                        {"recommendedDisplaySeconds", (family == "thermal-ir" || spec.legend.kind == image::presentation::LegendKind::Continuous) ? 22 : 18},
                        {"urls", {{"imagery", "frames/" + frame_id + "/imagery.png"}, {"ambient", "frames/" + frame_id + "/ambient.jpg"},
                                  {"legend", legend_required ? nlohmann::json("frames/" + frame_id + "/legend.png") : nlohmann::json(nullptr)}}}};

                    for (const image::presentation::MetadataField &field : spec.pass.details)
                        metadata["details"].push_back({{"label", field.label}, {"value", field.value}});
                    for (const image::presentation::ColorStop &stop : spec.legend.color_stops)
                        metadata["legend"]["colorStops"].push_back({{"position", stop.position}, {"color", stop.color}});
                    for (const image::presentation::LegendTick &tick : spec.legend.ticks)
                        metadata["legend"]["ticks"].push_back({{"position", tick.position}, {"label", tick.label}});
                    for (const image::presentation::CategoryEntry &category : spec.legend.categories)
                        metadata["legend"]["categories"].push_back({{"color", category.color}, {"label", category.label}});
                    for (const image::presentation::CompositeComponent &component : spec.legend.components)
                        metadata["legend"]["components"].push_back({
                            {"component", component.component}, {"color", component.marker_color}, {"channel", component.channel},
                            {"spectralRange", component.spectral_range}, {"quantity", component.quantity},
                            {"formula", component.formula}, {"description", component.description}});

                    write_json(frame_root / "metadata.json", metadata);
                    rebuild_pass_manifest(board_root, event, generation_id());
                    logger->info("Prepared online-board data package %s", frame_root.string().c_str());
                    return true;
                }
                catch (const std::exception &error)
                {
                    logger->error("Could not prepare online-board data for %s: %s", base_path.c_str(), error.what());
                    return false;
                }
            }

            bool save_variant(const image::Image &source,
                              image::TextDrawer &text_drawer,
                              image::presentation::PresentationSpec spec,
                              LayoutKind layout,
                              const OrientationInfo &orientation,
                              const std::string &path,
                              image::Image *rendered_copy = nullptr)
            {
                try
                {
                    spec.branding += layout == LayoutKind::Minimal ? " · Minimal" : " · Presentation";
                    image::Image rendered = image::presentation::render_layout(source, text_drawer, spec, layout);
                    image::save_img(rendered, path);

                    std::filesystem::path sidecar_path(path);
                    sidecar_path.replace_extension(".json");
                    std::ofstream sidecar(sidecar_path.string());
                    sidecar << make_sidecar(spec,
                                             orientation,
                                             source,
                                             rendered,
                                             layout == LayoutKind::Minimal ? "minimal" : "editorial")
                                   .dump(4);
                    sidecar.close();
                    if (rendered_copy != nullptr)
                        *rendered_copy = rendered;
                    logger->info("Saved presentation product %s", path.c_str());
                    return true;
                }
                catch (const std::exception &error)
                {
                    logger->error("Could not save presentation product %s: %s", path.c_str(), error.what());
                    return false;
                }
            }

            bool save_legacy(const image::Image &rendered,
                             const image::presentation::PresentationSpec &spec,
                             const OrientationInfo &orientation,
                             const image::Image &source,
                             const std::string &path,
                             const std::string &layout)
            {
                if (rendered.size() == 0)
                    return false;
                try
                {
                    image::save_img(rendered, path);
                    std::filesystem::path sidecar_path(path);
                    sidecar_path.replace_extension(".json");
                    std::ofstream sidecar(sidecar_path.string());
                    sidecar << make_sidecar(spec, orientation, source, rendered, layout).dump(4);
                    sidecar.close();
                    return true;
                }
                catch (const std::exception &error)
                {
                    logger->error("Could not save compatibility presentation %s: %s", path.c_str(), error.what());
                    return false;
                }
            }
        }

        OrientationInfo analyze_orientation(const image::Image &source,
                                            ImageProducts &products,
                                            const std::vector<double> &timestamps,
                                            const nlohmann::json &product_metadata,
                                            const std::string &source_variant,
                                            const OutputSettings &settings)
        {
            return determine_orientation_impl(source, products, timestamps, product_metadata, source_variant, settings);
        }

        OutputSettings resolve_output_settings(const nlohmann::json &composite_preset)
        {
            OutputSettings settings;
            settings.enabled = enabled(composite_preset);

            try
            {
                if (config::main_cfg.contains("satdump_general"))
                {
                    const nlohmann::json &general = config::main_cfg["satdump_general"];
                    if (general.contains("presentation"))
                        apply_output_section(settings, general["presentation"]);
                    if (general.contains("presentation_enabled"))
                        settings.enabled = json_bool(general["presentation_enabled"], settings.enabled);
                }
            }
            catch (const std::exception &)
            {
            }

            if (composite_preset.is_object() && composite_preset.contains("presentation"))
                apply_output_section(settings, composite_preset["presentation"]);

            if (!settings.save_minimal && !settings.save_editorial &&
                !settings.save_legacy_alias && !settings.prepare_online_board)
                settings.enabled = false;
            return settings;
        }

        OutputResult save_outputs(const image::Image &source,
                                  image::TextDrawer &text_drawer,
                                  ImageProducts &products,
                                  const ImageCompositeCfg &composite,
                                  const nlohmann::json &composite_preset,
                                  const std::string &product_name,
                                  const std::vector<double> &timestamps,
                                  const nlohmann::json &product_metadata,
                                  const std::string &source_variant,
                                  const std::string &base_path,
                                  const std::function<void(image::Image &, RasterTransform)> &decorate_oriented)
        {
            OutputResult result;
            const OutputSettings settings = resolve_output_settings(composite_preset);
            if (!settings.enabled || source.size() == 0)
                return result;

            result.orientation = analyze_orientation(source, products, timestamps, product_metadata, source_variant, settings);
            image::Image oriented = image::presentation::apply_transform(source, result.orientation.transform);
            if (decorate_oriented)
                decorate_oriented(oriented, result.orientation.transform);

            image::presentation::PresentationSpec base_spec = build_spec(
                products,
                composite,
                composite_preset,
                product_name,
                timestamps,
                product_metadata,
                source_variant);

            append_detail(base_spec, "Кадр", image::presentation::frame_kind_name(result.orientation.frame));
            append_detail(base_spec, "Ориентация", result.orientation.description);
            const std::string direction = localized_direction(result.orientation.pass_direction);
            if (!direction.empty() && base_spec.pass.pass_summary.find(direction) == std::string::npos)
            {
                if (!base_spec.pass.pass_summary.empty())
                    base_spec.pass.pass_summary += " · ";
                base_spec.pass.pass_summary += direction;
            }

            result.online_board = save_online_board_package(
                oriented, products, base_spec, result.orientation, timestamps,
                product_name, source_variant, base_path, settings);

            image::Image editorial_rendered;
            image::Image minimal_rendered;
            if (settings.save_minimal && text_drawer.font_ready())
            {
                image::presentation::PresentationSpec spec = base_spec;
                apply_layout_overrides(spec, composite_preset, LayoutKind::Minimal);
                result.minimal = save_variant(oriented,
                                              text_drawer,
                                              spec,
                                              LayoutKind::Minimal,
                                              result.orientation,
                                              base_path + "_annotated_minimal.png",
                                              &minimal_rendered);
            }
            if (settings.save_editorial && text_drawer.font_ready())
            {
                image::presentation::PresentationSpec spec = base_spec;
                apply_layout_overrides(spec, composite_preset, LayoutKind::Editorial);
                result.editorial = save_variant(oriented,
                                                text_drawer,
                                                spec,
                                                LayoutKind::Editorial,
                                                result.orientation,
                                                base_path + "_annotated_presentation.png",
                                                &editorial_rendered);
            }

            if (settings.save_legacy_alias && text_drawer.font_ready())
            {
                if (editorial_rendered.size() == 0 && minimal_rendered.size() == 0)
                {
                    image::presentation::PresentationSpec compatibility_spec = base_spec;
                    apply_layout_overrides(compatibility_spec, composite_preset, LayoutKind::Editorial);
                    compatibility_spec.branding += " · Presentation";
                    editorial_rendered = image::presentation::render_layout(oriented, text_drawer, compatibility_spec, LayoutKind::Editorial);
                }

                const image::Image &selected = editorial_rendered.size() > 0 ? editorial_rendered : minimal_rendered;
                const std::string layout = editorial_rendered.size() > 0 ? "editorial" : "minimal";
                result.legacy_alias = save_legacy(selected,
                                                  base_spec,
                                                  result.orientation,
                                                  oriented,
                                                  base_path + "_annotated.png",
                                                  layout);
            }
            return result;
        }
    }
}
