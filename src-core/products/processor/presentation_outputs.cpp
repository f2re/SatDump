#include "presentation_outputs.h"

#include "common/image/io.h"
#include "common/image/meta.h"
#include "common/projection/gcp_compute/gcp_compute.h"
#include "common/projection/projs2/proj_json.h"
#include "core/config.h"
#include "logger.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

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
                    proj::projection_free(&projection);

                    if (top_latitudes.empty() || bottom_latitudes.empty())
                        return false;
                    info.top_latitude = median(top_latitudes);
                    info.bottom_latitude = median(bottom_latitudes);
                    info.latitudes_valid = std::fabs(info.top_latitude - info.bottom_latitude) > 0.01;
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
                    for (const satdump::projection::GCP &gcp : gcps)
                    {
                        minimum_y = std::min(minimum_y, gcp.y);
                        maximum_y = std::max(maximum_y, gcp.y);
                    }
                    const double span = maximum_y - minimum_y;
                    if (!std::isfinite(span) || span <= 0.0)
                        return false;

                    std::vector<double> top_latitudes;
                    std::vector<double> bottom_latitudes;
                    const double top_limit = minimum_y + span * 0.22;
                    const double bottom_limit = maximum_y - span * 0.22;
                    for (const satdump::projection::GCP &gcp : gcps)
                    {
                        if (!std::isfinite(gcp.lat))
                            continue;
                        if (gcp.y <= top_limit)
                            top_latitudes.push_back(gcp.lat);
                        if (gcp.y >= bottom_limit)
                            bottom_latitudes.push_back(gcp.lat);
                    }
                    if (top_latitudes.empty() || bottom_latitudes.empty())
                        return false;

                    info.top_latitude = median(top_latitudes);
                    info.bottom_latitude = median(bottom_latitudes);
                    info.latitudes_valid = std::fabs(info.top_latitude - info.bottom_latitude) > 0.05;
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
                    if (info.top_latitude < info.bottom_latitude)
                    {
                        info.transform = RasterTransform::FlipVertical;
                        info.description = "север сверху · выполнено вертикальное отражение";
                        if (info.pass_direction.empty())
                            info.pass_direction = "ascending";
                    }
                    else
                    {
                        info.transform = RasterTransform::None;
                        info.description = "север сверху · исходная ориентация корректна";
                        if (info.pass_direction.empty())
                            info.pass_direction = "descending";
                    }
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

            if (!settings.save_minimal && !settings.save_editorial && !settings.save_legacy_alias)
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
                                  const std::string &base_path)
        {
            OutputResult result;
            const OutputSettings settings = resolve_output_settings(composite_preset);
            if (!settings.enabled || source.size() == 0 || !text_drawer.font_ready())
                return result;

            result.orientation = analyze_orientation(source, products, timestamps, product_metadata, source_variant, settings);
            image::Image oriented = image::presentation::apply_transform(source, result.orientation.transform);

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

            image::Image editorial_rendered;
            image::Image minimal_rendered;
            if (settings.save_minimal)
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
            if (settings.save_editorial)
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

            if (settings.save_legacy_alias)
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
