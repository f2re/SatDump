#pragma once

#include "nlohmann/json.hpp"
#include "imgui/imgui.h"
#include "common/map/map_drawer.h"
#include "common/map/city_labels.h"
#include "nlohmann/json_utils.h"
#include "logger.h"
#include "resources.h"
#include "common/widgets/stepped_slider.h"

#include <limits>
#include <map>
#include <string>
#include <vector>

struct OverlayCache
{
    size_t width = 0;
    size_t height = 0;
    std::vector<size_t> map;
};

struct GrayscaleOverlayCache
{
    size_t width = 0;
    size_t height = 0;
    std::map<size_t, float> map;
    std::string key;
};

class OverlayHandler
{
private:
    OverlayCache map_cache;
    OverlayCache shores_cache;
    OverlayCache latlon_cache;
    GrayscaleOverlayCache cities_cache;
    GrayscaleOverlayCache cities_outline_cache;
    GrayscaleOverlayCache qth_halo_cache;
    GrayscaleOverlayCache qth_outline_cache;
    GrayscaleOverlayCache qth_marker_cache;
    GrayscaleOverlayCache qth_text_cache;

    image::TextDrawer text_drawer;

public:
    // Colors. Alpha is applied when a four-component value is supplied.
    ImVec4 color_borders = {0.72f, 0.77f, 0.82f, 0.58f};
    ImVec4 color_shores = {0.88f, 0.82f, 0.62f, 0.62f};
    ImVec4 color_cities = {0.97f, 0.98f, 1.00f, 0.94f};
    ImVec4 color_cities_outline = {0.12f, 0.14f, 0.17f, 0.82f};
    ImVec4 color_qth = {0.94f, 0.72f, 0.24f, 1.00f};
    ImVec4 color_qth_text = {1.00f, 0.98f, 0.90f, 0.98f};
    ImVec4 color_qth_outline = {0.12f, 0.13f, 0.15f, 0.90f};
    ImVec4 color_latlon = {0.55f, 0.63f, 0.72f, 0.24f};

    // Reception point (QTH).
    std::string qth_label;
    std::string qth_label_prefix = "Пункт приёма: ";
    double qth_latitude = 0.0;
    double qth_longitude = 0.0;
    bool qth_coordinates_configured = false;

    // Layer switches.
    bool draw_map_overlay = false;
    bool draw_shores_overlay = false;
    bool draw_cities_overlay = false;
    bool draw_qth_overlay = false;
    bool draw_latlon_overlay = false;

    // City selection and label generalization.
    int cities_type = 0;
    int cities_size = 50;
    int cities_scale_rank = 3;
    std::string cities_locale = "ru";
    std::string cities_label_field;
    std::vector<std::string> cities_fallback_fields = {"name_ru", "name", "nameascii"};
    std::string cities_mode = "auto";
    bool cities_avoid_overlap = true;
    bool cities_prioritize_capitals = true;
    int cities_max_labels = 120;
    int cities_collision_padding = 4;
    int cities_outline_width = 2;

    // Reception point appearance.
    int qth_outline_width = 2;
    int qth_marker_size = 8;
    float qth_font_scale = 1.25f;
    float qth_halo_alpha = 0.28f;

    void set_defaults();
    void clear_cache();

public:
    OverlayHandler()
    {
        set_defaults();
    }

    int enabled();
    bool drawUI();
    void apply(image::Image &img,
               std::function<std::pair<int, int>(double, double, int, int)> proj_func,
               float *step_cnt = nullptr);
    nlohmann::json get_config();
    void set_config(nlohmann::json in, bool status = true);
};
