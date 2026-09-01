#include "city_labels.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_map>

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

        std::string normalize_key(const std::string &value)
        {
            std::string output;
            bool previous_space = false;
            for (unsigned char character : value)
            {
                if (character < 128 && (std::isalnum(character) || character == '-'))
                {
                    output.push_back((char)std::tolower(character));
                    previous_space = false;
                }
                else if (character < 128)
                {
                    if (!output.empty() && !previous_space)
                    {
                        output.push_back(' ');
                        previous_space = true;
                    }
                }
                else
                {
                    output.push_back((char)character);
                    previous_space = false;
                }
            }
            while (!output.empty() && output.back() == ' ')
                output.pop_back();
            return output;
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

        bool contains_cyrillic(const std::string &value)
        {
            for (size_t index = 0; index + 1 < value.size(); index++)
            {
                const unsigned char first = (unsigned char)value[index];
                const unsigned char second = (unsigned char)value[index + 1];
                if ((first == 0xD0 && second >= 0x90) || first == 0xD1)
                    return true;
            }
            return false;
        }

        const std::unordered_map<std::string, std::string> &russian_names()
        {
            static const std::unordered_map<std::string, std::string> names = {
                {"moscow", "Москва"}, {"st petersburg", "Санкт-Петербург"},
                {"saint petersburg", "Санкт-Петербург"}, {"murmansk", "Мурманск"},
                {"dudinka", "Дудинка"}, {"naryan mar", "Нарьян-Мар"},
                {"naryan-mar", "Нарьян-Мар"}, {"salekhard", "Салехард"},
                {"khanty mansiysk", "Ханты-Мансийск"}, {"khanty-mansiysk", "Ханты-Мансийск"},
                {"archangel", "Архангельск"}, {"arkhangelsk", "Архангельск"},
                {"vologda", "Вологда"}, {"petrozavodsk", "Петрозаводск"},
                {"syktyvkar", "Сыктывкар"}, {"perm", "Пермь"},
                {"yekaterinburg", "Екатеринбург"}, {"ekaterinburg", "Екатеринбург"},
                {"tyumen", "Тюмень"}, {"kurgan", "Курган"},
                {"chelyabinsk", "Челябинск"}, {"ufa", "Уфа"},
                {"kazan", "Казань"}, {"samara", "Самара"},
                {"saratov", "Саратов"}, {"volgograd", "Волгоград"},
                {"astrakhan", "Астрахань"}, {"rostov", "Ростов-на-Дону"},
                {"rostov on don", "Ростов-на-Дону"}, {"rostov-na-donu", "Ростов-на-Дону"},
                {"krasnodar", "Краснодар"}, {"stavropol", "Ставрополь"},
                {"makhachkala", "Махачкала"}, {"grozny", "Грозный"},
                {"sochi", "Сочи"}, {"orenburg", "Оренбург"},
                {"izhevsk", "Ижевск"}, {"kirov", "Киров"},
                {"nizhny novgorod", "Нижний Новгород"}, {"yoshkar ola", "Йошкар-Ола"},
                {"yoshkar-ola", "Йошкар-Ола"}, {"cheboksary", "Чебоксары"},
                {"ulyanovsk", "Ульяновск"}, {"saransk", "Саранск"},
                {"penza", "Пенза"}, {"tambov", "Тамбов"},
                {"ryazan", "Рязань"}, {"tula", "Тула"},
                {"kaluga", "Калуга"}, {"smolensk", "Смоленск"},
                {"tver", "Тверь"}, {"yaroslavl", "Ярославль"},
                {"ivanovo", "Иваново"}, {"vladimir", "Владимир"},
                {"kursk", "Курск"}, {"belgorod", "Белгород"},
                {"voronezh", "Воронеж"}, {"lipetsk", "Липецк"},
                {"bryansk", "Брянск"}, {"oryol", "Орёл"}, {"orel", "Орёл"},
                {"pskov", "Псков"}, {"velikiy novgorod", "Великий Новгород"},
                {"veliky novgorod", "Великий Новгород"}, {"kaliningrad", "Калининград"},
                {"novosibirsk", "Новосибирск"}, {"omsk", "Омск"},
                {"tomsk", "Томск"}, {"kemerovo", "Кемерово"},
                {"krasnoyarsk", "Красноярск"}, {"irkutsk", "Иркутск"},
                {"yakutsk", "Якутск"}, {"magadan", "Магадан"},
                {"vladivostok", "Владивосток"}, {"khabarovsk", "Хабаровск"},
                {"helsinki", "Хельсинки"}, {"tallinn", "Таллин"},
                {"riga", "Рига"}, {"vilnius", "Вильнюс"}, {"minsk", "Минск"},
                {"kiev", "Киев"}, {"kyiv", "Киев"}, {"oslo", "Осло"},
                {"stockholm", "Стокгольм"}, {"copenhagen", "Копенгаген"},
                {"berlin", "Берлин"}, {"warsaw", "Варшава"},
                {"prague", "Прага"}, {"vienna", "Вена"}, {"budapest", "Будапешт"},
                {"bucharest", "Бухарест"}, {"sofia", "София"},
                {"athens", "Афины"}, {"ankara", "Анкара"},
                {"istanbul", "Стамбул"}, {"tbilisi", "Тбилиси"},
                {"yerevan", "Ереван"}, {"baku", "Баку"},
                {"london", "Лондон"}, {"paris", "Париж"},
                {"madrid", "Мадрид"}, {"rome", "Рим"}, {"lisbon", "Лиссабон"},
                {"brussels", "Брюссель"}, {"amsterdam", "Амстердам"},
                {"dublin", "Дублин"}, {"reykjavik", "Рейкьявик"},
                {"washington d c", "Вашингтон"}, {"new york", "Нью-Йорк"},
                {"ottawa", "Оттава"}, {"mexico city", "Мехико"},
                {"havana", "Гавана"}, {"brasilia", "Бразилиа"},
                {"buenos aires", "Буэнос-Айрес"}, {"santiago", "Сантьяго"},
                {"lima", "Лима"}, {"beijing", "Пекин"}, {"tokyo", "Токио"},
                {"seoul", "Сеул"}, {"pyongyang", "Пхеньян"},
                {"ulaanbaatar", "Улан-Батор"}, {"new delhi", "Нью-Дели"},
                {"delhi", "Дели"}, {"tehran", "Тегеран"},
                {"baghdad", "Багдад"}, {"jerusalem", "Иерусалим"},
                {"cairo", "Каир"}, {"addis ababa", "Аддис-Абеба"},
                {"nairobi", "Найроби"}, {"pretoria", "Претория"},
                {"cape town", "Кейптаун"}, {"canberra", "Канберра"},
                {"sydney", "Сидней"}, {"wellington", "Веллингтон"}};
            return names;
        }

        std::string transliterate_to_russian(const std::string &input)
        {
            static const std::vector<std::pair<std::string, std::string>> groups = {
                {"shch", "щ"}, {"sch", "щ"}, {"yo", "ё"}, {"zh", "ж"},
                {"kh", "х"}, {"ts", "ц"}, {"ch", "ч"}, {"sh", "ш"},
                {"yu", "ю"}, {"ya", "я"}, {"ye", "е"}};
            static const std::unordered_map<char, std::string> letters = {
                {'a', "а"}, {'b', "б"}, {'c', "к"}, {'d', "д"}, {'e', "е"},
                {'f', "ф"}, {'g', "г"}, {'h', "х"}, {'i', "и"}, {'j', "дж"},
                {'k', "к"}, {'l', "л"}, {'m', "м"}, {'n', "н"}, {'o', "о"},
                {'p', "п"}, {'q', "к"}, {'r', "р"}, {'s', "с"}, {'t', "т"},
                {'u', "у"}, {'v', "в"}, {'w', "в"}, {'x', "кс"}, {'y', "ы"},
                {'z', "з"}};
            static const std::unordered_map<std::string, std::string> uppercase_letters = {
                {"а", "А"}, {"б", "Б"}, {"в", "В"}, {"г", "Г"}, {"д", "Д"},
                {"е", "Е"}, {"ё", "Ё"}, {"ж", "Ж"}, {"з", "З"}, {"и", "И"},
                {"к", "К"}, {"л", "Л"}, {"м", "М"}, {"н", "Н"}, {"о", "О"},
                {"п", "П"}, {"р", "Р"}, {"с", "С"}, {"т", "Т"}, {"у", "У"},
                {"ф", "Ф"}, {"х", "Х"}, {"ц", "Ц"}, {"ч", "Ч"}, {"ш", "Ш"},
                {"щ", "Щ"}, {"ы", "Ы"}, {"ю", "Ю"}, {"я", "Я"},
                {"дж", "Дж"}, {"кс", "Кс"}};

            std::string output;
            bool word_start = true;
            size_t index = 0;
            while (index < input.size())
            {
                const unsigned char raw = (unsigned char)input[index];
                if (raw >= 128)
                {
                    output.push_back((char)raw);
                    index++;
                    word_start = false;
                    continue;
                }
                if (!std::isalpha(raw))
                {
                    output.push_back((char)raw);
                    word_start = raw == ' ' || raw == '-' || raw == '/' || raw == '(';
                    index++;
                    continue;
                }

                const bool uppercase = std::isupper(raw) != 0;
                const std::string tail = lowercase_ascii(input.substr(index));
                std::string replacement;
                size_t consumed = 1;
                for (const auto &group : groups)
                    if (tail.rfind(group.first, 0) == 0)
                    {
                        replacement = group.second;
                        consumed = group.first.size();
                        break;
                    }
                if (replacement.empty())
                {
                    auto iterator = letters.find((char)std::tolower(raw));
                    replacement = iterator == letters.end() ? std::string(1, (char)raw) : iterator->second;
                }
                if ((uppercase || word_start) && !replacement.empty())
                {
                    auto upper = uppercase_letters.find(replacement);
                    if (upper != uppercase_letters.end())
                        replacement = upper->second;
                }
                output += replacement;
                word_start = false;
                index += consumed;
            }
            return output;
        }

        std::string resolve_label(const json &properties, const CityLabelStyle &style)
        {
            std::vector<std::string> fields;
            if (!style.label_field.empty())
                fields.push_back(style.label_field);
            if (lowercase_ascii(style.locale) == "ru")
            {
                fields.push_back("name_ru");
                fields.push_back("name:ru");
            }
            for (const std::string &field : style.fallback_fields)
                fields.push_back(field);
            fields.push_back("name");
            fields.push_back("nameascii");
            fields.push_back("namepar");

            std::string source;
            std::set<std::string> visited;
            for (const std::string &field : fields)
            {
                const std::string key = lowercase_ascii(field);
                if (!visited.insert(key).second)
                    continue;
                source = property_string(properties, field);
                if (!source.empty())
                    break;
            }
            if (source.empty())
                return "";
            if (lowercase_ascii(style.locale) != "ru" || contains_cyrillic(source))
                return source;
            const auto &aliases = russian_names();
            auto exact = aliases.find(normalize_key(source));
            return exact != aliases.end() ? exact->second : transliterate_to_russian(source);
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
