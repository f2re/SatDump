#include "presentation_processor.h"

#include "common/image/io.h"
#include "core/config.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

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
                    const bool is_space = character == ' ' || character == '\t' || character == '\r' || character == '\n';
                    if (is_space)
                    {
                        if (!previous_space && !output.empty())
                            output.push_back(' ');
                    }
                    else
                    {
                        output.push_back(character);
                    }
                    previous_space = is_space;
                }
                return trim(output);
            }

            std::string lowercase(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                               { return (char)std::tolower(character); });
                return value;
            }

            std::string format_number(double value, int precision)
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
                return output;
            }

            std::string scalar_to_string(const nlohmann::json &value)
            {
                if (value.is_string())
                    return value.get<std::string>();
                if (value.is_boolean())
                    return value.get<bool>() ? "true" : "false";
                if (value.is_number_unsigned())
                    return std::to_string(value.get<unsigned long long>());
                if (value.is_number_integer())
                    return std::to_string(value.get<long long>());
                if (value.is_number_float())
                    return format_number(value.get<double>(), 3);
                return "";
            }

            const nlohmann::json *lookup_path(const nlohmann::json &root, const std::vector<std::string> &path)
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

            std::string first_path_string(const nlohmann::json &primary,
                                          const nlohmann::json &secondary,
                                          const std::vector<std::vector<std::string>> &paths)
            {
                for (const nlohmann::json *root : {&primary, &secondary})
                {
                    for (const std::vector<std::string> &path : paths)
                    {
                        const nlohmann::json *value = lookup_path(*root, path);
                        if (value != nullptr)
                        {
                            const std::string text = scalar_to_string(*value);
                            if (!text.empty())
                                return text;
                        }
                    }
                }
                return "";
            }

            bool bool_value(const nlohmann::json &value, bool fallback)
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

            Color parse_color(const nlohmann::json &value, const Color &fallback)
            {
                if (value.is_array() && value.size() >= 3)
                {
                    try
                    {
                        Color color = {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
                        if (std::max(color[0], std::max(color[1], color[2])) > 1.0)
                        {
                            color[0] /= 255.0;
                            color[1] /= 255.0;
                            color[2] /= 255.0;
                        }
                        for (double &component : color)
                            component = std::max(0.0, std::min(1.0, component));
                        return color;
                    }
                    catch (const std::exception &)
                    {
                        return fallback;
                    }
                }

                if (value.is_string())
                {
                    std::string text = value.get<std::string>();
                    if (!text.empty() && text.front() == '#')
                        text.erase(text.begin());
                    if (text.size() == 6)
                    {
                        try
                        {
                            return {
                                (double)std::stoi(text.substr(0, 2), nullptr, 16) / 255.0,
                                (double)std::stoi(text.substr(2, 2), nullptr, 16) / 255.0,
                                (double)std::stoi(text.substr(4, 2), nullptr, 16) / 255.0};
                        }
                        catch (const std::exception &)
                        {
                        }
                    }
                }
                return fallback;
            }

            std::vector<std::string> split_top_level(const std::string &value, char separator)
            {
                std::vector<std::string> output;
                int round_depth = 0;
                int square_depth = 0;
                int curly_depth = 0;
                bool quoted = false;
                char quote = '\0';
                std::string current;

                for (size_t index = 0; index < value.size(); index++)
                {
                    const char character = value[index];
                    if (quoted)
                    {
                        current.push_back(character);
                        if (character == quote && (index == 0 || value[index - 1] != '\\'))
                            quoted = false;
                        continue;
                    }

                    if (character == '\'' || character == '"')
                    {
                        quoted = true;
                        quote = character;
                        current.push_back(character);
                        continue;
                    }

                    if (character == '(')
                        round_depth++;
                    else if (character == ')')
                        round_depth = std::max(0, round_depth - 1);
                    else if (character == '[')
                        square_depth++;
                    else if (character == ']')
                        square_depth = std::max(0, square_depth - 1);
                    else if (character == '{')
                        curly_depth++;
                    else if (character == '}')
                        curly_depth = std::max(0, curly_depth - 1);

                    if (character == separator && round_depth == 0 && square_depth == 0 && curly_depth == 0)
                    {
                        const std::string item = trim(current);
                        if (!item.empty())
                            output.push_back(item);
                        current.clear();
                    }
                    else
                    {
                        current.push_back(character);
                    }
                }

                const std::string item = trim(current);
                if (!item.empty())
                    output.push_back(item);
                return output;
            }

            bool identifier_character(char character)
            {
                const unsigned char value = (unsigned char)character;
                return std::isalnum(value) || character == '_' || character == '.';
            }

            struct ChannelTokenCandidate
            {
                std::string token;
                int image_index = -1;
            };

            std::vector<ChannelTokenCandidate> channel_candidates(ImageProducts &products)
            {
                std::vector<ChannelTokenCandidate> output;
                for (size_t index = 0; index < products.images.size(); index++)
                {
                    output.push_back({"cch" + products.images[index].channel_name, (int)index});
                    output.push_back({"ch" + products.images[index].channel_name, (int)index});
                }
                std::sort(output.begin(), output.end(), [](const ChannelTokenCandidate &left, const ChannelTokenCandidate &right)
                          {
                              if (left.token.size() != right.token.size())
                                  return left.token.size() > right.token.size();
                              return left.token < right.token;
                          });
                return output;
            }

            std::vector<std::string> extract_channel_tokens(ImageProducts &products, const std::string &expression)
            {
                const std::vector<ChannelTokenCandidate> candidates = channel_candidates(products);
                std::vector<std::string> output;
                std::set<std::string> seen;

                for (size_t position = 0; position < expression.size(); position++)
                {
                    if (position > 0 && identifier_character(expression[position - 1]))
                        continue;

                    const ChannelTokenCandidate *match = nullptr;
                    for (const ChannelTokenCandidate &candidate : candidates)
                    {
                        if (position + candidate.token.size() > expression.size())
                            continue;
                        if (expression.compare(position, candidate.token.size(), candidate.token) != 0)
                            continue;

                        const size_t end = position + candidate.token.size();
                        if (end < expression.size() && identifier_character(expression[end]))
                            continue;

                        match = &candidate;
                        break;
                    }

                    if (match != nullptr)
                    {
                        if (seen.insert(match->token).second)
                            output.push_back(match->token);
                        position += match->token.size() - 1;
                    }
                }
                return output;
            }

            int channel_index(ImageProducts &products, const std::string &token)
            {
                std::string name = token;
                if (name.rfind("cch", 0) == 0)
                    name = name.substr(3);
                else if (name.rfind("ch", 0) == 0)
                    name = name.substr(2);

                for (size_t index = 0; index < products.images.size(); index++)
                    if (products.images[index].channel_name == name)
                        return (int)index;
                return -1;
            }

            std::string wavelength_label(ImageProducts &products, int index)
            {
                if (index < 0 || index >= (int)products.images.size())
                    return "";
                try
                {
                    const double wavenumber = products.get_wavenumber(index);
                    if (!std::isfinite(wavenumber) || wavenumber <= 0.0)
                        return "";
                    const double wavelength_um = 10000.0 / wavenumber;
                    return format_number(wavelength_um, wavelength_um < 10.0 ? 2 : 1) + " мкм";
                }
                catch (const std::exception &)
                {
                    return "";
                }
            }

            std::string calibrated_quantity(const ImageCompositeCfg &composite, const std::string &token)
            {
                if (token.rfind("cch", 0) != 0)
                    return "цифровой отсчёт";

                try
                {
                    if (composite.calib_cfg.contains(token) && composite.calib_cfg[token].contains("type"))
                    {
                        const std::string type = lowercase(composite.calib_cfg[token]["type"].get<std::string>());
                        if (type == "temperature")
                            return "яркостная температура";
                        if (type == "albedo")
                            return "альбедо / отражательная способность";
                        if (type == "radiance")
                            return "спектральная радианс";
                    }
                }
                catch (const std::exception &)
                {
                }
                return "калиброванная величина";
            }

            std::string describe_token(ImageProducts &products, const ImageCompositeCfg &composite, const std::string &token)
            {
                const int index = channel_index(products, token);
                std::string output = index >= 0 ? "канал " + products.images[index].channel_name : token;
                const std::string wavelength = wavelength_label(products, index);
                if (!wavelength.empty())
                    output += " · " + wavelength;
                const std::string quantity = calibrated_quantity(composite, token);
                if (!quantity.empty())
                    output += " · " + quantity;
                return output;
            }

            std::string join(const std::vector<std::string> &values, const std::string &separator)
            {
                std::string output;
                for (const std::string &value : values)
                {
                    if (value.empty())
                        continue;
                    if (!output.empty())
                        output += separator;
                    output += value;
                }
                return output;
            }

            std::string component_label(size_t index, size_t count)
            {
                if (count == 3)
                    return index == 0 ? "R" : (index == 1 ? "G" : "B");
                if (count == 4)
                    return index == 0 ? "R" : (index == 1 ? "G" : (index == 2 ? "B" : "A"));
                if (count == 1)
                    return "IN";
                return "IN " + std::to_string(index + 1);
            }

            std::vector<std::string> output_expressions(const ImageCompositeCfg &composite)
            {
                if (!composite.equation.empty())
                {
                    std::string expression = composite.equation;
                    const size_t last_semicolon = expression.find_last_of(';');
                    if (last_semicolon != std::string::npos && last_semicolon + 1 < expression.size())
                        expression = expression.substr(last_semicolon + 1);
                    return split_top_level(expression, ',');
                }
                if (!composite.channels.empty())
                    return split_top_level(composite.channels, ',');
                return {};
            }

            LegendSpec automatic_composite_legend(ImageProducts &products, const ImageCompositeCfg &composite)
            {
                LegendSpec legend;
                const std::vector<std::string> expressions = output_expressions(composite);
                const bool explicit_rgb = expressions.size() == 3 || expressions.size() == 4;
                const std::string source = !composite.channels.empty() ? composite.channels : composite.equation;
                const std::vector<std::string> tokens = extract_channel_tokens(products, source);

                if (!explicit_rgb)
                {
                    // A scalar equation or LUT may use one or many input channels,
                    // but it still produces one thematic value.  Calling every
                    // input an RGB component made IASI legends hundreds of lines
                    // tall and mislabeled one-channel MSA products as composites.
                    legend.kind = LegendKind::None;
                    legend.title = tokens.size() == 1 ? "Одноканальный тематический продукт" : "Тематический продукт";
                    std::vector<std::string> descriptions;
                    const size_t shown = std::min<size_t>(tokens.size(), 4);
                    for (size_t index = 0; index < shown; index++)
                        descriptions.push_back(describe_token(products, composite, tokens[index]));
                    legend.subtitle = join(descriptions, " · ");
                    if (tokens.size() > shown)
                        legend.subtitle += " · ещё " + std::to_string(tokens.size() - shown) + " каналов";
                }
                else
                {
                    legend.kind = LegendKind::Composite;
                    legend.title = "Состав RGB-композита";
                    legend.subtitle = "Спектральные каналы, физические величины и формулы, формирующие результирующий цвет";
                }

                if (explicit_rgb)
                {
                    for (size_t index = 0; index < expressions.size(); index++)
                    {
                        CompositeComponent component;
                        component.component = component_label(index, expressions.size());
                        component.formula = collapse_spaces(expressions[index]);

                        std::vector<std::string> descriptions;
                        for (const std::string &token : extract_channel_tokens(products, expressions[index]))
                            descriptions.push_back(describe_token(products, composite, token));
                        component.description = join(descriptions, " + ");
                        if (component.description.empty())
                            component.description = "формула: " + component.formula;
                        else if (!component.formula.empty())
                            component.description += "  |  формула: " + component.formula;
                        legend.components.push_back(component);
                    }
                }
                else
                {
                    if (legend.subtitle.empty() && !expressions.empty())
                        legend.subtitle = collapse_spaces(expressions.front());
                }

                if (!composite.lut.empty())
                    legend.notes.push_back("Цвета формируются таблицей LUT: " + composite.lut + ". Один результирующий цвет может зависеть от нескольких входных значений.");
                else if (!composite.lua.empty())
                    legend.notes.push_back("Цвета формируются скриптом Lua: " + composite.lua + ". Интерпретировать их следует по назначению конкретного композита.");
                else if (!composite.cpp.empty())
                    legend.notes.push_back("Цвета формируются алгоритмом C++: " + composite.cpp + ". Интерпретировать их следует по назначению конкретного композита.");
                else if (explicit_rgb)
                    legend.notes.push_back("Цвета синтезированы из перечисленных компонентов и не являются самостоятельной физической величиной.");
                else
                    legend.notes.push_back("Цветовая шкала отображает один результирующий тематический показатель, а не отдельные компоненты RGB.");

                std::vector<std::string> transformations;
                if (composite.equalize)
                    transformations.push_back("общая эквализация");
                if (composite.individual_equalize)
                    transformations.push_back("поканальная эквализация");
                if (composite.invert)
                    transformations.push_back("инверсия");
                if (composite.normalize)
                    transformations.push_back("нормализация");
                if (composite.white_balance)
                    transformations.push_back("баланс белого");
                if (composite.apply_lut)
                    transformations.push_back("цветовая LUT");
                if (!transformations.empty())
                    legend.notes.push_back("Визуальная обработка: " + join(transformations, ", ") + ".");
                return legend;
            }

            void default_continuous_stops(LegendSpec &legend)
            {
                legend.color_stops = {
                    {0.00, {0.105882, 0.094118, 0.266667}},
                    {0.25, {0.117647, 0.368627, 0.545098}},
                    {0.50, {0.141176, 0.627451, 0.576471}},
                    {0.75, {0.588235, 0.788235, 0.349020}},
                    {1.00, {0.988235, 0.905882, 0.145098}}};
            }

            void read_explicit_components(const nlohmann::json &legend_json, LegendSpec &legend)
            {
                if (!legend_json.contains("components") || !legend_json["components"].is_array())
                    return;

                legend.components.clear();
                for (const nlohmann::json &component_json : legend_json["components"])
                {
                    if (!component_json.is_object())
                        continue;
                    CompositeComponent component;
                    component.component = component_json.value("component", "IN");
                    component.channel = component_json.value("channel", "");
                    component.spectral_range = component_json.value("spectral_range", "");
                    component.quantity = component_json.value("quantity", "");
                    component.formula = component_json.value("formula", "");
                    component.description = component_json.value("description", "");
                    if (component_json.contains("color"))
                        component.marker_color = parse_color(component_json["color"], component.marker_color);
                    legend.components.push_back(component);
                }
            }

            LegendSpec explicit_legend(const nlohmann::json &legend_json, LegendSpec fallback)
            {
                if (!legend_json.is_object())
                    return fallback;

                const std::string kind = lowercase(legend_json.value("kind", ""));
                if (kind == "continuous")
                    fallback.kind = LegendKind::Continuous;
                else if (kind == "categorical" || kind == "discrete")
                    fallback.kind = LegendKind::Categorical;
                else if (kind == "composite" || kind == "rgb")
                    fallback.kind = LegendKind::Composite;
                else if (kind == "none")
                    fallback.kind = LegendKind::None;

                if (legend_json.contains("title") && legend_json["title"].is_string())
                    fallback.title = legend_json["title"].get<std::string>();
                if (legend_json.contains("subtitle") && legend_json["subtitle"].is_string())
                    fallback.subtitle = legend_json["subtitle"].get<std::string>();
                if (legend_json.contains("unit") && legend_json["unit"].is_string())
                    fallback.unit = legend_json["unit"].get<std::string>();

                if (fallback.kind == LegendKind::Continuous)
                {
                    fallback.components.clear();
                    fallback.categories.clear();
                    fallback.color_stops.clear();
                    if (legend_json.contains("colors") && legend_json["colors"].is_array())
                    {
                        const nlohmann::json &colors = legend_json["colors"];
                        for (size_t index = 0; index < colors.size(); index++)
                        {
                            ColorStop stop;
                            stop.position = colors.size() <= 1 ? 0.0 : (double)index / (double)(colors.size() - 1);
                            if (colors[index].is_object())
                            {
                                stop.position = colors[index].value("position", stop.position);
                                if (colors[index].contains("color"))
                                    stop.color = parse_color(colors[index]["color"], {0.5, 0.5, 0.5});
                            }
                            else
                            {
                                stop.color = parse_color(colors[index], {0.5, 0.5, 0.5});
                            }
                            fallback.color_stops.push_back(stop);
                        }
                    }
                    if (fallback.color_stops.empty())
                        default_continuous_stops(fallback);

                    fallback.ticks.clear();
                    const double minimum = legend_json.value("min", 0.0);
                    const double maximum = legend_json.value("max", 1.0);
                    if (legend_json.contains("ticks") && legend_json["ticks"].is_array())
                    {
                        for (const nlohmann::json &tick_json : legend_json["ticks"])
                        {
                            LegendTick tick;
                            if (tick_json.is_number())
                            {
                                const double value = tick_json.get<double>();
                                tick.position = maximum == minimum ? 0.0 : (value - minimum) / (maximum - minimum);
                                tick.label = format_number(value, 1);
                            }
                            else if (tick_json.is_object())
                            {
                                if (tick_json.contains("position"))
                                    tick.position = tick_json["position"].get<double>();
                                else if (tick_json.contains("value") && maximum != minimum)
                                    tick.position = (tick_json["value"].get<double>() - minimum) / (maximum - minimum);
                                if (tick_json.contains("label") && tick_json["label"].is_string())
                                    tick.label = tick_json["label"].get<std::string>();
                                else if (tick_json.contains("value"))
                                    tick.label = format_number(tick_json["value"].get<double>(), 1);
                            }
                            fallback.ticks.push_back(tick);
                        }
                    }
                    if (fallback.ticks.empty())
                    {
                        const int count = std::max(2, legend_json.value("tick_count", 8));
                        for (int index = 0; index < count; index++)
                        {
                            const double position = count <= 1 ? 0.0 : (double)index / (double)(count - 1);
                            fallback.ticks.push_back({position, format_number(minimum + (maximum - minimum) * position, 1)});
                        }
                    }
                }
                else if (fallback.kind == LegendKind::Categorical)
                {
                    fallback.components.clear();
                    fallback.color_stops.clear();
                    fallback.ticks.clear();
                    fallback.categories.clear();
                    if (legend_json.contains("categories") && legend_json["categories"].is_array())
                    {
                        for (const nlohmann::json &entry_json : legend_json["categories"])
                        {
                            if (!entry_json.is_object())
                                continue;
                            CategoryEntry entry;
                            entry.label = entry_json.value("label", "Без названия");
                            if (entry_json.contains("color"))
                                entry.color = parse_color(entry_json["color"], {0.5, 0.5, 0.5});
                            fallback.categories.push_back(entry);
                        }
                    }
                }
                else if (fallback.kind == LegendKind::Composite)
                {
                    fallback.color_stops.clear();
                    fallback.ticks.clear();
                    fallback.categories.clear();
                    read_explicit_components(legend_json, fallback);
                }
                else
                {
                    fallback.color_stops.clear();
                    fallback.ticks.clear();
                    fallback.categories.clear();
                    fallback.components.clear();
                }

                if (legend_json.contains("notes") && legend_json["notes"].is_array())
                {
                    fallback.notes.clear();
                    for (const nlohmann::json &note : legend_json["notes"])
                        if (note.is_string())
                            fallback.notes.push_back(note.get<std::string>());
                }
                return fallback;
            }

            std::tm utc_tm(time_t timestamp)
            {
                std::tm result{};
#ifdef _WIN32
                gmtime_s(&result, &timestamp);
#else
                gmtime_r(&timestamp, &result);
#endif
                return result;
            }

            std::string format_utc(double timestamp, const char *format)
            {
                if (!std::isfinite(timestamp) || timestamp <= 0.0)
                    return "";
                const time_t integral = (time_t)std::llround(timestamp);
                const std::tm value = utc_tm(integral);
                std::ostringstream stream;
                stream << std::put_time(&value, format);
                return stream.str();
            }

            std::pair<double, double> timestamp_range(const std::vector<double> &timestamps, ImageProducts &products)
            {
                const double anchor = products.has_product_timestamp() ? (double)products.get_product_timestamp() : NAN;
                constexpr double maximum_distance = 6.0 * 60.0 * 60.0;
                double minimum = INFINITY;
                double maximum = -INFINITY;
                for (double timestamp : timestamps)
                {
                    if (std::isfinite(timestamp) && timestamp > 0.0 &&
                        (!std::isfinite(anchor) || std::fabs(timestamp - anchor) <= maximum_distance))
                    {
                        minimum = std::min(minimum, timestamp);
                        maximum = std::max(maximum, timestamp);
                    }
                }
                if (!std::isfinite(minimum) && std::isfinite(anchor))
                    minimum = maximum = anchor;
                return {minimum, maximum};
            }

            std::string format_interval(const std::pair<double, double> &range)
            {
                if (!std::isfinite(range.first))
                    return "Время наблюдения не указано";
                if (!std::isfinite(range.second) || std::fabs(range.second - range.first) < 0.5)
                    return format_utc(range.first, "%d.%m.%Y · %H:%M:%S UTC");

                const std::string first_date = format_utc(range.first, "%d.%m.%Y");
                const std::string second_date = format_utc(range.second, "%d.%m.%Y");
                if (first_date == second_date)
                    return first_date + " · " + format_utc(range.first, "%H:%M:%S") + "–" + format_utc(range.second, "%H:%M:%S") + " UTC";
                return format_utc(range.first, "%d.%m.%Y %H:%M:%S") + " – " + format_utc(range.second, "%d.%m.%Y %H:%M:%S") + " UTC";
            }

            std::string frequency_label(const std::string &raw)
            {
                if (raw.empty())
                    return "";
                try
                {
                    const double value = std::stod(raw);
                    if (value >= 1e9)
                        return format_number(value / 1e9, 4) + " ГГц";
                    if (value >= 1e6)
                        return format_number(value / 1e6, 4) + " МГц";
                    if (value >= 1e3)
                        return format_number(value / 1e3, 2) + " кГц";
                    return format_number(value, 0) + " Гц";
                }
                catch (const std::exception &)
                {
                    return raw;
                }
            }

            std::string satellite_name(ImageProducts &products)
            {
                if (products.has_product_source())
                {
                    const std::string source = products.get_product_source();
                    if (!source.empty())
                        return source;
                }
                if (products.has_tle() && !products.get_tle().name.empty())
                    return products.get_tle().name;
                return "Спутниковый продукт";
            }

            std::vector<std::string> used_channels(ImageProducts &products, const ImageCompositeCfg &composite)
            {
                const std::string source = !composite.channels.empty() ? composite.channels : composite.equation;
                return extract_channel_tokens(products, source);
            }

            std::string channel_summary(ImageProducts &products, const ImageCompositeCfg &composite)
            {
                std::vector<std::string> values;
                for (const std::string &token : used_channels(products, composite))
                {
                    const int index = channel_index(products, token);
                    if (index < 0)
                        continue;
                    std::string value = products.images[index].channel_name;
                    const std::string wavelength = wavelength_label(products, index);
                    if (!wavelength.empty())
                        value += " (" + wavelength + ")";
                    values.push_back(value);
                }
                return join(values, ", ");
            }

            nlohmann::json spec_to_json(const PresentationSpec &spec)
            {
                nlohmann::json output;
                output["schema"] = "satdump.presentation/1";
                output["pass"] = {
                    {"satellite", spec.pass.satellite},
                    {"instrument", spec.pass.instrument},
                    {"product", spec.pass.product},
                    {"acquisition_time", spec.pass.acquisition_time},
                    {"pass_summary", spec.pass.pass_summary},
                    {"quality", spec.pass.quality},
                    {"quality_detail", spec.pass.quality_detail}};
                for (const MetadataField &field : spec.pass.details)
                    output["pass"]["details"].push_back({{"label", field.label}, {"value", field.value}});

                std::string kind = "none";
                if (spec.legend.kind == LegendKind::Continuous)
                    kind = "continuous";
                else if (spec.legend.kind == LegendKind::Categorical)
                    kind = "categorical";
                else if (spec.legend.kind == LegendKind::Composite)
                    kind = "composite";

                output["legend"] = {
                    {"kind", kind},
                    {"title", spec.legend.title},
                    {"subtitle", spec.legend.subtitle},
                    {"unit", spec.legend.unit},
                    {"notes", spec.legend.notes}};
                for (const ColorStop &stop : spec.legend.color_stops)
                    output["legend"]["color_stops"].push_back({{"position", stop.position}, {"color", stop.color}});
                for (const LegendTick &tick : spec.legend.ticks)
                    output["legend"]["ticks"].push_back({{"position", tick.position}, {"label", tick.label}});
                for (const CategoryEntry &entry : spec.legend.categories)
                    output["legend"]["categories"].push_back({{"color", entry.color}, {"label", entry.label}});
                for (const CompositeComponent &component : spec.legend.components)
                    output["legend"]["components"].push_back({
                        {"component", component.component},
                        {"channel", component.channel},
                        {"spectral_range", component.spectral_range},
                        {"quantity", component.quantity},
                        {"formula", component.formula},
                        {"description", component.description}});
                output["branding"] = spec.branding;
                return output;
            }
        }

        bool enabled(const nlohmann::json &composite_preset)
        {
            try
            {
                if (composite_preset.is_object() && composite_preset.contains("presentation"))
                {
                    const nlohmann::json &presentation = composite_preset["presentation"];
                    if (presentation.is_boolean())
                        return presentation.get<bool>();
                    if (presentation.is_object() && presentation.contains("enabled"))
                        return bool_value(presentation["enabled"], true);
                }
                if (config::main_cfg.contains("satdump_general") && config::main_cfg["satdump_general"].contains("presentation_enabled"))
                    return bool_value(config::main_cfg["satdump_general"]["presentation_enabled"], true);
            }
            catch (const std::exception &)
            {
            }
            return true;
        }

        PresentationSpec build_spec(ImageProducts &products,
                                    const ImageCompositeCfg &composite,
                                    const nlohmann::json &composite_preset,
                                    const std::string &product_name,
                                    const std::vector<double> &timestamps,
                                    const nlohmann::json &product_metadata,
                                    const std::string &source_variant)
        {
            PresentationSpec spec;
            spec.branding = "SatDump 1.2.2 · Presentation";
            spec.pass.satellite = satellite_name(products);
            spec.pass.instrument = products.instrument_name;
            spec.pass.product = product_name;
            spec.pass.acquisition_time = format_interval(timestamp_range(timestamps, products));

            const std::string direction = first_path_string(products.contents, product_metadata,
                                                             {{"acquisition", "pass", "direction"}, {"pass", "direction"}, {"pass_direction"}});
            const std::string maximum_elevation = first_path_string(products.contents, product_metadata,
                                                                     {{"acquisition", "pass", "max_elevation_deg"}, {"pass", "max_elevation_deg"}, {"max_elevation_deg"}});
            std::vector<std::string> pass_parts;
            if (!direction.empty())
                pass_parts.push_back(direction);
            if (!maximum_elevation.empty())
                pass_parts.push_back("макс. высота " + maximum_elevation + "°");
            spec.pass.pass_summary = join(pass_parts, " · ");

            if (products.has_tle() && products.get_tle().norad > 0)
                spec.pass.details.push_back({"NORAD", std::to_string(products.get_tle().norad)});

            const std::string channels = channel_summary(products, composite);
            if (!channels.empty())
                spec.pass.details.push_back({"Каналы", channels});

            const std::string downlink = first_path_string(products.contents, product_metadata,
                                                            {{"acquisition", "downlink", "center_frequency_hz"},
                                                             {"reception", "center_frequency_hz"},
                                                             {"downlink_frequency_hz"},
                                                             {"frequency_hz"}});
            if (!downlink.empty())
                spec.pass.details.push_back({"Приём", frequency_label(downlink)});

            const std::string sample_rate = first_path_string(products.contents, product_metadata,
                                                               {{"acquisition", "downlink", "sample_rate_hz"},
                                                                {"reception", "sample_rate_hz"},
                                                                {"sample_rate_hz"},
                                                                {"samplerate"}});
            if (!sample_rate.empty())
                spec.pass.details.push_back({"Дискретизация", frequency_label(sample_rate)});

            try
            {
                if (products.has_proj_cfg() && products.get_proj_cfg().contains("type"))
                    spec.pass.details.push_back({"Проекция", scalar_to_string(products.get_proj_cfg()["type"])});
            }
            catch (const std::exception &)
            {
            }
            if (!source_variant.empty())
                spec.pass.details.push_back({"Вариант", source_variant});

            spec.pass.quality = first_path_string(products.contents, product_metadata,
                                                  {{"quality", "score"}, {"quality_score"}});
            const std::string packet_loss = first_path_string(products.contents, product_metadata,
                                                               {{"quality", "packet_loss_percent"}, {"packet_loss_percent"}});
            const std::string snr = first_path_string(products.contents, product_metadata,
                                                       {{"quality", "snr_db"}, {"snr_db"}});
            std::vector<std::string> quality_parts;
            if (!packet_loss.empty())
                quality_parts.push_back("потери " + packet_loss + "%");
            if (!snr.empty())
                quality_parts.push_back("SNR " + snr + " дБ");
            spec.pass.quality_detail = join(quality_parts, " · ");
            if (!spec.pass.quality.empty() && spec.pass.quality.find('%') == std::string::npos)
                spec.pass.quality += "%";

            spec.legend = automatic_composite_legend(products, composite);

            if (composite_preset.is_object() && composite_preset.contains("presentation") && composite_preset["presentation"].is_object())
            {
                const nlohmann::json &presentation = composite_preset["presentation"];
                if (presentation.contains("title") && presentation["title"].is_string())
                    spec.pass.product = presentation["title"].get<std::string>();
                if (presentation.contains("subtitle") && presentation["subtitle"].is_string())
                    spec.legend.subtitle = presentation["subtitle"].get<std::string>();
                if (presentation.contains("branding") && presentation["branding"].is_string())
                    spec.branding = presentation["branding"].get<std::string>();
                if (presentation.contains("legend"))
                    spec.legend = explicit_legend(presentation["legend"], spec.legend);

                if (presentation.contains("theme") && presentation["theme"].is_object())
                {
                    const nlohmann::json &theme = presentation["theme"];
                    if (theme.contains("panel"))
                        spec.theme.panel = parse_color(theme["panel"], spec.theme.panel);
                    if (theme.contains("panel_secondary"))
                        spec.theme.panel_secondary = parse_color(theme["panel_secondary"], spec.theme.panel_secondary);
                    if (theme.contains("text"))
                        spec.theme.text = parse_color(theme["text"], spec.theme.text);
                    if (theme.contains("muted_text"))
                        spec.theme.muted_text = parse_color(theme["muted_text"], spec.theme.muted_text);
                    if (theme.contains("accent"))
                        spec.theme.accent = parse_color(theme["accent"], spec.theme.accent);
                }
            }
            return spec;
        }

        bool save(const image::Image &source,
                  image::TextDrawer &text_drawer,
                  const PresentationSpec &spec,
                  const std::string &png_path)
        {
            if (source.size() == 0)
                return false;
            if (!text_drawer.font_ready())
            {
                logger->error("Presentation renderer font is not initialized; skipping %s", png_path.c_str());
                return false;
            }

            try
            {
                image::Image presented = image::presentation::render(source, text_drawer, spec);
                image::save_img(presented, png_path);

                std::filesystem::path sidecar_path(png_path);
                sidecar_path.replace_extension(".json");
                std::ofstream sidecar(sidecar_path.string());
                sidecar << spec_to_json(spec).dump(4);
                sidecar.close();

                logger->info("Saved presentation product %s", png_path.c_str());
                return true;
            }
            catch (const std::exception &error)
            {
                logger->error("Could not save presentation product %s: %s", png_path.c_str(), error.what());
                return false;
            }
        }
    }
}
