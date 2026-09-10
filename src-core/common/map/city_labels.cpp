#include "city_labels.h"
#include "city_name_resolver.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>

namespace map
{
    namespace
    {
        using json = nlohmann::json;

        struct CityCandidate
        {
            std::string label;
            double longitude = 0.0;
            double latitude = 0.0;
            double population = 0.0;
            int scale_rank = 99;
            int x = -1;
            int y = -1;
            int priority = 0;
            bool admin0_capital = false;
            bool admin1_capital = false;
            bool world_city = false;
        };

        struct PlacedLabel
        {
            int text_x = 0;
            int text_y = 0;
            LabelBox box;
        };

        std::string lowercase_ascii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                           { return character < 128 ? (char)std::tolower(character) : (char)character; });
            return value;
        }

        const json *property_ci(const json &properties, const std::string &name)
        {
            if (!properties.is_object())
                return nullptr;
            auto direct = properties.find(name);
            if (direct != properties.end())
                return &(*direct);
            const std::string target = lowercase_ascii(name);
            for (auto iterator = properties.begin(); iterator != properties.end(); ++iterator)
                if (lowercase_ascii(iterator.key()) == target)
                    return &iterator.value();
            return nullptr;
        }

        std::string property_string(const json &properties, const std::string &name)
        {
            const json *value = property_ci(properties, name);
            if (value == nullptr || value->is_null())
                return "";
            try
            {
                if (value->is_string())
                    return value->get<std::string>();
                if (value->is_number_integer())
                    return std::to_string(value->get<long long>());
                if (value->is_number_unsigned())
                    return std::to_string(value->get<unsigned long long>());
                if (value->is_number_float())
                    return std::to_string(value->get<double>());
            }
            catch (const std::exception &)
            {
            }
            return "";
        }

        double property_number(const json &properties, const std::string &name, double fallback)
        {
            const json *value = property_ci(properties, name);
            if (value == nullptr || value->is_null())
                return fallback;
            try
            {
                if (value->is_number())
                    return value->get<double>();
                if (value->is_string() && !value->get<std::string>().empty())
                    return std::stod(value->get<std::string>());
            }
            catch (const std::exception &)
            {
            }
            return fallback;
        }

        bool property_bool(const json &properties, const std::string &name)
        {
            const json *value = property_ci(properties, name);
            if (value == nullptr || value->is_null())
                return false;
            try
            {
                if (value->is_boolean())
                    return value->get<bool>();
                if (value->is_number())
                    return std::fabs(value->get<double>()) > 0.5;
                if (value->is_string())
                {
                    const std::string text = lowercase_ascii(value->get<std::string>());
                    return text == "1" || text == "true" || text == "yes" || text == "y";
                }
            }
            catch (const std::exception &)
            {
            }
            return false;
        }

        std::string resolve_label(const json &properties, const CityLabelStyle &style)
        {
            return resolve_city_name(properties, style.locale, style.label_field, style.fallback_fields);
        }

        bool base_filter(const CityCandidate &candidate, const CityLabelStyle &style)
        {
            if (style.cities_type == 0)
                return candidate.admin0_capital;
            if (style.cities_type == 1)
                return candidate.admin0_capital || candidate.admin1_capital;
            return candidate.scale_rank <= style.scale_rank;
        }

        double longitude_span(const std::vector<CityCandidate> &candidates)
        {
            if (candidates.size() < 2)
                return 0.0;
            std::vector<double> values;
            for (const CityCandidate &candidate : candidates)
            {
                double longitude = std::fmod(candidate.longitude + 360.0, 360.0);
                if (longitude < 0.0)
                    longitude += 360.0;
                values.push_back(longitude);
            }
            std::sort(values.begin(), values.end());
            double largest_gap = values.front() + 360.0 - values.back();
            for (size_t index = 1; index < values.size(); index++)
                largest_gap = std::max(largest_gap, values[index] - values[index - 1]);
            return std::max(0.0, 360.0 - largest_gap);
        }

        std::string resolve_mode(const CityLabelStyle &style, const std::vector<CityCandidate> &candidates)
        {
            const std::string requested = lowercase_ascii(style.detail_mode);
            if (requested == "world" || requested == "global" || requested == "capitals")
                return "world";
            if (requested == "continent" || requested == "continental" || requested == "major")
                return "continent";
            if (requested == "regional" || requested == "region")
                return "regional";
            if (requested == "local" || requested == "all")
                return "local";
            if (candidates.size() < 2)
                return "local";

            double minimum_latitude = candidates.front().latitude;
            double maximum_latitude = candidates.front().latitude;
            for (const CityCandidate &candidate : candidates)
            {
                minimum_latitude = std::min(minimum_latitude, candidate.latitude);
                maximum_latitude = std::max(maximum_latitude, candidate.latitude);
            }
            const double lat_span = maximum_latitude - minimum_latitude;
            const double lon_span = longitude_span(candidates);
            if (lon_span >= 140.0 || lat_span >= 75.0)
                return "world";
            if (lon_span >= 65.0 || lat_span >= 40.0)
                return "continent";
            if (lon_span >= 25.0 || lat_span >= 18.0)
                return "regional";
            return "local";
        }

        bool mode_filter(const CityCandidate &candidate, const std::string &mode)
        {
            if (mode == "world")
                return candidate.admin0_capital;
            if (mode == "continent")
                return candidate.admin0_capital ||
                       (candidate.world_city && candidate.scale_rank <= 3) ||
                       candidate.population >= 1500000.0;
            if (mode == "regional")
                return candidate.admin0_capital || candidate.admin1_capital ||
                       candidate.world_city || candidate.scale_rank <= 4 ||
                       candidate.population >= 300000.0;
            return true;
        }

        int effective_limit(const CityLabelStyle &style, const std::string &mode, int width)
        {
            const int configured = style.max_labels <= 0 ? std::numeric_limits<int>::max() : style.max_labels;
            if (mode == "world")
                return std::min(configured, std::clamp(width / 45, 24, 80));
            if (mode == "continent")
                return std::min(configured, std::clamp(width / 32, 40, 120));
            if (mode == "regional")
                return std::min(configured, std::clamp(width / 23, 60, 180));
            return configured;
        }

        int candidate_priority(const CityCandidate &candidate, bool prioritize_capitals)
        {
            int priority = 0;
            if (prioritize_capitals)
            {
                if (candidate.admin0_capital)
                    priority += 1000000;
                else if (candidate.admin1_capital)
                    priority += 500000;
            }
            if (candidate.world_city)
                priority += 200000;
            priority += std::max(0, 20 - candidate.scale_rank) * 5000;
            if (candidate.population > 0.0)
                priority += (int)std::min(99999.0, std::log10(candidate.population + 1.0) * 10000.0);
            return priority;
        }

        LabelBox combined_box(int city_x, int city_y, int marker_radius,
                              int text_x, int text_y, int text_width, int text_height,
                              int outline_width)
        {
            LabelBox result;
            result.left = std::min(city_x - marker_radius, text_x) - outline_width;
            result.top = std::min(city_y - marker_radius, text_y) - outline_width;
            result.right = std::max(city_x + marker_radius + 1, text_x + text_width) + outline_width;
            result.bottom = std::max(city_y + marker_radius + 1, text_y + text_height) + outline_width;
            return result;
        }

        bool collides(const LabelBox &box, const std::vector<LabelBox> &occupied, int padding)
        {
            for (const LabelBox &other : occupied)
                if (label_boxes_intersect(box, other, padding))
                    return true;
            return false;
        }

        bool place_label(const CityCandidate &candidate, image::TextDrawer &text_drawer,
                         const CityLabelStyle &style, int width, int height,
                         const std::vector<LabelBox> &occupied, PlacedLabel &placed)
        {
            const image::TextSize measured = text_drawer.measure_text(style.font_size, candidate.label);
            const int text_width = std::max(1, measured.width);
            const int text_height = std::max(style.font_size, std::max(measured.height, measured.line_height));
            const int radius = std::max(1, style.marker_radius);
            const int gap = std::max(3, radius + style.outline_width + 2);
            const int half_height = text_height / 2;
            const std::vector<std::pair<int, int>> positions = {
                {candidate.x + radius + gap, candidate.y - half_height},
                {candidate.x - radius - gap - text_width, candidate.y - half_height},
                {candidate.x - text_width / 2, candidate.y - radius - gap - text_height},
                {candidate.x - text_width / 2, candidate.y + radius + gap},
                {candidate.x + radius + gap, candidate.y - radius - gap - text_height},
                {candidate.x + radius + gap, candidate.y + radius + gap}};

            for (const auto &position : positions)
            {
                LabelBox box = combined_box(candidate.x, candidate.y, radius,
                                            position.first, position.second,
                                            text_width, text_height, style.outline_width);
                if (box.left < 0 || box.top < 0 || box.right > width || box.bottom > height)
                    continue;
                if (style.avoid_overlap && collides(box, occupied, style.collision_padding))
                    continue;
                placed.text_x = position.first;
                placed.text_y = position.second;
                placed.box = box;
                return true;
            }
            return false;
        }

        void draw_outlined_label(image::Image &fill_mask, image::Image &outline_mask,
                                 image::TextDrawer &text_drawer, const CityCandidate &candidate,
                                 const PlacedLabel &placed, const CityLabelStyle &style)
        {
            const int marker_radius = std::max(1, style.marker_radius);
            const int outline_width = std::max(0, style.outline_width);
            if (outline_width > 0)
            {
                outline_mask.draw_circle(candidate.x, candidate.y,
                                         marker_radius + outline_width, {1}, true);
                for (int y = -outline_width; y <= outline_width; y++)
                    for (int x = -outline_width; x <= outline_width; x++)
                        if (x * x + y * y <= outline_width * outline_width)
                            text_drawer.draw_text(outline_mask, placed.text_x + x, placed.text_y + y,
                                                  {1}, style.font_size, candidate.label);
            }
            fill_mask.draw_circle(candidate.x, candidate.y, marker_radius, {1}, true);
            text_drawer.draw_text(fill_mask, placed.text_x, placed.text_y,
                                  {1}, style.font_size, candidate.label);
        }
    }

    bool label_boxes_intersect(const LabelBox &left, const LabelBox &right, int padding)
    {
        return left.left < right.right + padding && left.right + padding > right.left &&
               left.top < right.bottom + padding && left.bottom + padding > right.top;
    }

    CityLabelStats drawProjectedCitiesGeoJsonStyled(
        const std::vector<std::string> &json_files,
        image::Image &fill_mask,
        image::Image &outline_mask,
        image::TextDrawer &text_drawer,
        std::function<std::pair<int, int>(double, double, int, int)> projection_func,
        const CityLabelStyle &style,
        const std::vector<LabelBox> &reserved_boxes)
    {
        CityLabelStats stats;
        if (!text_drawer.font_ready() || fill_mask.size() == 0 || outline_mask.size() == 0)
            return stats;

        std::vector<CityCandidate> projected_candidates;
        for (const std::string &json_file : json_files)
        {
            json document;
            try
            {
                std::ifstream input(json_file);
                if (!input.good())
                    continue;
                input >> document;
            }
            catch (const std::exception &)
            {
                continue;
            }
            if (!document.is_object() || !document.contains("features") || !document["features"].is_array())
                continue;

            for (const json &feature : document["features"])
            {
                if (!feature.is_object() || feature.value("type", "") != "Feature" ||
                    !feature.contains("geometry") || !feature["geometry"].is_object() ||
                    feature["geometry"].value("type", "") != "Point" ||
                    !feature["geometry"].contains("coordinates") ||
                    !feature["geometry"]["coordinates"].is_array() ||
                    feature["geometry"]["coordinates"].size() < 2)
                    continue;

                stats.candidates++;
                const json properties = feature.value("properties", json::object());
                CityCandidate candidate;
                try
                {
                    candidate.longitude = feature["geometry"]["coordinates"][0].get<double>();
                    candidate.latitude = feature["geometry"]["coordinates"][1].get<double>();
                }
                catch (const std::exception &)
                {
                    stats.skipped_filter++;
                    continue;
                }

                candidate.label = resolve_label(properties, style);
                const std::string feature_class = lowercase_ascii(property_string(properties, "featurecla"));
                candidate.admin0_capital = feature_class.find("admin-0 capital") != std::string::npos ||
                                           property_bool(properties, "adm0cap");
                candidate.admin1_capital = feature_class.find("admin-1 capital") != std::string::npos ||
                                           property_bool(properties, "adm1cap");
                candidate.world_city = property_bool(properties, "worldcity") || property_bool(properties, "megacity");
                candidate.scale_rank = (int)std::round(property_number(properties, "scalerank", 99.0));
                candidate.population = property_number(properties, "pop_max", property_number(properties, "popmax", 0.0));
                if (candidate.label.empty() || !base_filter(candidate, style))
                {
                    stats.skipped_filter++;
                    continue;
                }

                const std::pair<int, int> point = projection_func(
                    candidate.latitude, candidate.longitude,
                    (int)fill_mask.height(), (int)fill_mask.width());
                if (point.first < 0 || point.second < 0 ||
                    point.first >= (int)fill_mask.width() || point.second >= (int)fill_mask.height())
                    continue;
                candidate.x = point.first;
                candidate.y = point.second;
                candidate.priority = candidate_priority(candidate, style.prioritize_capitals);
                projected_candidates.push_back(candidate);
                stats.projected++;
            }
        }

        stats.resolved_mode = resolve_mode(style, projected_candidates);
        std::vector<CityCandidate> visible;
        for (const CityCandidate &candidate : projected_candidates)
            if (mode_filter(candidate, stats.resolved_mode))
                visible.push_back(candidate);
            else
                stats.skipped_filter++;

        std::sort(visible.begin(), visible.end(), [](const CityCandidate &left, const CityCandidate &right)
                  {
                      if (left.priority != right.priority)
                          return left.priority > right.priority;
                      if (left.population != right.population)
                          return left.population > right.population;
                      return left.label < right.label;
                  });

        const int limit = effective_limit(style, stats.resolved_mode, (int)fill_mask.width());
        std::vector<LabelBox> occupied = reserved_boxes;
        std::set<std::string> used_labels;
        for (size_t candidate_index = 0; candidate_index < visible.size(); candidate_index++)
        {
            const CityCandidate &candidate = visible[candidate_index];
            if (stats.drawn >= limit)
            {
                stats.skipped_limit += (int)(visible.size() - candidate_index);
                break;
            }
            if (!used_labels.insert(lowercase_ascii(candidate.label)).second)
                continue;
            PlacedLabel placed;
            if (!place_label(candidate, text_drawer, style,
                             (int)fill_mask.width(), (int)fill_mask.height(), occupied, placed))
            {
                stats.skipped_overlap++;
                continue;
            }
            draw_outlined_label(fill_mask, outline_mask, text_drawer, candidate, placed, style);
            occupied.push_back(placed.box);
            stats.drawn++;
            stats.drawn_labels.push_back(candidate.label);
        }
        return stats;
    }
}
