#include "overlay_handler.h"

#include "core/config.h"
#include "imgui/imgui_stdlib.h"

#include <algorithm>
#include <cmath>

namespace
{
    float clamp01(float value)
    {
        return std::max(0.0f, std::min(1.0f, value));
    }

    bool nearly(float left, float right)
    {
        return std::fabs(left - right) < 0.001f;
    }

    bool rgb_is(const ImVec4 &color, float red, float green, float blue)
    {
        return nearly(color.x, red) && nearly(color.y, green) && nearly(color.z, blue);
    }

    nlohmann::json color_json(const ImVec4 &color)
    {
        return {color.x, color.y, color.z, color.w};
    }

    void read_color(const nlohmann::json &root, const std::string &key, ImVec4 &color)
    {
        if (!root.is_object() || !root.contains(key) || !root[key].is_array() || root[key].size() < 3)
            return;
        try
        {
            color.x = clamp01(root[key][0].get<float>());
            color.y = clamp01(root[key][1].get<float>());
            color.z = clamp01(root[key][2].get<float>());
            if (root[key].size() >= 4)
                color.w = clamp01(root[key][3].get<float>());
        }
        catch (const std::exception &)
        {
        }
    }

    template <typename T>
    void read_value(const nlohmann::json &root, const std::string &key, T &value)
    {
        if (!root.is_object() || !root.contains(key))
            return;
        try
        {
            value = root[key].get<T>();
        }
        catch (const std::exception &)
        {
        }
    }

    void store_mask(const image::Image &mask, GrayscaleOverlayCache &cache, const std::string &key)
    {
        cache.map.clear();
        cache.width = mask.width();
        cache.height = mask.height();
        cache.key = key;
        for (size_t position = 0; position < mask.width() * mask.height(); position++)
        {
            const float value = (float)mask.getf(position);
            if (value > 0.0f)
                cache.map[position] = value;
        }
    }

    bool cache_matches(const GrayscaleOverlayCache &cache, size_t width, size_t height, const std::string &key)
    {
        return cache.width == width && cache.height == height && cache.key == key;
    }

    void blend_pixel(image::Image &image, size_t position, const ImVec4 &color, float coverage)
    {
        const int channels = image.channels();
        if (channels <= 0)
            return;
        const float alpha = clamp01(coverage * color.w);
        if (alpha <= 0.0f)
            return;

        if (channels == 1)
        {
            const float luminance = 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
            const float destination = (float)image.getf(0, position);
            image.setf(0, position, luminance * alpha + destination * (1.0f - alpha));
            return;
        }

        const float destination_alpha = channels >= 4 ? (float)image.getf(3, position) : 1.0f;
        const float output_alpha = alpha + destination_alpha * (1.0f - alpha);
        const float components[3] = {color.x, color.y, color.z};
        for (int channel = 0; channel < std::min(3, channels); channel++)
        {
            const float destination = (float)image.getf(channel, position);
            const float output = output_alpha <= 0.0f
                                     ? 0.0f
                                     : (components[channel] * alpha + destination * destination_alpha * (1.0f - alpha)) /
                                           output_alpha;
            image.setf(channel, position, output);
        }
        if (channels >= 4)
            image.setf(3, position, output_alpha);
    }

    void apply_mask(image::Image &image, const GrayscaleOverlayCache &cache, const ImVec4 &color)
    {
        for (const auto &entry : cache.map)
            blend_pixel(image, entry.first, color, entry.second);
    }

    void apply_binary(image::Image &image, const OverlayCache &cache, const ImVec4 &color)
    {
        for (size_t position : cache.map)
            blend_pixel(image, position, color, 1.0f);
    }

    std::string city_cache_key(const OverlayHandler &handler, size_t width, size_t height)
    {
        nlohmann::json key = {
            {"width", width}, {"height", height}, {"type", handler.cities_type},
            {"size", handler.cities_size}, {"rank", handler.cities_scale_rank},
            {"locale", handler.cities_locale}, {"label_field", handler.cities_label_field},
            {"fallback", handler.cities_fallback_fields}, {"mode", handler.cities_mode},
            {"avoid_overlap", handler.cities_avoid_overlap},
            {"prioritize_capitals", handler.cities_prioritize_capitals},
            {"max_labels", handler.cities_max_labels},
            {"collision_padding", handler.cities_collision_padding},
            {"outline_width", handler.cities_outline_width},
            {"qth", handler.draw_qth_overlay}, {"qth_lat", handler.qth_latitude},
            {"qth_lon", handler.qth_longitude}, {"qth_label", handler.qth_label}};
        return key.dump();
    }

