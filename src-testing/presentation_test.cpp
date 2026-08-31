#include "common/image/io.h"
#include "common/image/meta.h"
#include "common/image/presentation.h"
#include "common/image/presentation_layout.h"
#include "common/image/text.h"
#include "products/image_products.h"
#include "products/processor/presentation_outputs.h"
#include "logger.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void stage(const std::string &name)
    {
        std::cerr << "[presentation-test] " << name << std::endl;
    }

    image::Image make_source_image(int width, int height)
    {
        image::Image source(8, width, height, 3);
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                const double nx = width <= 1 ? 0.0 : (double)x / (double)(width - 1);
                const double ny = height <= 1 ? 0.0 : (double)y / (double)(height - 1);
                const double cloud_a = std::exp(-30.0 * std::pow(ny - (0.34 + 0.10 * std::sin(nx * 8.0)), 2.0));
                const double cloud_b = std::exp(-44.0 * std::pow(ny - (0.69 + 0.08 * std::cos(nx * 11.0)), 2.0));
                const double cloud = std::min(1.0, cloud_a * 0.72 + cloud_b * 0.55);

                source.setf(0, x, y, std::min(1.0, 0.07 + nx * 0.20 + cloud * 0.74));
                source.setf(1, x, y, std::min(1.0, 0.14 + (1.0 - ny) * 0.28 + cloud * 0.72));
                source.setf(2, x, y, std::min(1.0, 0.22 + ny * 0.18 + cloud * 0.68));
            }
        }
        return source;
    }

    image::presentation::PresentationSpec base_spec()
    {
        image::presentation::PresentationSpec spec;
        spec.pass.satellite = "METEOR-M №2-3";
        spec.pass.instrument = "МСУ-МР";
        spec.pass.product = "Яркостная температура облачной поверхности";
        spec.pass.acquisition_time = "20.07.2026 · 02:41:16–02:55:38 UTC";
        spec.pass.pass_summary = "нисходящий пролёт · макс. высота 67°";
        spec.pass.details = {
            {"NORAD", "57166"},
            {"Каналы", "5 (10,8 мкм)"},
            {"Приём", "137,9 МГц"},
            {"Проекция", "equirec"},
            {"Кадр", "адаптивный"}};
        spec.pass.quality = "94%";
        spec.pass.quality_detail = "потери 0,8% · SNR 17,4 дБ";
        spec.branding = "SatDump 1.2.2 · Presentation smoke test";
        return spec;
    }

    image::presentation::PresentationSpec continuous_spec()
    {
        image::presentation::PresentationSpec spec = base_spec();
        spec.legend.kind = image::presentation::LegendKind::Continuous;
        spec.legend.title = "Яркостная температура";
        spec.legend.subtitle = "Канал 10,8 мкм · физическая шкала без эквализации после окрашивания";
        spec.legend.unit = "K";
        spec.legend.color_stops = {
            {0.00, {0.105882, 0.094118, 0.266667}},
            {0.25, {0.117647, 0.368627, 0.545098}},
            {0.50, {0.141176, 0.627451, 0.576471}},
            {0.75, {0.588235, 0.788235, 0.349020}},
            {1.00, {0.988235, 0.905882, 0.145098}}};
        for (int index = 0; index < 8; index++)
        {
            const double position = (double)index / 7.0;
            spec.legend.ticks.push_back({position, std::to_string(180 + index * 20)});
        }
        spec.legend.notes = {
            "Яркостная температура излучающей поверхности; температура верхней границы облаков требует отдельного алгоритма восстановления."};
        return spec;
    }

    image::presentation::PresentationSpec categorical_spec()
    {
        image::presentation::PresentationSpec spec = base_spec();
        spec.pass.product = "Классификация облачности";
        spec.legend.kind = image::presentation::LegendKind::Categorical;
        spec.legend.title = "Тип облачности";
        spec.legend.subtitle = "Категориальный тематический продукт";
        spec.legend.categories = {
            {{0.08, 0.10, 0.13}, "Ясно"},
            {{0.85, 0.90, 0.95}, "Низкая облачность"},
            {{0.47, 0.66, 0.80}, "Средняя облачность"},
            {{0.21, 0.36, 0.60}, "Высокая облачность"},
            {{0.55, 0.36, 0.66}, "Тонкая перистая"},
            {{0.85, 0.29, 0.29}, "Мощная конвективная"},
            {{0.39, 0.42, 0.45}, "Не определено"}};
        spec.legend.notes = {"Цвета классов задаются алгоритмом классификации."};
        return spec;
    }

    image::presentation::PresentationSpec composite_spec()
    {
        image::presentation::PresentationSpec spec = base_spec();
        spec.pass.product = "Ночная микрофизика облаков";
        spec.legend.kind = image::presentation::LegendKind::Composite;
        spec.legend.title = "Состав RGB-композита";
        spec.legend.subtitle = "Условный многоканальный синтез для качественной интерпретации";
        spec.legend.components = {
            {"R", {0.95, 0.28, 0.28}, "", "", "", "T(12,0 мкм) − T(10,8 мкм)", "каналы 5 и 4 · разность яркостных температур · −4…2 K"},
            {"G", {0.25, 0.82, 0.48}, "", "", "", "T(10,8 мкм) − T(3,9 мкм)", "каналы 4 и 3 · разность яркостных температур · 0…15 K"},
            {"B", {0.30, 0.58, 1.00}, "", "", "", "T(10,8 мкм)", "канал 4 · яркостная температура · 243…293 K · инверсия"}};
        spec.legend.notes = {
            "Результирующий цвет зависит от трёх компонентов и не является самостоятельной физической величиной."};
        return spec;
    }

    image::presentation::PresentationSpec single_channel_color_spec()
    {
        image::presentation::PresentationSpec spec = base_spec();
        spec.pass.product = "Псевдоцветное представление канала 5";
        spec.legend.kind = image::presentation::LegendKind::Composite;
        spec.legend.title = "Состав многоканального композита";
        spec.legend.subtitle = "Старое неоднозначное описание";
        spec.legend.components = {
            {"IN", {0.30, 0.58, 1.00}, "", "", "", "ch5", "канал 5 · 10,8 мкм · яркостная температура"}};
        spec.legend.notes = {
            "Цвета синтезированы из перечисленных компонентов."};
        return spec;
    }

    std::pair<int, int> normalized_raster_dimensions(const image::Image &source)
    {
        const double short_side = (double)std::min(source.width(), source.height());
        const double long_side = (double)std::max(source.width(), source.height());

        double scale = 1.0;
        scale = std::max(scale, 720.0 / std::max(1.0, short_side));
        scale = std::max(scale, 1280.0 / std::max(1.0, long_side));
        scale = std::min(scale, 8.0);
        if (long_side * scale > 8192.0)
            scale = std::max(1.0, 8192.0 / long_side);

        return {
            std::max(1, (int)std::round((double)source.width() * scale)),
            std::max(1, (int)std::round((double)source.height() * scale))};
    }

    bool validate_result(const image::Image &source,
                         const image::Image &result,
                         image::presentation::LayoutKind layout,
                         const std::string &label)
    {
        if (result.size() == 0)
        {
            std::cerr << label << ": renderer returned an empty image\n";
            return false;
        }

        const std::pair<int, int> raster = normalized_raster_dimensions(source);
        if ((int)result.width() != raster.first)
        {
            std::cerr << label << ": unexpected normalized raster width; expected "
                      << raster.first << ", got " << result.width() << "\n";
            return false;
        }
        if ((int)result.height() <= raster.second)
        {
            std::cerr << label << ": presentation chrome was not added\n";
            return false;
        }
        if (result.channels() != 3)
        {
            std::cerr << label << ": presentation output must be RGB\n";
            return false;
        }

        const int chrome_height = (int)result.height() - raster.second;
        if (layout == image::presentation::LayoutKind::Minimal)
        {
            if (chrome_height > 180)
            {
                std::cerr << label << ": minimal output is not a thin one-line strip; chrome="
                          << chrome_height << " px\n";
                return false;
            }
        }
        else if (chrome_height > std::max(1800, raster.second * 2))
        {
            std::cerr << label << ": presentation chrome is unexpectedly large\n";
            return false;
        }
        return true;
    }

    bool validate_transform()
    {
        image::Image source(8, 3, 2, 1);
        source.set(0, 0, 0, 10);
        source.set(0, 1, 0, 20);
        source.set(0, 2, 0, 30);
        source.set(0, 0, 1, 40);
        source.set(0, 1, 1, 50);
        source.set(0, 2, 1, 60);

        image::Image vertical = image::presentation::apply_transform(source, image::presentation::RasterTransform::FlipVertical);
        image::Image horizontal = image::presentation::apply_transform(source, image::presentation::RasterTransform::FlipHorizontal);
        image::Image rotated = image::presentation::apply_transform(source, image::presentation::RasterTransform::Rotate180);

        return vertical.get(0, 0, 0) == 40 && vertical.get(0, 2, 1) == 30 &&
               horizontal.get(0, 0, 0) == 30 && horizontal.get(0, 2, 1) == 40 &&
               rotated.get(0, 0, 0) == 60 && rotated.get(0, 2, 1) == 10;
    }

    bool validate_orientation_analysis()
    {
        image::Image source(8, 64, 128, 3);
        satdump::ImageProducts products;
        satdump::product_presentation::OutputSettings settings;
        settings.north_up = true;
        settings.orientation_mode = "auto";

        products.contents["pass_direction"] = "ascending";
        image::presentation::OrientationInfo ascending = satdump::product_presentation::analyze_orientation(
            source, products, {}, nlohmann::json(), "композит", settings);
        if (ascending.transform != image::presentation::RasterTransform::FlipVertical || !ascending.north_up_verified)
            return false;

        products.contents["pass_direction"] = "descending";
        image::presentation::OrientationInfo descending = satdump::product_presentation::analyze_orientation(
            source, products, {}, nlohmann::json(), "композит", settings);
        if (descending.transform != image::presentation::RasterTransform::None || !descending.north_up_verified)
            return false;

        image::Image projected(8, 64, 128, 3);
        image::set_metadata_proj_cfg(projected, {
            {"type", "equirec"},
            {"offset_x", -10.0},
            {"offset_y", 40.0},
            {"scalar_x", 0.25},
            {"scalar_y", 0.25}});
        products.contents.erase("pass_direction");
        image::presentation::OrientationInfo upside_down_projection = satdump::product_presentation::analyze_orientation(
            projected, products, {}, nlohmann::json(), "географическая проекция", settings);
        return upside_down_projection.transform == image::presentation::RasterTransform::FlipVertical &&
               upside_down_projection.north_up_verified && upside_down_projection.inferred_from_projection;
    }

    bool render_and_save(const image::Image &source,
                         image::TextDrawer &drawer,
                         const image::presentation::PresentationSpec &spec,
                         image::presentation::LayoutKind layout,
                         const std::filesystem::path &path,
                         const std::string &label)
    {
        stage("render " + label);
        image::Image output = image::presentation::render_layout(source, drawer, spec, layout);
        stage("validate " + label);
        if (!validate_result(source, output, layout, label))
            return false;
        stage("save " + label);
        image::save_img(output, path.string());
        return std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
    }
}

