#pragma once

#include "common/image/image.h"
#include "common/image/text.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace map
{
    struct LabelBox
    {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
    };

    struct CityLabelStyle
    {
        int font_size = 28;
        int cities_type = 1;
        int scale_rank = 4;
        int max_labels = 120;
        int collision_padding = 4;
        int marker_radius = 3;
        int outline_width = 2;
        bool avoid_overlap = true;
        bool prioritize_capitals = true;
        std::string locale = "ru";
        std::string label_field;
        std::vector<std::string> fallback_fields = {"name_ru", "name", "nameascii"};
        std::string detail_mode = "auto";
    };

    struct CityLabelStats
    {
        int candidates = 0;
        int projected = 0;
        int drawn = 0;
        int skipped_filter = 0;
        int skipped_overlap = 0;
        int skipped_limit = 0;
        std::string resolved_mode;
        std::vector<std::string> drawn_labels;
    };

    bool label_boxes_intersect(const LabelBox &left, const LabelBox &right, int padding = 0);

    CityLabelStats drawProjectedCitiesGeoJsonStyled(
        const std::vector<std::string> &json_files,
        image::Image &fill_mask,
        image::Image &outline_mask,
        image::TextDrawer &text_drawer,
        std::function<std::pair<int, int>(double, double, int, int)> projection_func,
        const CityLabelStyle &style,
        const std::vector<LabelBox> &reserved_boxes = {});
}