    std::string reception_label(const OverlayHandler &handler)
    {
        if (handler.qth_label.empty())
            return "Пункт приёма";
        if (handler.qth_label_prefix.empty() || handler.qth_label.rfind(handler.qth_label_prefix, 0) == 0)
            return handler.qth_label;
        return handler.qth_label_prefix + handler.qth_label;
    }

    struct ReceptionLayout
    {
        bool valid = false;
        int x = 0;
        int y = 0;
        int text_x = 0;
        int text_y = 0;
        int font_size = 0;
        map::LabelBox box;
        std::string label;
    };

    ReceptionLayout make_reception_layout(
        const OverlayHandler &handler,
        image::TextDrawer &text_drawer,
        size_t width,
        size_t height,
        const std::function<std::pair<int, int>(double, double, int, int)> &projection_func)
    {
        ReceptionLayout layout;
        if (!handler.draw_qth_overlay || !handler.qth_coordinates_configured || !text_drawer.font_ready())
            return layout;
        if (!std::isfinite(handler.qth_latitude) || !std::isfinite(handler.qth_longitude) ||
            handler.qth_latitude < -90.0 || handler.qth_latitude > 90.0 ||
            handler.qth_longitude < -180.0 || handler.qth_longitude > 180.0)
            return layout;

        const std::pair<int, int> point = projection_func(
            handler.qth_latitude, handler.qth_longitude, (int)height, (int)width);
        if (point.first < 0 || point.second < 0 || point.first >= (int)width || point.second >= (int)height)
            return layout;

        layout.x = point.first;
        layout.y = point.second;
        layout.font_size = std::max(14, (int)std::round(handler.cities_size * handler.qth_font_scale));
        layout.label = reception_label(handler);
        const image::TextSize text_size = text_drawer.measure_text(layout.font_size, layout.label);
        const int text_width = std::max(1, text_size.width);
        const int text_height = std::max(layout.font_size, std::max(text_size.height, text_size.line_height));
        const int marker = std::max(3, handler.qth_marker_size);
        const int gap = marker + std::max(4, handler.qth_outline_width + 3);

        layout.text_x = layout.x + gap;
        if (layout.text_x + text_width + handler.qth_outline_width >= (int)width)
            layout.text_x = layout.x - gap - text_width;
        layout.text_y = layout.y - text_height / 2;
        layout.text_x = std::max(handler.qth_outline_width,
                                 std::min((int)width - text_width - handler.qth_outline_width, layout.text_x));
        layout.text_y = std::max(handler.qth_outline_width,
                                 std::min((int)height - text_height - handler.qth_outline_width, layout.text_y));

        layout.box.left = std::min(layout.x - marker, layout.text_x) - handler.qth_outline_width;
        layout.box.top = std::min(layout.y - marker, layout.text_y) - handler.qth_outline_width;
        layout.box.right = std::max(layout.x + marker + 1, layout.text_x + text_width) + handler.qth_outline_width;
        layout.box.bottom = std::max(layout.y + marker + 1, layout.text_y + text_height) + handler.qth_outline_width;
        layout.valid = true;
        return layout;
    }

    std::string qth_cache_key(const OverlayHandler &handler, const ReceptionLayout &layout, size_t width, size_t height)
    {
        nlohmann::json key = {
            {"width", width}, {"height", height}, {"x", layout.x}, {"y", layout.y},
            {"font", layout.font_size}, {"label", layout.label},
            {"marker", handler.qth_marker_size}, {"outline", handler.qth_outline_width},
            {"halo", handler.qth_halo_alpha}};
        return key.dump();
    }

