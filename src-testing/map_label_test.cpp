#include "common/map/city_labels.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    size_t nonzero_pixels(const image::Image &image)
    {
        size_t count = 0;
        for (size_t position = 0; position < image.width() * image.height(); position++)
            if (image.get(position) != 0)
                count++;
        return count;
    }

    bool contains(const std::vector<std::string> &values, const std::string &expected)
    {
        for (const std::string &value : values)
            if (value == expected)
                return true;
        return false;
    }

    std::filesystem::path write_fixture(const std::filesystem::path &directory)
    {
        const std::filesystem::path path = directory / "cities.geojson";
        std::ofstream output(path.string());
        output << R"JSON({
  "type": "FeatureCollection",
  "features": [
    {"type":"Feature","properties":{"featurecla":"Admin-0 capital","nameascii":"Moscow","scalerank":0,"pop_max":12000000},"geometry":{"type":"Point","coordinates":[37.62,55.75]}},
    {"type":"Feature","properties":{"featurecla":"Admin-1 capital","nameascii":"St Petersburg","scalerank":1,"pop_max":5300000},"geometry":{"type":"Point","coordinates":[30.31,59.94]}},
    {"type":"Feature","properties":{"featurecla":"Admin-1 capital","nameascii":"Murmansk","scalerank":4,"pop_max":270000},"geometry":{"type":"Point","coordinates":[33.08,68.97]}},
    {"type":"Feature","properties":{"featurecla":"Admin-0 capital","nameascii":"Paris","scalerank":0,"pop_max":11000000},"geometry":{"type":"Point","coordinates":[2.35,48.86]}},
    {"type":"Feature","properties":{"featurecla":"Admin-0 capital","nameascii":"London","scalerank":0,"pop_max":9000000},"geometry":{"type":"Point","coordinates":[-0.13,51.51]}},
    {"type":"Feature","properties":{"featurecla":"Populated place","name_ru":"Тестовый город","nameascii":"Test City","scalerank":5,"pop_max":100000},"geometry":{"type":"Point","coordinates":[40.0,56.0]}},
    {"type":"Feature","properties":{"featurecla":"Populated place","nameascii":"Nearby City","scalerank":5,"pop_max":90000},"geometry":{"type":"Point","coordinates":[40.1,56.0]}}
  ]
})JSON";
        output.close();
        return path;
    }
}

int main(int argc, char **argv)
{
    initLogger();
    if (argc < 2)
    {
        std::cerr << "Usage: satdump-map-label-test <font.ttf> [output-directory]\n";
        return 2;
    }

    const std::filesystem::path output_directory = argc >= 3 ? argv[2] : "map-label-test-output";
    std::filesystem::create_directories(output_directory);
    const std::filesystem::path fixture = write_fixture(output_directory);

    image::TextDrawer drawer;
    drawer.init_font(argv[1]);
    if (!drawer.font_ready())
        return 3;

    auto projection = [](double latitude, double longitude, int height, int width) -> std::pair<int, int>
    {
        const int x = (int)((longitude + 180.0) / 360.0 * (double)(width - 1));
        const int y = (int)((90.0 - latitude) / 180.0 * (double)(height - 1));
        if (x < 0 || x >= width || y < 0 || y >= height)
            return {-1, -1};
        return {x, y};
    };

    image::Image fill(8, 1280, 720, 1);
    image::Image outline(8, 1280, 720, 1);
    map::CityLabelStyle local;
    local.font_size = 24;
    local.cities_type = 2;
    local.scale_rank = 10;
    local.detail_mode = "local";
    local.max_labels = 5;
    local.locale = "ru";
    local.avoid_overlap = true;
    local.collision_padding = 6;
    const map::CityLabelStats local_stats = map::drawProjectedCitiesGeoJsonStyled(
        {fixture.string()}, fill, outline, drawer, projection, local);

    if (local_stats.drawn <= 0 || local_stats.drawn > local.max_labels)
        return 4;
    if (!contains(local_stats.drawn_labels, "Москва") || !contains(local_stats.drawn_labels, "Париж"))
        return 5;
    if (nonzero_pixels(fill) == 0 || nonzero_pixels(outline) == 0)
        return 6;

    image::Image world_fill(8, 1280, 720, 1);
    image::Image world_outline(8, 1280, 720, 1);
    map::CityLabelStyle world = local;
    world.detail_mode = "world";
    world.max_labels = 20;
    world.avoid_overlap = false;
    const map::CityLabelStats world_stats = map::drawProjectedCitiesGeoJsonStyled(
        {fixture.string()}, world_fill, world_outline, drawer, projection, world);
    if (world_stats.resolved_mode != "world" || world_stats.drawn != 3)
        return 7;
    if (contains(world_stats.drawn_labels, "Санкт-Петербург") || contains(world_stats.drawn_labels, "Мурманск"))
        return 8;

    const map::LabelBox first{10, 10, 50, 40};
    const map::LabelBox overlapping{45, 35, 80, 70};
    const map::LabelBox separate{90, 90, 120, 120};
    if (!map::label_boxes_intersect(first, overlapping) || map::label_boxes_intersect(first, separate))
        return 9;

    std::cout << "Map label tests passed: local=" << local_stats.drawn
              << ", world=" << world_stats.drawn << "\n";
    return 0;
}
