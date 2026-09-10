#include "common/map/city_name_resolver.h"
#include <cassert>
#include <iostream>
#include <fstream>

int main(int argc, char **argv)
{
    using nlohmann::json;
    using map::resolve_city_name;
    const std::vector<std::string> defaults = {"name_ru", "name", "nameascii"};
    const json norilsk = {{"name", "Noril'sk"}, {"name_ru", "Норильск"}};
    assert(resolve_city_name(norilsk, "ru", "", defaults) == "Норильск");
    assert(resolve_city_name(norilsk, "ru", "name", defaults) == "Норильск");
    assert(resolve_city_name(norilsk, "RU", "auto", defaults) == "Норильск");
    assert(resolve_city_name(norilsk, "ru_RU.UTF-8", "", defaults) == "Норильск");
    assert(resolve_city_name(norilsk, "ru-RU", "", defaults) == "Норильск");
    assert(resolve_city_name(norilsk, "en", "", defaults) == "Noril'sk");
    assert(resolve_city_name(norilsk, "auto", "", defaults) == "Noril'sk");
    assert(resolve_city_name(norilsk, "en", "name_ru", defaults) == "Норильск");
    assert(resolve_city_name(json{{"NAME_RU", "Ёлки-Город"}, {"NAME", "English"}}, "ru", "", defaults) == "Ёлки-Город");
    assert(resolve_city_name(json{{"name:ru", "Ноябрьск"}}, "ru", "", defaults) == "Ноябрьск");
    assert(resolve_city_name(json{{"name_ru", nullptr}, {"name", "Noyabrsk"}}, "ru", "", defaults) == "Noyabrsk");
    assert(resolve_city_name(json{{"name_ru", " \t"}, {"name", "Moscow"}}, "ru", "", defaults) == "Moscow");
    assert(resolve_city_name(json{{"name", "Moscow"}}, "ru", "", defaults) == "Moscow");
    assert(resolve_city_name(json{{"name", "Noril'sk"}}, "ru", "", defaults) == "Noril'sk");
    assert(resolve_city_name(json{{"name_ru", "Имя из источника"}, {"name", "Moscow"}}, "ru", "", defaults) == "Имя из источника");
    assert(resolve_city_name(json{{"name_ru", " Å — ё "}}, "ru", "", defaults) == " Å — ё ");
    assert(resolve_city_name(json{{"name_ru", 123}, {"name", "Fallback"}}, "ru", "", defaults) == "Fallback");
    assert(resolve_city_name(json{{"name", "Älmhult"}}, "ru", "", defaults) == "Älmhult");
    assert(resolve_city_name(json{{"name", "Original"}, {"name_en", "English"}}, "en", "", defaults) == "English");
    assert(resolve_city_name(json{{"custom", "Custom"}, {"name", "Original"}}, "ru", "custom", defaults) == "Custom");
    assert(resolve_city_name(json{{"nameascii", "ASCII"}}, "ru", "", defaults) == "ASCII");
    assert(resolve_city_name(json::array(), "ru", "", defaults).empty());
    assert(resolve_city_name(json::object(), "ru", "", defaults).empty());
    std::cout << "23 city-name resolver checks passed\n";
    if (argc == 2)
    {
        std::ifstream input(argv[1]);
        assert(input.good());
        json document;
        input >> document;
        int russian = 0;
        int fallback = 0;
        for (const auto &feature : document.at("features"))
        {
            const auto &properties = feature.at("properties");
            const std::string ru = map::city_names::field(properties, "name_ru");
            const std::string result = resolve_city_name(properties, "ru", "name", defaults);
            if (!ru.empty())
            {
                assert(result == ru);
                ++russian;
            }
            else
            {
                assert(result == resolve_city_name(properties, "original", "name", defaults));
                ++fallback;
            }
        }
        assert(russian > 0);
        std::cout << "Full catalogue: " << russian << " verbatim Russian names, "
                  << fallback << " unchanged fallback names\n";
    }
}