    void draw_reception_masks(OverlayHandler &handler,
                              const ReceptionLayout &layout,
                              size_t width,
                              size_t height,
                              const std::string &key,
                              GrayscaleOverlayCache &halo_cache,
                              GrayscaleOverlayCache &outline_cache,
                              GrayscaleOverlayCache &marker_cache,
                              GrayscaleOverlayCache &text_cache,
                              image::TextDrawer &text_drawer)
    {
        image::Image halo(8, width, height, 1);
        image::Image outline(8, width, height, 1);
        image::Image marker(8, width, height, 1);
        image::Image text(8, width, height, 1);
        const int radius = std::max(3, handler.qth_marker_size);
        const int outline_width = std::max(0, handler.qth_outline_width);
        halo.draw_circle(layout.x, layout.y, std::max(radius + 2, (int)std::round(radius * 2.2)), {1}, true);
        if (outline_width > 0)
        {
            outline.draw_circle(layout.x, layout.y, radius + outline_width, {1}, true);
            for (int y = -outline_width; y <= outline_width; y++)
                for (int x = -outline_width; x <= outline_width; x++)
                    if (x * x + y * y <= outline_width * outline_width)
                        text_drawer.draw_text(outline, layout.text_x + x, layout.text_y + y,
                                              {1}, layout.font_size, layout.label);
        }
        marker.draw_circle(layout.x, layout.y, radius, {1}, true);
        text_drawer.draw_text(text, layout.text_x, layout.text_y, {1}, layout.font_size, layout.label);
        store_mask(halo, halo_cache, key);
        store_mask(outline, outline_cache, key);
        store_mask(marker, marker_cache, key);
        store_mask(text, text_cache, key);
    }

    int mode_index(const std::string &mode)
    {
        if (mode == "world") return 1;
        if (mode == "continent") return 2;
        if (mode == "regional") return 3;
        if (mode == "local") return 4;
        return 0;
    }

    std::string mode_name(int index)
    {
        static const char *values[] = {"auto", "world", "continent", "regional", "local"};
        return values[std::max(0, std::min(4, index))];
    }
}

int OverlayHandler::enabled()
{
    return (int)draw_map_overlay + (int)draw_cities_overlay + (int)draw_qth_overlay +
           (int)draw_latlon_overlay + (int)draw_shores_overlay;
}