int main(int argc, char **argv)
{
    initLogger();

    if (argc < 2)
    {
        std::cerr << "Usage: satdump-presentation-test <font.ttf> [output-directory]\n";
        return 2;
    }

    const std::filesystem::path output_directory = argc >= 3 ? argv[2] : "presentation-test-output";
    std::filesystem::create_directories(output_directory);

    stage("initialize font");
    image::TextDrawer text_drawer;
    text_drawer.init_font(argv[1]);
    if (!text_drawer.font_ready())
    {
        std::cerr << "Could not initialize test font: " << argv[1] << "\n";
        return 3;
    }

    stage("validate raster transforms");
    if (!validate_transform())
    {
        std::cerr << "Raster transform tests failed\n";
        return 4;
    }

    stage("validate north-up analysis");
    if (!validate_orientation_analysis())
    {
        std::cerr << "North-up orientation analysis tests failed\n";
        return 5;
    }

    stage("create synthetic rasters");
    const image::Image landscape = make_source_image(1280, 620);
    const image::Image portrait = make_source_image(720, 1320);
    const image::Image small_receiver_frame = make_source_image(320, 180);
    if (image::presentation::classify_frame(landscape) != image::presentation::FrameKind::Landscape ||
        image::presentation::classify_frame(portrait) != image::presentation::FrameKind::Portrait)
    {
        std::cerr << "Frame classification tests failed\n";
        return 6;
    }

    if (!render_and_save(landscape, text_drawer, continuous_spec(), image::presentation::LayoutKind::Editorial,
                         output_directory / "continuous_editorial_landscape.png", "continuous editorial landscape"))
        return 10;
    if (!render_and_save(landscape, text_drawer, continuous_spec(), image::presentation::LayoutKind::Minimal,
                         output_directory / "continuous_minimal_landscape.png", "continuous minimal landscape"))
        return 11;
    if (!render_and_save(landscape, text_drawer, categorical_spec(), image::presentation::LayoutKind::Editorial,
                         output_directory / "categorical_editorial_landscape.png", "categorical editorial landscape"))
        return 12;
    if (!render_and_save(landscape, text_drawer, composite_spec(), image::presentation::LayoutKind::Editorial,
                         output_directory / "composite_editorial_landscape.png", "composite editorial landscape"))
        return 13;
    if (!render_and_save(landscape, text_drawer, composite_spec(), image::presentation::LayoutKind::Minimal,
                         output_directory / "composite_minimal_landscape.png", "composite minimal landscape"))
        return 14;
    if (!render_and_save(portrait, text_drawer, continuous_spec(), image::presentation::LayoutKind::Editorial,
                         output_directory / "continuous_editorial_portrait.png", "continuous editorial portrait"))
        return 15;
    if (!render_and_save(portrait, text_drawer, continuous_spec(), image::presentation::LayoutKind::Minimal,
                         output_directory / "continuous_minimal_portrait.png", "continuous minimal portrait"))
        return 16;
    if (!render_and_save(small_receiver_frame, text_drawer, continuous_spec(), image::presentation::LayoutKind::Minimal,
                         output_directory / "small_receiver_minimal.png", "small receiver minimal"))
        return 17;
    if (!render_and_save(small_receiver_frame, text_drawer, continuous_spec(), image::presentation::LayoutKind::Editorial,
                         output_directory / "small_receiver_editorial.png", "small receiver editorial"))
        return 18;
    if (!render_and_save(landscape, text_drawer, single_channel_color_spec(), image::presentation::LayoutKind::Editorial,
                         output_directory / "single_channel_color_editorial.png", "single-channel color editorial"))
        return 19;

    stage("all smoke tests passed");
    std::cout << "Presentation smoke tests passed; artifacts: " << output_directory << "\n";
    return 0;
}
