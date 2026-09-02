#include "presentation_semantics.h"
#include "presentation_processor.h"

#include "common/image/io.h"
#include "logger.h"
#include "resources.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace satdump
{
    namespace product_presentation
    {
        namespace
        {
            using image::presentation::CategoryEntry;
            using image::presentation::Color;
            using image::presentation::ColorStop;
            using image::presentation::CompositeComponent;
            using image::presentation::LegendKind;
            using image::presentation::LegendSpec;
            using image::presentation::LegendTick;
            using image::presentation::MetadataField;
            using image::presentation::PresentationSpec;

            struct PhysicalScale
            {
                bool valid = false;
                std::string type;
                std::string title;
                std::string unit;
                double minimum = 0.0;
                double maximum = 1.0;
            };

            std::string trim(const std::string &value)
            {
                const size_t first = value.find_first_not_of(" \t\r\n");
                if (first == std::string::npos)
                    return "";
                const size_t last = value.find_last_not_of(" \t\r\n");
                return value.substr(first, last - first + 1);
            }

            std::string collapse_spaces(const std::string &value)
            {
                std::string output;
                bool previous_space = false;
                for (char character : value)
                {
                    const bool whitespace = character == ' ' || character == '\t' ||
                                            character == '\r' || character == '\n';
                    if (whitespace)
                    {
                        if (!previous_space && !output.empty())
                            output.push_back(' ');
                    }
                    else
                    {
                        output.push_back(character);
                    }
                    previous_space = whitespace;
                }
                return trim(output);
            }

            std::string lowercase_ascii(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                               { return character < 128 ? (char)std::tolower(character) : (char)character; });
                return value;
            }

            std::string normalized_key(const std::string &value)
            {
                std::string output;
                bool previous_separator = false;
                for (unsigned char character : value)
                {
                    if (character < 128 && std::isalnum(character))
                    {
                        output.push_back((char)std::tolower(character));
                        previous_separator = false;
                    }
                    else if (character < 128)
                    {
                        if (!output.empty() && !previous_separator)
                        {
                            output.push_back(' ');
                            previous_separator = true;
                        }
                    }
                    else
                    {
                        output.push_back((char)character);
                        previous_separator = false;
                    }
                }
                while (!output.empty() && output.back() == ' ')
                    output.pop_back();
                return output;
            }

            std::string compact_ascii(const std::string &value)
            {
                std::string output;
                for (unsigned char character : value)
                    if (character < 128 && std::isalnum(character))
                        output.push_back((char)std::tolower(character));
                return output;
            }

            std::string basename(const std::string &path)
            {
                const size_t separator = path.find_last_of("/\\");
                return separator == std::string::npos ? path : path.substr(separator + 1);
            }

            bool contains_ci(const std::string &haystack, const std::string &needle)
            {
                if (needle.empty())
                    return false;
                return lowercase_ascii(haystack).find(lowercase_ascii(needle)) != std::string::npos;
            }

            bool json_bool(const nlohmann::json &value, bool fallback)
            {
                try
                {
                    if (value.is_boolean())
                        return value.get<bool>();
                    if (value.is_object() && value.contains("value"))
                        return json_bool(value["value"], fallback);
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
                    if (value.is_object() && value.contains("value"))
                        return json_string(value["value"], fallback);
                }
                catch (const std::exception &)
                {
                }
                return fallback;
            }

            bool json_number(const nlohmann::json &object, const std::string &key, double &value)
            {
                if (!object.is_object() || !object.contains(key))
                    return false;
                try
                {
                    if (object[key].is_number())
                    {
                        value = object[key].get<double>();
                        return std::isfinite(value);
                    }
                    if (object[key].is_string())
                    {
                        value = std::stod(object[key].get<std::string>());
                        return std::isfinite(value);
                    }
                }
                catch (const std::exception &)
                {
                }
                return false;
            }

            Color parse_color(const nlohmann::json &value, const Color &fallback)
            {
                try
                {
                    if (value.is_array() && value.size() >= 3)
                    {
                        Color output = {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
                        const double maximum = std::max(output[0], std::max(output[1], output[2]));
                        if (maximum > 1.0)
                            for (double &component : output)
                                component /= 255.0;
                        for (double &component : output)
                            component = std::max(0.0, std::min(1.0, component));
                        return output;
                    }
                    if (value.is_string())
                    {
                        std::string text = value.get<std::string>();
                        if (!text.empty() && text.front() == '#')
                            text.erase(text.begin());
                        if (text.size() == 6 || text.size() == 8)
                            return {
                                (double)std::stoi(text.substr(0, 2), nullptr, 16) / 255.0,
                                (double)std::stoi(text.substr(2, 2), nullptr, 16) / 255.0,
                                (double)std::stoi(text.substr(4, 2), nullptr, 16) / 255.0};
                    }
                }
                catch (const std::exception &)
                {
                }
                return fallback;
            }

            std::string format_number_ru(double value, int precision = 1)
            {
                if (!std::isfinite(value))
                    return "";
                std::ostringstream stream;
                stream << std::fixed << std::setprecision(precision) << value;
                std::string output = stream.str();
                if (output.find('.') != std::string::npos)
                {
                    while (!output.empty() && output.back() == '0')
                        output.pop_back();
                    if (!output.empty() && output.back() == '.')
                        output.pop_back();
                }
                std::replace(output.begin(), output.end(), '.', ',');
                return output;
            }

            const nlohmann::json *presentation_section(const nlohmann::json &preset)
            {
                if (!preset.is_object() || !preset.contains("presentation") ||
                    !preset["presentation"].is_object())
                    return nullptr;
                return &preset["presentation"];
            }

            const nlohmann::json *semantic_section(const nlohmann::json &preset)
            {
                const nlohmann::json *presentation = presentation_section(preset);
                if (presentation == nullptr)
                    return nullptr;
                for (const char *key : {"semantics", "semantic"})
                    if (presentation->contains(key) && (*presentation)[key].is_object())
                        return &(*presentation)[key];
                return nullptr;
            }

            bool semantics_enabled(const nlohmann::json &preset)
            {
                const nlohmann::json *presentation = presentation_section(preset);
                if (presentation == nullptr)
                    return true;
                for (const char *key : {"auto_semantics", "semantic_enabled"})
                    if (presentation->contains(key))
                        return json_bool((*presentation)[key], true);
                if (presentation->contains("semantic") && (*presentation)["semantic"].is_boolean())
                    return (*presentation)["semantic"].get<bool>();
                return true;
            }

            bool explicit_legend(const nlohmann::json &preset)
            {
                const nlohmann::json *presentation = presentation_section(preset);
                return presentation != nullptr && presentation->contains("legend") &&
                       (*presentation)["legend"].is_object();
            }

            std::string forced_profile(const nlohmann::json &preset)
            {
                const nlohmann::json *presentation = presentation_section(preset);
                if (presentation != nullptr)
                {
                    for (const char *key : {"profile", "semantic_profile"})
                        if (presentation->contains(key))
                            return json_string((*presentation)[key]);
                }
                const nlohmann::json *semantic = semantic_section(preset);
                if (semantic != nullptr && semantic->contains("profile"))
                    return json_string((*semantic)["profile"]);
                return "";
            }

            std::string profile_resource_path()
            {
                const char *override_path = std::getenv("SATDUMP_PRESENTATION_PROFILES");
                if (override_path != nullptr && *override_path != '\0')
                    return override_path;
                return resources::getResourcePath("presentation/composite_profiles_ru.json");
            }

            const nlohmann::json &profile_catalog()
            {
                static nlohmann::json catalog = nlohmann::json::object();
                static std::once_flag initialized;
                std::call_once(initialized, []()
                               {
                                   try
                                   {
                                       std::ifstream input(profile_resource_path());
                                       if (!input.good())
                                       {
                                           logger->warn("Presentation semantic profiles are unavailable: %s", profile_resource_path().c_str());
                                           return;
                                       }
                                       input >> catalog;
                                       if (!catalog.is_object() || !catalog.contains("profiles") || !catalog["profiles"].is_array())
                                       {
                                           logger->warn("Presentation semantic profile catalog has an invalid schema");
                                           catalog = nlohmann::json::object();
                                       }
                                   }
                                   catch (const std::exception &error)
                                   {
                                       logger->warn("Could not load presentation semantic profiles: %s", error.what());
                                       catalog = nlohmann::json::object();
                                   }
                               });
                return catalog;
            }

            std::string handler_name(const ImageCompositeCfg &composite)
            {
                if (!composite.equation.empty())
                    return "equation";
                if (!composite.lut.empty())
                    return "lut";
                if (!composite.lua.empty())
                    return "lua";
                if (!composite.cpp.empty())
                    return "cpp";
                return "unknown";
            }

            std::string field_value(const std::string &field,
                                    ImageProducts &products,
                                    const ImageCompositeCfg &composite,
                                    const std::string &product_name)
            {
                if (field == "name")
                    return product_name;
                if (field == "description")
                    return composite.description_markdown;
                if (field == "lut")
                    return composite.lut;
                if (field == "lua")
                    return composite.lua;
                if (field == "cpp")
                    return composite.cpp;
                if (field == "instrument")
                    return products.instrument_name;
                return "";
            }

            bool array_contains_string(const nlohmann::json &array, const std::string &value)
            {
                if (!array.is_array())
                    return false;
                for (const nlohmann::json &entry : array)
                    if (entry.is_string() && lowercase_ascii(entry.get<std::string>()) == lowercase_ascii(value))
                        return true;
                return false;
            }

            bool any_substring_matches(const nlohmann::json &array, const std::string &value)
            {
                if (!array.is_array())
                    return false;
                for (const nlohmann::json &entry : array)
                    if (entry.is_string() && contains_ci(value, entry.get<std::string>()))
                        return true;
                return false;
            }

            bool profile_matches(const nlohmann::json &profile,
                                 ImageProducts &products,
                                 const ImageCompositeCfg &composite,
                                 const std::string &product_name)
            {
                if (!profile.is_object() || !profile.contains("match") || !profile["match"].is_object())
                    return false;
                const nlohmann::json &match = profile["match"];
                const std::string handler = handler_name(composite);
                if (match.contains("handler_any") && !array_contains_string(match["handler_any"], handler))
                    return false;

                bool has_text_rule = false;
                bool text_rule_matched = false;
                const std::vector<std::pair<std::string, std::string>> rules = {
                    {"name_contains", "name"},
                    {"description_contains", "description"},
                    {"lut_contains", "lut"},
                    {"lua_contains", "lua"},
                    {"cpp_contains", "cpp"},
                    {"instrument_contains", "instrument"}};
                for (const auto &rule : rules)
                {
                    if (!match.contains(rule.first) || !match[rule.first].is_array() || match[rule.first].empty())
                        continue;
                    has_text_rule = true;
                    text_rule_matched = text_rule_matched ||
                                        any_substring_matches(match[rule.first], field_value(rule.second, products, composite, product_name));
                }
                return !has_text_rule || text_rule_matched;
            }

            const nlohmann::json *select_profile(ImageProducts &products,
                                                 const ImageCompositeCfg &composite,
                                                 const nlohmann::json &preset,
                                                 const std::string &product_name)
            {
                const nlohmann::json &catalog = profile_catalog();
                if (!catalog.is_object() || !catalog.contains("profiles") || !catalog["profiles"].is_array())
                    return nullptr;
                const std::string forced = lowercase_ascii(forced_profile(preset));
                if (!forced.empty())
                {
                    for (const nlohmann::json &profile : catalog["profiles"])
                        if (profile.is_object() && lowercase_ascii(profile.value("id", "")) == forced)
                            return &profile;
                    logger->warn("Unknown presentation semantic profile: %s", forced.c_str());
                }
                for (const nlohmann::json &profile : catalog["profiles"])
                    if (profile_matches(profile, products, composite, product_name))
                        return &profile;
                return nullptr;
            }

            std::string localized_instrument(const std::string &input)
            {
                static const std::unordered_map<std::string, std::string> names = {
                    {"msu mr", "МСУ-МР"},
                    {"msumr", "МСУ-МР"},
                    {"mtvza", "МТВЗА-ГЯ"},
                    {"mtvza gy", "МТВЗА-ГЯ"},
                    {"mtvzagy", "МТВЗА-ГЯ"},
                    {"avhrr", "AVHRR"},
                    {"avhrr 3", "AVHRR/3"},
                    {"amsu a", "AMSU-A"},
                    {"mhs", "MHS"},
                    {"hirs", "HIRS"},
                    {"modis", "MODIS"},
                    {"viirs", "VIIRS"},
                    {"mersi", "MERSI"},
                    {"seviri", "SEVIRI"},
                    {"abi", "ABI"},
                    {"ahi", "AHI"},
                    {"fci", "FCI"}};
                const std::string key = normalized_key(input);
                auto iterator = names.find(key);
                return iterator == names.end() ? input : iterator->second;
            }

            std::string localized_satellite(const std::string &input)
            {
                const std::string compact = compact_ascii(input);
                for (const std::string &prefix : {"meteorm2", "meteormn2"})
                {
                    if (compact.rfind(prefix, 0) == 0)
                    {
                        const std::string number = compact.substr(prefix.size());
                        if (!number.empty())
                            return "Метеор-М №2-" + number;
                        return "Метеор-М №2";
                    }
                }
                return input;
            }

            std::string localized_product_fallback(const std::string &input)
            {
                const std::string value = lowercase_ascii(input);
                if (value.find("natural color") != std::string::npos || value.find("true color") != std::string::npos)
                    return "Естественные цвета";
                if (value.find("false color") != std::string::npos)
                    return "Изображение в ложных цветах";
                if (value.find("night microphysics") != std::string::npos)
                    return "Ночная микрофизика облаков";
                if (value.find("day microphysics") != std::string::npos)
                    return "Дневная микрофизика облаков";
                if (value.find("sea surface temperature") != std::string::npos)
                    return "Температура поверхности моря";
                if (value.find("cloud type") != std::string::npos)
                    return "Тип облачности";
                if (value.find("cloud phase") != std::string::npos)
                    return "Фаза облачных частиц";
                if (value.find("water vapor") != std::string::npos || value.find("water vapour") != std::string::npos)
                    return "Водяной пар";
                if (value.find("fire") != std::string::npos || value.find("hot spot") != std::string::npos)
                    return "Тепловые аномалии";
                if (value.find("enhancement") != std::string::npos)
                    return "Спутниковое цветовое усиление «" + input + "»";
                return input;
            }

            void append_or_replace_detail(PresentationSpec &spec, const std::string &label, const std::string &value)
            {
                if (value.empty())
                    return;
                for (MetadataField &field : spec.pass.details)
                {
                    if (field.label == label)
                    {
                        field.value = value;
                        return;
                    }
                }
                spec.pass.details.push_back({label, value});
            }

            void rename_detail(PresentationSpec &spec, const std::string &old_label, const std::string &new_label)
            {
                for (MetadataField &field : spec.pass.details)
                    if (field.label == old_label)
                        field.label = new_label;
            }

            void reorder_details(PresentationSpec &spec)
            {
                const std::vector<std::string> order = {
                    "Назначение", "Данные", "Режим", "Каналы", "Физика", "Обработка",
                    "Проекция", "Приём", "Дискретизация", "NORAD", "Подготовка",
                    "Кадр", "Ориентация"};
                std::stable_sort(spec.pass.details.begin(), spec.pass.details.end(), [&](const MetadataField &left, const MetadataField &right)
                                 {
                                     auto rank = [&](const std::string &label)
                                     {
                                         auto iterator = std::find(order.begin(), order.end(), label);
                                         return iterator == order.end() ? order.size() : (size_t)std::distance(order.begin(), iterator);
                                     };
                                     return rank(left.label) < rank(right.label);
                                 });
            }

            void append_note(LegendSpec &legend, const std::string &note)
            {
                const std::string normalized = collapse_spaces(note);
                if (normalized.empty())
                    return;
                for (const std::string &existing : legend.notes)
                    if (collapse_spaces(existing) == normalized)
                        return;
                legend.notes.push_back(normalized);
            }

            std::vector<std::string> component_notes(const LegendSpec &legend)
            {
                std::vector<std::string> output;
                for (const CompositeComponent &component : legend.components)
                {
                    std::string details = collapse_spaces(component.description);
                    if (details.empty())
                    {
                        std::vector<std::string> parts;
                        if (!component.channel.empty()) parts.push_back(component.channel);
                        if (!component.spectral_range.empty()) parts.push_back(component.spectral_range);
                        if (!component.quantity.empty()) parts.push_back(component.quantity);
                        if (!component.formula.empty()) parts.push_back("формула " + component.formula);
                        for (const std::string &part : parts)
                        {
                            if (!details.empty()) details += " · ";
                            details += part;
                        }
                    }
                    if (!details.empty())
                        output.push_back("Компонент " + (component.component.empty() ? std::string("IN") : component.component) + ": " + details);
                }
                return output;
            }

            PhysicalScale physical_scale(const ImageCompositeCfg &composite)
            {
                PhysicalScale result;
                if (!composite.calib_cfg.is_object())
                    return result;

                std::vector<PhysicalScale> candidates;
                for (auto iterator = composite.calib_cfg.begin(); iterator != composite.calib_cfg.end(); ++iterator)
                {
                    if (!iterator.value().is_object())
                        continue;
                    double minimum = 0.0;
                    double maximum = 0.0;
                    if (!json_number(iterator.value(), "min", minimum) ||
                        !json_number(iterator.value(), "max", maximum) ||
                        maximum <= minimum)
                        continue;
                    PhysicalScale candidate;
                    candidate.valid = true;
                    candidate.type = lowercase_ascii(iterator.value().value("type", ""));
                    candidate.minimum = minimum;
                    candidate.maximum = maximum;
                    if (candidate.type == "temperature")
                    {
                        candidate.title = "Яркостная температура";
                        candidate.unit = "K";
                    }
                    else if (candidate.type == "albedo" || candidate.type == "reflectance")
                    {
                        candidate.title = "Отражательная способность";
                        candidate.unit = maximum <= 1.5 ? "доля 0–1" : "%";
                    }
                    else if (candidate.type == "radiance")
                    {
                        candidate.title = "Спектральная радианс";
                        candidate.unit = iterator.value().value("unit", "условные единицы");
                    }
                    else
                    {
                        candidate.title = "Калиброванная величина";
                        candidate.unit = iterator.value().value("unit", "");
                    }
                    candidates.push_back(candidate);
                }
                if (candidates.size() == 1)
                    return candidates.front();
                return result;
            }

            std::vector<ColorStop> sampled_lut(const ImageCompositeCfg &composite, size_t count = 7)
            {
                std::vector<ColorStop> output;
                if (composite.lut.empty() || count < 2)
                    return output;
                try
                {
                    const std::string path = resources::getResourcePath(composite.lut);
                    image::Image lut;
                    image::load_img(lut, path);
                    if (lut.size() == 0 || lut.width() == 0 || lut.height() == 0)
                        return output;
                    for (size_t index = 0; index < count; index++)
                    {
                        const double position = (double)index / (double)(count - 1);
                        const size_t x = lut.width() >= lut.height()
                                             ? std::min(lut.width() - 1, (size_t)std::llround(position * (double)(lut.width() - 1)))
                                             : lut.width() / 2;
                        const size_t y = lut.height() > lut.width()
                                             ? std::min(lut.height() - 1, (size_t)std::llround(position * (double)(lut.height() - 1)))
                                             : lut.height() / 2;
                        Color color(3, 0.0);
                        if (lut.channels() >= 3)
                        {
                            color[0] = lut.getf(0, x, y);
                            color[1] = lut.getf(1, x, y);
                            color[2] = lut.getf(2, x, y);
                        }
                        else
                        {
                            const double value = lut.getf(0, x, y);
                            color = {value, value, value};
                        }
                        output.push_back({position, color});
                    }
                }
                catch (const std::exception &error)
                {
                    logger->debug("Could not sample presentation LUT %s: %s", composite.lut.c_str(), error.what());
                }
                return output;
            }

            std::vector<ColorStop> default_scale_colors(const PhysicalScale &scale, const ImageCompositeCfg &composite)
            {
                std::vector<ColorStop> lut = sampled_lut(composite);
                if (!lut.empty())
                    return lut;
                if (scale.type == "temperature")
                    return {
                        {0.00, {0.16, 0.10, 0.34}},
                        {0.20, {0.12, 0.30, 0.62}},
                        {0.40, {0.15, 0.58, 0.72}},
                        {0.60, {0.34, 0.72, 0.47}},
                        {0.80, {0.91, 0.76, 0.26}},
                        {1.00, {0.78, 0.20, 0.20}}};
                return composite.invert
                           ? std::vector<ColorStop>{{0.0, {1.0, 1.0, 1.0}}, {1.0, {0.05, 0.05, 0.05}}}
                           : std::vector<ColorStop>{{0.0, {0.05, 0.05, 0.05}}, {1.0, {1.0, 1.0, 1.0}}};
            }

            void make_ticks(LegendSpec &legend, double minimum, double maximum, const nlohmann::json *ticks = nullptr)
            {
                legend.ticks.clear();
                if (ticks != nullptr && ticks->is_array())
                {
                    for (const nlohmann::json &entry : *ticks)
                    {
                        if (!entry.is_number())
                            continue;
                        const double value = entry.get<double>();
                        const double position = maximum == minimum ? 0.0 : (value - minimum) / (maximum - minimum);
                        legend.ticks.push_back({std::max(0.0, std::min(1.0, position)), format_number_ru(value, std::fabs(value) < 10.0 ? 2 : 1)});
                    }
                }
                if (!legend.ticks.empty())
                    return;
                const int count = 7;
                for (int index = 0; index < count; index++)
                {
                    const double position = (double)index / (double)(count - 1);
                    const double value = minimum + (maximum - minimum) * position;
                    legend.ticks.push_back({position, format_number_ru(value, std::fabs(maximum - minimum) <= 5.0 ? 2 : 1)});
                }
            }

            void apply_continuous_legend(LegendSpec &legend,
                                         const nlohmann::json &legend_json,
                                         PhysicalScale scale,
                                         const ImageCompositeCfg &composite,
                                         const std::vector<std::string> &preserved_components)
            {
                double minimum = scale.valid ? scale.minimum : 0.0;
                double maximum = scale.valid ? scale.maximum : 1.0;
                json_number(legend_json, "min", minimum);
                json_number(legend_json, "max", maximum);
                if (maximum <= minimum)
                    maximum = minimum + 1.0;

                const std::string transform = lowercase_ascii(legend_json.value("unit_transform", ""));
                if (transform == "kelvin_to_celsius")
                {
                    minimum -= 273.15;
                    maximum -= 273.15;
                    scale.unit = "°C";
                }

                legend.kind = LegendKind::Continuous;
                legend.components.clear();
                legend.categories.clear();
                legend.title = legend_json.value("title", scale.valid ? scale.title : std::string("Физическая шкала"));
                legend.subtitle = legend_json.value("subtitle", legend.subtitle);
                legend.unit = legend_json.value("unit", scale.unit);
                legend.color_stops.clear();
                if (legend_json.contains("colors") && legend_json["colors"].is_array())
                {
                    const nlohmann::json &colors = legend_json["colors"];
                    for (size_t index = 0; index < colors.size(); index++)
                        legend.color_stops.push_back({colors.size() <= 1 ? 0.0 : (double)index / (double)(colors.size() - 1),
                                                      parse_color(colors[index], {0.5, 0.5, 0.5})});
                }
                if (legend.color_stops.empty())
                    legend.color_stops = default_scale_colors(scale, composite);
                const nlohmann::json *ticks = legend_json.contains("ticks") ? &legend_json["ticks"] : nullptr;
                make_ticks(legend, minimum, maximum, ticks);
                for (const std::string &note : preserved_components)
                    append_note(legend, note);
            }

            void apply_profile_legend(LegendSpec &legend,
                                      const nlohmann::json &profile,
                                      const PhysicalScale &scale,
                                      const ImageCompositeCfg &composite)
            {
                if (!profile.contains("legend") || !profile["legend"].is_object())
                    return;
                const nlohmann::json &definition = profile["legend"];
                std::string kind = lowercase_ascii(definition.value("kind", "auto"));
                const std::vector<std::string> preserved_components = component_notes(legend);
                if (kind == "auto")
                    kind = scale.valid ? "continuous" : (legend.kind == LegendKind::Composite ? "composite" : "explanation");

                if (kind == "continuous")
                {
                    PhysicalScale selected = scale;
                    if (!selected.valid && definition.contains("min") && definition.contains("max"))
                    {
                        selected.valid = true;
                        selected.title = definition.value("title", "Физическая шкала");
                        selected.unit = definition.value("unit", "");
                    }
                    apply_continuous_legend(legend, definition, selected, composite, preserved_components);
                }
                else if (kind == "categorical")
                {
                    legend.kind = LegendKind::Categorical;
                    legend.components.clear();
                    legend.color_stops.clear();
                    legend.ticks.clear();
                    legend.unit.clear();
                    legend.title = definition.value("title", "Как читать цвета");
                    legend.subtitle = definition.value("subtitle", legend.subtitle);
                    legend.categories.clear();
                    if (definition.contains("categories") && definition["categories"].is_array())
                    {
                        for (const nlohmann::json &category : definition["categories"])
                        {
                            if (!category.is_object())
                                continue;
                            CategoryEntry entry;
                            entry.label = category.value("label", "Без названия");
                            if (category.contains("color"))
                                entry.color = parse_color(category["color"], {0.5, 0.5, 0.5});
                            legend.categories.push_back(entry);
                        }
                    }
                    for (const std::string &note : preserved_components)
                        append_note(legend, note);
                }
                else if (kind == "composite")
                {
                    legend.kind = LegendKind::Composite;
                    legend.title = definition.value("title", "Как построен цвет");
                    legend.subtitle = definition.value("subtitle", legend.subtitle);
                }
                else
                {
                    legend.kind = LegendKind::None;
                    legend.components.clear();
                    legend.categories.clear();
                    legend.color_stops.clear();
                    legend.ticks.clear();
                    legend.unit.clear();
                    legend.title = definition.value("title", "Как читать продукт");
                    legend.subtitle = definition.value("subtitle", legend.subtitle);
                    for (const std::string &note : preserved_components)
                        append_note(legend, note);
                }

                if (definition.contains("notes") && definition["notes"].is_array())
                    for (const nlohmann::json &note : definition["notes"])
                        if (note.is_string())
                            append_note(legend, note.get<std::string>());
            }

            std::string handler_description(const ImageCompositeCfg &composite, const LegendSpec &legend)
            {
                const std::string handler = handler_name(composite);
                if (handler == "equation")
                    return legend.kind == LegendKind::Composite ? "формулы компонентов R/G/B" : "формульный расчёт";
                if (handler == "lut")
                    return "таблица цветов LUT " + basename(composite.lut);
                if (handler == "lua")
                    return "скрипт Lua " + basename(composite.lua);
                if (handler == "cpp")
                    return "алгоритм C++ " + basename(composite.cpp);
                return "не указан";
            }

            std::string generic_purpose(const ImageCompositeCfg &composite, const LegendSpec &legend)
            {
                if (legend.kind == LegendKind::Composite)
                    return "Качественная интерпретация сочетания нескольких спектральных каналов";
                const std::string handler = handler_name(composite);
                if (handler == "lut")
                    return "Визуальное выделение диапазонов исходной или калиброванной величины таблицей цветов";
                if (handler == "lua" || handler == "cpp")
                    return "Тематическая обработка спутниковых каналов специализированным алгоритмом";
                return "Просмотр пространственного распределения спутниковой величины";
            }

            std::string generic_data_kind(const ImageCompositeCfg &composite,
                                          const LegendSpec &legend,
                                          const PhysicalScale &scale)
            {
                if (scale.valid)
                    return "калиброванная физическая величина";
                if (legend.kind == LegendKind::Composite)
                    return "качественный RGB-композит";
                const std::string handler = handler_name(composite);
                if (handler == "lut")
                    return "цветовое усиление LUT";
                if (handler == "lua")
                    return "тематический продукт Lua";
                if (handler == "cpp")
                    return "тематический продукт C++";
                return "тематический спутниковый продукт";
            }

            void apply_generic_legend(LegendSpec &legend,
                                      const PhysicalScale &scale,
                                      const ImageCompositeCfg &composite,
                                      const std::string &purpose)
            {
                if (scale.valid && legend.kind != LegendKind::Composite)
                {
                    nlohmann::json definition = {
                        {"kind", "continuous"},
                        {"title", scale.title},
                        {"subtitle", "Цвет соответствует положению значения на физической шкале; точные числа приведены на делениях."},
                        {"unit", scale.unit}};
                    apply_continuous_legend(legend, definition, scale, composite, {});
                    append_note(legend, "Интерпретируйте физические значения, а не только визуальный оттенок.");
                    return;
                }

                if (legend.kind == LegendKind::Composite)
                {
                    legend.title = "Как построен цвет";
                    legend.subtitle = "Каждый компонент R/G/B формируется указанными каналами и формулами; однозначного значения одного оттенка без этой схемы нет.";
                    append_note(legend, "Это качественный синтез. Цвет не является самостоятельной физической величиной.");
                    return;
                }

                legend.kind = LegendKind::None;
                if (legend.title.empty() || legend.title == "Тематический продукт" || legend.title == "Одноканальный тематический продукт")
                    legend.title = "Как читать продукт";
                if (legend.subtitle.empty())
                    legend.subtitle = purpose;
                const std::string handler = handler_name(composite);
                if (handler == "lut")
                    append_note(legend, "Цвет задан таблицей LUT " + basename(composite.lut) + "; без заданного физического диапазона оттенок следует трактовать качественно.");
                else if (handler == "lua")
                    append_note(legend, "Цвет и маски сформированы скриптом Lua " + basename(composite.lua) + "; точная логика определяется этим алгоритмом.");
                else if (handler == "cpp")
                    append_note(legend, "Цвет и маски сформированы алгоритмом C++ " + basename(composite.cpp) + "; точная логика определяется этим обработчиком.");
                else
                    append_note(legend, "Для количественной интерпретации необходима явно заданная калибровка или легенда конкретного preset.");
            }

            std::string profile_value(const nlohmann::json *profile, const std::string &key)
            {
                return profile != nullptr && profile->is_object() ? profile->value(key, "") : "";
            }

            void apply_semantic_overrides(PresentationSpec &spec,
                                          const nlohmann::json &preset,
                                          std::string &purpose,
                                          std::string &mode,
                                          std::string &data_kind)
            {
                const nlohmann::json *semantic = semantic_section(preset);
                if (semantic == nullptr)
                    return;
                if (semantic->contains("title") && (*semantic)["title"].is_string())
                    spec.pass.product = (*semantic)["title"].get<std::string>();
                if (semantic->contains("purpose") && (*semantic)["purpose"].is_string())
                    purpose = (*semantic)["purpose"].get<std::string>();
                if (semantic->contains("mode") && (*semantic)["mode"].is_string())
                    mode = (*semantic)["mode"].get<std::string>();
                if (semantic->contains("data_kind") && (*semantic)["data_kind"].is_string())
                    data_kind = (*semantic)["data_kind"].get<std::string>();
            }
        }

        void apply_presentation_semantics(PresentationSpec &spec,
                                          ImageProducts &products,
                                          const ImageCompositeCfg &composite,
                                          const nlohmann::json &composite_preset,
                                          const std::string &product_name)
        {
            if (!semantics_enabled(composite_preset))
                return;

            spec.pass.satellite = localized_satellite(spec.pass.satellite);
            spec.pass.instrument = localized_instrument(spec.pass.instrument);

            const nlohmann::json *profile = select_profile(products, composite, composite_preset, product_name);
            const nlohmann::json *presentation = presentation_section(composite_preset);
            const bool title_is_explicit = presentation != nullptr && presentation->contains("title") && (*presentation)["title"].is_string();
            if (!title_is_explicit)
            {
                const std::string title = profile_value(profile, "title");
                spec.pass.product = title.empty() ? localized_product_fallback(spec.pass.product) : title;
            }

            const PhysicalScale scale = physical_scale(composite);
            std::string purpose = profile_value(profile, "purpose");
            if (purpose.empty())
                purpose = generic_purpose(composite, spec.legend);
            std::string mode = profile_value(profile, "mode");
            if (mode.empty())
                mode = "зависит от используемых каналов";
            std::string data_kind = profile_value(profile, "data_kind");
            if (data_kind.empty())
                data_kind = generic_data_kind(composite, spec.legend, scale);

            apply_semantic_overrides(spec, composite_preset, purpose, mode, data_kind);

            append_or_replace_detail(spec, "Назначение", purpose);
            append_or_replace_detail(spec, "Данные", data_kind);
            append_or_replace_detail(spec, "Режим", mode);
            if (scale.valid)
                append_or_replace_detail(spec, "Физика", scale.title + (scale.unit.empty() ? "" : " [" + scale.unit + "]"));
            append_or_replace_detail(spec, "Обработка", handler_description(composite, spec.legend));
            rename_detail(spec, "Вариант", "Подготовка");

            if (!explicit_legend(composite_preset))
            {
                if (profile != nullptr)
                    apply_profile_legend(spec.legend, *profile, scale, composite);
                else
                    apply_generic_legend(spec.legend, scale, composite, purpose);
            }
            else
            {
                append_note(spec.legend, "Назначение продукта: " + purpose + ".");
            }

            if (profile != nullptr)
                logger->debug("Presentation semantics: %s -> profile %s", product_name.c_str(), profile->value("id", "unknown").c_str());
            else
                logger->debug("Presentation semantics: %s -> generic %s profile", product_name.c_str(), handler_name(composite).c_str());

            reorder_details(spec);
        }

        PresentationSpec build_spec(ImageProducts &products,
                                    const ImageCompositeCfg &composite,
                                    const nlohmann::json &composite_preset,
                                    const std::string &product_name,
                                    const std::vector<double> &timestamps,
                                    const nlohmann::json &product_metadata,
                                    const std::string &source_variant)
        {
            PresentationSpec spec = build_spec_raw(products,
                                                   composite,
                                                   composite_preset,
                                                   product_name,
                                                   timestamps,
                                                   product_metadata,
                                                   source_variant);
            apply_presentation_semantics(spec, products, composite, composite_preset, product_name);
            return spec;
        }
    }
}