bool OverlayHandler::drawUI()
{
    bool update = false;
    ImGui::Checkbox("Координатная сетка", &draw_latlon_overlay);
    ImGui::SameLine();
    ImGui::ColorEdit4("##latlongrid", (float *)&color_latlon, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::Checkbox("Государственные границы", &draw_map_overlay);
    ImGui::SameLine();
    ImGui::ColorEdit4("##borders", (float *)&color_borders, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::Checkbox("Береговая линия", &draw_shores_overlay);
    ImGui::SameLine();
    ImGui::ColorEdit4("##shores", (float *)&color_shores, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::Checkbox("Города", &draw_cities_overlay);
    ImGui::SameLine();
    ImGui::ColorEdit4("##cities", (float *)&color_cities, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    ImGui::SameLine();
    ImGui::ColorEdit4("##cities-outline", (float *)&color_cities_outline, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    if (draw_cities_overlay)
    {
        widgets::SteppedSliderInt("Размер подписей", &cities_size, 10, 160);
        static const char *city_categories[] = {"Только столицы", "Столицы и региональные центры", "По рангу масштаба"};
        ImGui::Combo("Состав городов", &cities_type, city_categories, IM_ARRAYSIZE(city_categories));
        if (cities_type == 2)
            widgets::SteppedSliderInt("Максимальный ранг", &cities_scale_rank, 0, 10);
        int generalization = mode_index(cities_mode);
        static const char *generalization_modes[] = {"Автоматически", "Мир", "Материк", "Регион", "Локально"};
        if (ImGui::Combo("Генерализация", &generalization, generalization_modes, IM_ARRAYSIZE(generalization_modes)))
            cities_mode = mode_name(generalization);
        ImGui::Checkbox("Не допускать наложений", &cities_avoid_overlap);
        ImGui::Checkbox("Столицы имеют приоритет", &cities_prioritize_capitals);
        ImGui::InputInt("Максимум подписей", &cities_max_labels);
        ImGui::InputInt("Промежуток между подписями", &cities_collision_padding);
        ImGui::InputInt("Толщина обводки", &cities_outline_width);
        ImGui::InputText("Язык подписей", &cities_locale);
    }

    ImGui::Checkbox("Пункт приёма", &draw_qth_overlay);
    ImGui::SameLine();
    ImGui::ColorEdit4("##qth-marker", (float *)&color_qth, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
    if (draw_qth_overlay)
    {
        ImGui::InputText("Название пункта приёма", &qth_label);
        ImGui::InputText("Префикс подписи", &qth_label_prefix);
        bool coordinates_changed = ImGui::InputDouble("Широта пункта", &qth_latitude, 0.01, 0.1, "%.6f");
        coordinates_changed = ImGui::InputDouble("Долгота пункта", &qth_longitude, 0.01, 0.1, "%.6f") || coordinates_changed;
        if (coordinates_changed)
            qth_coordinates_configured = true;
        ImGui::ColorEdit4("Цвет подписи пункта", (float *)&color_qth_text);
        ImGui::ColorEdit4("Обводка пункта", (float *)&color_qth_outline);
        ImGui::InputInt("Размер маркера пункта", &qth_marker_size);
        ImGui::InputFloat("Масштаб подписи пункта", &qth_font_scale, 0.05f, 0.1f, "%.2f");
        ImGui::SliderFloat("Прозрачность ореола", &qth_halo_alpha, 0.0f, 1.0f, "%.2f");
    }

    if (ImGui::Button("По умолчанию###overlay-defaults"))
    {
        set_defaults();
        clear_cache();
    }
    ImGui::SameLine();
    if (ImGui::Button("Применить###overlay-apply"))
    {
        clear_cache();
        update = true;
    }
    return update;
}

void OverlayHandler::apply(image::Image &img,
                           std::function<std::pair<int, int>(double, double, int, int)> proj_func,
                           float *step_cnt)
{
    const size_t width = img.width();
    const size_t height = img.height();

    if (draw_map_overlay)
    {
        if (map_cache.map.empty() || map_cache.width != width || map_cache.height != height)
        {
            logger->info("Drawing map overlay...");
            map_cache.map.clear();
            map_cache.width = width;
            map_cache.height = height;
            image::Image bitmask(8, width, height, 1);
            map::drawProjectedMapShapefile({resources::getResourcePath("maps/ne_10m_admin_0_countries.shp")}, bitmask, {1}, proj_func);
            for (size_t position = 0; position < width * height; position++)
                if (bitmask.get(position) == 255)
                    map_cache.map.push_back(position);
        }
        apply_binary(img, map_cache, color_borders);
        if (step_cnt != nullptr) (*step_cnt)++;
    }

    if (draw_shores_overlay)
    {
        if (shores_cache.map.empty() || shores_cache.width != width || shores_cache.height != height)
        {
            logger->info("Drawing shores overlay...");
            shores_cache.map.clear();
            shores_cache.width = width;
            shores_cache.height = height;
            image::Image bitmask(8, width, height, 1);
            map::drawProjectedMapShapefile({resources::getResourcePath("maps/ne_10m_coastline.shp")}, bitmask, {1}, proj_func);
            for (size_t position = 0; position < width * height; position++)
                if (bitmask.get(position) == 255)
                    shores_cache.map.push_back(position);
        }
        apply_binary(img, shores_cache, color_shores);
        if (step_cnt != nullptr) (*step_cnt)++;
    }

    if (!text_drawer.font_ready())
        text_drawer.init_font(resources::getResourcePath("fonts/font.ttf"));
    ReceptionLayout reception = make_reception_layout(*this, text_drawer, width, height, proj_func);

    if (draw_cities_overlay && text_drawer.font_ready())
    {
        const std::string key = city_cache_key(*this, width, height);
        if (!cache_matches(cities_cache, width, height, key) || !cache_matches(cities_outline_cache, width, height, key))
        {
            logger->info("Drawing generalized city labels...");
            image::Image fill_mask(8, width, height, 1);
            image::Image outline_mask(8, width, height, 1);
            map::CityLabelStyle style;
            style.font_size = std::max(10, cities_size);
            style.cities_type = std::max(0, std::min(2, cities_type));
            style.scale_rank = std::max(0, std::min(10, cities_scale_rank));
            style.max_labels = std::max(1, cities_max_labels);
            style.collision_padding = std::max(0, cities_collision_padding);
            style.marker_radius = std::max(2, cities_size / 9);
            style.outline_width = std::max(0, cities_outline_width);
            style.avoid_overlap = cities_avoid_overlap;
            style.prioritize_capitals = cities_prioritize_capitals;
            style.locale = cities_locale;
            style.label_field = cities_label_field;
            style.fallback_fields = cities_fallback_fields;
            style.detail_mode = cities_mode;
            std::vector<map::LabelBox> reserved;
            if (reception.valid) reserved.push_back(reception.box);
            const map::CityLabelStats stats = map::drawProjectedCitiesGeoJsonStyled(
                {resources::getResourcePath("maps/ne_10m_populated_places_simple.json")},
                fill_mask, outline_mask, text_drawer, proj_func, style, reserved);
            store_mask(fill_mask, cities_cache, key);
            store_mask(outline_mask, cities_outline_cache, key);
            logger->info("City labels: mode=%s, drawn=%d, projected=%d, overlaps=%d",
                         stats.resolved_mode.c_str(), stats.drawn, stats.projected, stats.skipped_overlap);
        }
        apply_mask(img, cities_outline_cache, color_cities_outline);
        apply_mask(img, cities_cache, color_cities);
        if (step_cnt != nullptr) (*step_cnt)++;
    }

    if (draw_qth_overlay && reception.valid && text_drawer.font_ready())
    {
        const std::string key = qth_cache_key(*this, reception, width, height);
        if (!cache_matches(qth_halo_cache, width, height, key) ||
            !cache_matches(qth_outline_cache, width, height, key) ||
            !cache_matches(qth_marker_cache, width, height, key) ||
            !cache_matches(qth_text_cache, width, height, key))
        {
            logger->info("Drawing reception point: %s", reception.label.c_str());
            draw_reception_masks(*this, reception, width, height, key,
                                 qth_halo_cache, qth_outline_cache, qth_marker_cache, qth_text_cache, text_drawer);
        }
        ImVec4 halo = color_qth;
        halo.w = clamp01(qth_halo_alpha);
        apply_mask(img, qth_halo_cache, halo);
        apply_mask(img, qth_outline_cache, color_qth_outline);
        apply_mask(img, qth_marker_cache, color_qth);
        apply_mask(img, qth_text_cache, color_qth_text);
        if (step_cnt != nullptr) (*step_cnt)++;
    }
    else if (draw_qth_overlay && !qth_coordinates_configured)
    {
        logger->debug("Reception point overlay is enabled, but qth_lat/qth_lon and a station name are not configured");
    }

    if (draw_latlon_overlay)
    {
        if (latlon_cache.map.empty() || latlon_cache.width != width || latlon_cache.height != height)
        {
            logger->info("Drawing lat/lon overlay...");
            latlon_cache.map.clear();
            latlon_cache.width = width;
            latlon_cache.height = height;
            image::Image bitmask(8, width, height, 1);
            map::drawProjectedMapLatLonGrid(bitmask, {1}, proj_func);
            for (size_t position = 0; position < width * height; position++)
                if (bitmask.get(position) == 255)
                    latlon_cache.map.push_back(position);
        }
        apply_binary(img, latlon_cache, color_latlon);
        if (step_cnt != nullptr) (*step_cnt)++;
    }
}

nlohmann::json OverlayHandler::get_config()
{
    nlohmann::json output;
    output["qth_label"] = qth_label;
    output["qth_label_prefix"] = qth_label_prefix;
    output["qth_lat"] = qth_latitude;
    output["qth_lon"] = qth_longitude;
    output["cities_type"] = cities_type;
    output["cities_size"] = cities_size;
    output["cities_scale_rank"] = cities_scale_rank;
    output["cities_locale"] = cities_locale;
    output["cities_label_field"] = cities_label_field;
    output["cities_fallback_fields"] = cities_fallback_fields;
    output["cities_mode"] = cities_mode;
    output["cities_avoid_overlap"] = cities_avoid_overlap;
    output["cities_prioritize_capitals"] = cities_prioritize_capitals;
    output["cities_max_labels"] = cities_max_labels;
    output["cities_collision_padding"] = cities_collision_padding;
    output["cities_outline_width"] = cities_outline_width;
    output["qth_outline_width"] = qth_outline_width;
    output["qth_marker_size"] = qth_marker_size;
    output["qth_font_scale"] = qth_font_scale;
    output["qth_halo_alpha"] = qth_halo_alpha;
    output["borders_color"] = color_json(color_borders);
    output["shores_color"] = color_json(color_shores);
    output["cities_color"] = color_json(color_cities);
    output["cities_outline_color"] = color_json(color_cities_outline);
    output["qth_color"] = color_json(color_qth);
    output["qth_text_color"] = color_json(color_qth_text);
    output["qth_outline_color"] = color_json(color_qth_outline);
    output["latlon_color"] = color_json(color_latlon);
    output["draw_map_overlay"] = draw_map_overlay;
    output["draw_shores_overlay"] = draw_shores_overlay;
    output["draw_cities_overlay"] = draw_cities_overlay;
    output["draw_qth_overlay"] = draw_qth_overlay;
    output["draw_latlon_overlay"] = draw_latlon_overlay;
    output["cities_scale"] = cities_size;
    return output;
}

void OverlayHandler::set_config(nlohmann::json input, bool status)
{
    read_value(input, "qth_label", qth_label);
    read_value(input, "qth_label_prefix", qth_label_prefix);
    if (input.contains("qth_lat"))
    {
        read_value(input, "qth_lat", qth_latitude);
        qth_coordinates_configured = true;
    }
    if (input.contains("qth_lon"))
    {
        read_value(input, "qth_lon", qth_longitude);
        qth_coordinates_configured = true;
    }
    read_value(input, "cities_size", cities_size);
    read_value(input, "cities_type", cities_type);
    read_value(input, "cities_scale_rank", cities_scale_rank);
    read_value(input, "cities_locale", cities_locale);
    read_value(input, "cities_language", cities_locale);
    read_value(input, "cities_label_field", cities_label_field);
    read_value(input, "cities_mode", cities_mode);
    read_value(input, "cities_avoid_overlap", cities_avoid_overlap);
    read_value(input, "cities_prioritize_capitals", cities_prioritize_capitals);
    read_value(input, "cities_max_labels", cities_max_labels);
    read_value(input, "cities_collision_padding", cities_collision_padding);
    read_value(input, "cities_min_distance_px", cities_collision_padding);
    read_value(input, "cities_outline_width", cities_outline_width);
    read_value(input, "qth_outline_width", qth_outline_width);
    read_value(input, "qth_marker_size", qth_marker_size);
    read_value(input, "qth_marker_radius", qth_marker_size);
    read_value(input, "qth_font_scale", qth_font_scale);
    read_value(input, "qth_halo_alpha", qth_halo_alpha);
    if (input.contains("cities_fallback_fields") && input["cities_fallback_fields"].is_array())
    {
        try { cities_fallback_fields = input["cities_fallback_fields"].get<std::vector<std::string>>(); }
        catch (const std::exception &) {}
    }

    read_color(input, "borders_color", color_borders);
    read_color(input, "shores_color", color_shores);
    read_color(input, "cities_color", color_cities);
    read_color(input, "cities_text_color", color_cities);
    read_color(input, "cities_outline_color", color_cities_outline);
    read_color(input, "qth_color", color_qth);
    read_color(input, "qth_marker_color", color_qth);
    read_color(input, "qth_text_color", color_qth_text);
    read_color(input, "qth_outline_color", color_qth_outline);
    read_color(input, "latlon_color", color_latlon);

    if (status)
    {
        read_value(input, "draw_map_overlay", draw_map_overlay);
        read_value(input, "draw_shores_overlay", draw_shores_overlay);
        read_value(input, "draw_cities_overlay", draw_cities_overlay);
        read_value(input, "draw_latlon_overlay", draw_latlon_overlay);
        read_value(input, "draw_qth_overlay", draw_qth_overlay);
    }
    read_value(input, "cities_scale", cities_size);

    cities_size = std::max(10, cities_size);
    cities_scale_rank = std::max(0, std::min(10, cities_scale_rank));
    cities_max_labels = std::max(1, cities_max_labels);
    cities_collision_padding = std::max(0, cities_collision_padding);
    cities_outline_width = std::max(0, cities_outline_width);
    qth_outline_width = std::max(0, qth_outline_width);
    qth_marker_size = std::max(3, qth_marker_size);
    qth_font_scale = std::max(0.6f, std::min(3.0f, qth_font_scale));
    qth_halo_alpha = clamp01(qth_halo_alpha);
    clear_cache();
}

void OverlayHandler::set_defaults()
{
    cities_locale = "ru";
    cities_label_field.clear();
    cities_fallback_fields = {"name_ru", "name", "nameascii"};
    cities_mode = "auto";
    cities_avoid_overlap = true;
    cities_prioritize_capitals = true;
    cities_max_labels = 120;
    cities_collision_padding = 4;
    cities_outline_width = 2;
    qth_label_prefix = "Пункт приёма: ";
    qth_outline_width = 2;
    qth_marker_size = 8;
    qth_font_scale = 1.25f;
    qth_halo_alpha = 0.28f;
    color_borders = {0.72f, 0.77f, 0.82f, 0.58f};
    color_shores = {0.88f, 0.82f, 0.62f, 0.62f};
    color_cities = {0.97f, 0.98f, 1.00f, 0.94f};
    color_cities_outline = {0.12f, 0.14f, 0.17f, 0.82f};
    color_qth = {0.94f, 0.72f, 0.24f, 1.00f};
    color_qth_text = {1.00f, 0.98f, 0.90f, 0.98f};
    color_qth_outline = {0.12f, 0.13f, 0.15f, 0.90f};
    color_latlon = {0.55f, 0.63f, 0.72f, 0.24f};
    qth_label = "QTH";
    qth_latitude = 0.0;
    qth_longitude = 0.0;
    qth_coordinates_configured = false;

    try
    {
        const nlohmann::json &general = satdump::config::main_cfg["satdump_general"];
        if (general.contains("default_qth_label"))
            qth_label = general["default_qth_label"]["value"].get<std::string>();
        if (general.contains("qth_lat") && general.contains("qth_lon"))
        {
            qth_latitude = general["qth_lat"]["value"].get<double>();
            qth_longitude = general["qth_lon"]["value"].get<double>();
            qth_coordinates_configured = std::fabs(qth_latitude) > 1e-9 ||
                                         std::fabs(qth_longitude) > 1e-9 ||
                                         (!qth_label.empty() && qth_label != "QTH");
        }
        auto load_default = [&](const char *name, ImVec4 &target)
        {
            if (!general.contains(name) || !general[name].contains("value")) return;
            const std::vector<float> value = general[name]["value"].get<std::vector<float>>();
            if (value.size() >= 3)
            {
                target.x = clamp01(value[0]); target.y = clamp01(value[1]); target.z = clamp01(value[2]);
            }
        };
        load_default("default_borders_color", color_borders);
        load_default("default_shores_color", color_shores);
        load_default("default_cities_color", color_cities);
        load_default("default_qth_color", color_qth);
        load_default("default_latlon_color", color_latlon);
    }
    catch (const std::exception &) {}

    if (rgb_is(color_borders, 0.0f, 1.0f, 0.0f)) color_borders = {0.72f, 0.77f, 0.82f, 0.58f};
    if (rgb_is(color_shores, 1.0f, 1.0f, 0.0f)) color_shores = {0.88f, 0.82f, 0.62f, 0.62f};
    if (rgb_is(color_cities, 1.0f, 0.0f, 0.0f)) color_cities = {0.97f, 0.98f, 1.00f, 0.94f};
    if (rgb_is(color_qth, 1.0f, 0.0f, 1.0f)) color_qth = {0.94f, 0.72f, 0.24f, 1.00f};
    if (rgb_is(color_latlon, 0.0f, 0.0f, 1.0f)) color_latlon = {0.55f, 0.63f, 0.72f, 0.24f};
}

void OverlayHandler::clear_cache()
{
    map_cache.map.clear();
    shores_cache.map.clear();
    latlon_cache.map.clear();
    cities_cache = GrayscaleOverlayCache();
    cities_outline_cache = GrayscaleOverlayCache();
    qth_halo_cache = GrayscaleOverlayCache();
    qth_outline_cache = GrayscaleOverlayCache();
    qth_marker_cache = GrayscaleOverlayCache();
    qth_text_cache = GrayscaleOverlayCache();
}
