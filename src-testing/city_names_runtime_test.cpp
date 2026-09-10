#include "common/map/city_labels.h"
#include "common/map/city_name_resolver.h"
#include "common/map/map_drawer.h"
#include "common/image/io.h"
#include "common/image/font/utf8.h"
#include "common/overlay_handler.h"
#include "satdump_vars.h"
#include "resources.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;
namespace
{
    void require(bool ok, const std::string &message)
    {
        if (!ok) throw std::runtime_error(message);
    }
    json read_json(const fs::path &path)
    {
        std::ifstream input(path);
        require(input.good(), "Cannot read " + path.string());
        json result; input >> result; return result;
    }
    void write_json(const fs::path &path, const json &value)
    {
        std::ofstream output(path);
        output << value.dump(2) << '\n';
        require(output.good(), "Cannot write " + path.string());
    }
    bool same_pixels(const image::Image &a, const image::Image &b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a.get(i) != b.get(i)) return false;
        return true;
    }
    size_t ink(const image::Image &value)
    {
        size_t result = 0;
        for (size_t i = 0; i < value.size(); ++i) result += value.get(i) != 0;
        return result;
    }
    class FontProbe : public image::TextDrawer
    {
    public:
        bool has_glyph(int cp) { return stbtt_FindGlyphIndex(&font.fontp, cp) != 0; }
    };
}

