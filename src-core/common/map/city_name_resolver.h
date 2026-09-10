#pragma once

#include "nlohmann/json.hpp"

#include <set>
#include <string>
#include <vector>

namespace map
{
    namespace city_names
    {
        inline std::string ascii_lower(std::string value)
        {
            for (char &character : value)
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
            return value;
        }

        inline std::string field(const nlohmann::json &properties, const std::string &name)
        {
            if (!properties.is_object())
                return "";
            auto found = properties.find(name);
            if (found == properties.end())
            {
                const std::string key = ascii_lower(name);
                for (auto item = properties.begin(); item != properties.end(); ++item)
                    if (ascii_lower(item.key()) == key)
                    {
                        found = item;
                        break;
                    }
            }
            if (found == properties.end() || !found->is_string())
                return "";
            const std::string value = found->get<std::string>();
            // Check emptiness without modifying any bytes of a source name.
            return value.find_first_not_of(" \t\r\n") == std::string::npos ? "" : value;
        }
    }

    // Names are data, not transliteration rules. In Russian mode an available
    // NAME_RU always wins over a legacy explicit "name" setting. Missing names
    // fall back to the original spelling; no dictionary or transliteration.
    inline std::string resolve_city_name(
        const nlohmann::json &properties,
        const std::string &locale,
        const std::string &label_field,
        const std::vector<std::string> &fallback_fields)
    {
        std::string language = city_names::ascii_lower(locale);
        language = language.substr(0, language.find_first_of("-_."));
        std::vector<std::string> fields;
        if (language == "ru")
        {
            fields.push_back("name_ru");
            fields.push_back("name:ru");
        }
        if (!label_field.empty() && city_names::ascii_lower(label_field) != "auto")
            fields.push_back(label_field);
        if (language == "en")
        {
            fields.push_back("name_en");
            fields.push_back("name:en");
        }
        for (const std::string &fallback : fallback_fields)
        {
            const std::string key = city_names::ascii_lower(fallback);
            // Old defaults include name_ru. Do not let that default force
            // Russian names after switching the language to English/original.
            if (language != "ru" && (key == "name_ru" || key == "name:ru"))
                continue;
            fields.push_back(fallback);
        }
        fields.push_back("name");
        fields.push_back("nameascii");
        fields.push_back("namepar");
        std::set<std::string> visited;
        for (const std::string &name : fields)
        {
            if (!visited.insert(city_names::ascii_lower(name)).second)
                continue;
            const std::string value = city_names::field(properties, name);
            if (!value.empty())
                return value;
        }
        return "";
    }
}