int main(int argc, char **argv)
{
    try
    {
        require(argc == 3, "Usage: satdump-city-names-runtime-test <resources> <output>");
        initLogger();
        const fs::path root = fs::canonical(argv[1]);
        const fs::path output = fs::absolute(argv[2]);
        fs::create_directories(output / "empty-cwd");
        fs::current_path(output / "empty-cwd");
        require(!fs::exists("resources"), "Source resources must not shadow installed resources");
        satdump::RESPATH = root.parent_path().string() + "/";
        const std::string catalog = resources::getResourcePath("maps/ne_10m_populated_places_simple.json");
        require(fs::equivalent(catalog, root / "maps/ne_10m_populated_places_simple.json"),
                "Resource lookup selected a different catalogue");
        const json data = read_json(catalog);
        const json report = read_json(root / "maps/city_names_ru.report.json");
        const auto &features = data.at("features");
        require(features.size() == report.at("target_features").get<size_t>(), "Catalogue count differs from report");
        size_t russian = 0, fallback = 0;
        std::vector<std::string> names;
        std::set<int> codepoints;
        for (const auto &feature : features)
        {
            const auto &p = feature.at("properties");
            const std::string ru = map::city_names::field(p, "name_ru");
            const std::string name = map::resolve_city_name(p, "ru_RU.UTF-8", "name", {"name_ru", "name", "nameascii"});
            if (!ru.empty())
            {
                require(name == ru, "Russian name was modified or overridden");
                ++russian; names.push_back(ru);
                auto cursor = ru.begin();
                while (cursor != ru.end()) codepoints.insert(utf8::next(cursor, ru.end()));
            }
            else
            {
                require(name == map::resolve_city_name(p, "original", "name", {"name", "nameascii"}), "Fallback name was translated");
                ++fallback;
            }
        }
        require(russian > 0 && russian == report.at("russian_names").get<size_t>(), "Russian coverage differs from report");
        json font_results = json::array();
        for (const std::string font_name : {"font.ttf", "Roboto-Medium.ttf"})
        {
            FontProbe font;
            font.init_font((root / "fonts" / font_name).string());
            require(font.font_ready(), "Font missing: " + font_name);
            for (int cp : codepoints)
                require(font.has_glyph(cp), "Missing glyph " + std::to_string(cp) + " in " + font_name);
            for (const std::string &name : names)
            {
                const auto size = font.measure_text(26, name);
                require(size.width > 0 && size.width < 8192, "Invalid text extent");
                image::Image label(8, size.width + 32, std::max(96, size.height + 32), 1);
                font.draw_text(label, 8, 8, {1}, 26, name);
                require(ink(label) > 0, "Russian text rendered blank: " + name);
            }
            font_results.push_back({{"font", font_name}, {"codepoints", codepoints.size()}, {"rendered_names", names.size()}, {"missing_glyphs", 0}});
        }

        const std::vector<std::string> samples = {"Москва", "Санкт-Петербург", "Норильск", "Ноябрьск", "Комсомольск-на-Амуре", "Ханты-Мансийск", "Кишинёв", "Париж"};
        std::vector<json> selected;
        for (const auto &label : samples)
        {
            auto found = std::find_if(features.begin(), features.end(), [&](const json &f) {
                return map::city_names::field(f.at("properties"), "name_ru") == label;
            });
            require(found != features.end(), "Missing reference city: " + label);
            selected.push_back(*found);
        }
        auto projection = [&](double lat, double lon, int, int) -> std::pair<int, int> {
            for (size_t i = 0; i < selected.size(); ++i)
            {
                const auto &c = selected[i].at("geometry").at("coordinates");
                if (std::fabs(lon - c[0].get<double>()) < 1e-7 && std::fabs(lat - c[1].get<double>()) < 1e-7)
                    return {40 + int(i % 2) * 768, 100 + int(i / 2) * 180};
            }
            return {-1, -1};
        };
        image::TextDrawer font;
        font.init_font(resources::getResourcePath("fonts/font.ttf"));
        map::CityLabelStyle style;
        style.font_size = 30; style.cities_type = 2; style.scale_rank = 10;
        style.max_labels = 16; style.detail_mode = "local"; style.label_field = "name";
        image::Image fill(8, 1536, 768, 1), outline(8, 1536, 768, 1);
        const auto stats = map::drawProjectedCitiesGeoJsonStyled({catalog}, fill, outline, font, projection, style);
        require(std::set<std::string>(stats.drawn_labels.begin(), stats.drawn_labels.end()) == std::set<std::string>(samples.begin(), samples.end()), "Styled renderer did not draw every exact Russian sample");
        // Count text away from city markers, so marker-only output cannot pass.
        for (size_t i = 0; i < samples.size(); ++i)
        {
            size_t count = 0;
            const int x0 = 70 + int(i % 2) * 768, y0 = 50 + int(i / 2) * 180;
            for (int y = y0; y < y0 + 120; ++y)
                for (int x = x0; x < x0 + 650; ++x) count += fill.get(0, x, y) != 0;
            require(count > 40, "Missing projected text pixels: " + samples[i]);
        }
        image::save_png(fill, (output / "styled_ru_mask.png").string());
        image::save_png(outline, (output / "styled_ru_outline.png").string());

        json reference = {{"type", "FeatureCollection"}, {"features", json::array()}};
        for (auto feature : selected)
        {
            auto &p = feature["properties"];
            const std::string ru = p["name_ru"].get<std::string>();
            p["name"] = ru; p["nameascii"] = ru; p.erase("name_ru");
            reference["features"].push_back(feature);
        }
        write_json(output / "legacy-reference.geojson", reference);
        image::Image legacy(8, 1536, 768, 1), expected(8, 1536, 768, 1);
        map::drawProjectedCitiesGeoJson({catalog}, legacy, font, {1}, projection, 30, 2, 10);
        map::drawProjectedCitiesGeoJson({(output / "legacy-reference.geojson").string()}, expected, font, {1}, projection, 30, 2, 10);
        require(ink(legacy) > 0 && same_pixels(legacy, expected), "Legacy renderer bypasses name_ru");
        image::save_png(legacy, (output / "legacy_ru.png").string());

        OverlayHandler handler;
        json config = {{"draw_cities_overlay", true}, {"cities_locale", "ru"}, {"cities_label_field", "name"},
                       {"cities_type", 2}, {"cities_scale_rank", 10}, {"cities_size", 30}, {"cities_max_labels", 16}, {"cities_mode", "local"}};
        handler.set_config(config);
        require(handler.get_config().at("cities_locale") == "ru", "Overlay config lost locale");
        image::Image ru(8, 1536, 768, 3), en(8, 1536, 768, 3), again(8, 1536, 768, 3);
        handler.apply(ru, projection);
        require(ink(ru) > 500, "OverlayHandler output is empty");
        config["cities_locale"] = "en"; handler.set_config(config); handler.apply(en, projection);
        require(!same_pixels(ru, en), "Changing locale did not invalidate city cache");
        config["cities_locale"] = "ru"; handler.set_config(config); handler.apply(again, projection);
        require(same_pixels(ru, again), "Russian overlay is not repeatable after language switch");
        image::save_png(ru, (output / "overlay_ru.png").string());
        image::save_png(en, (output / "overlay_en.png").string());
        write_json(output / "runtime-report.json", {{"catalogue_features", features.size()}, {"russian_names", russian},
            {"original_fallback_names", fallback}, {"fonts", font_results}, {"projected_samples", stats.drawn_labels},
            {"legacy_pixels_match", true}, {"overlay_language_cache_roundtrip", true}, {"resources", root.string()}});
        std::cout << "City runtime PASS: " << russian << " Russian names rendered in both fonts, " << fallback
                  << " original fallbacks, " << stats.drawn << " projected samples; legacy and OverlayHandler passed\n";
        return 0;
    }
    catch (const std::exception &e) { std::cerr << "City runtime FAIL: " << e.what() << '\n'; return 1; }
}
