#include "common/image/text.h"
#include "products/image_products.h"
#include "products/processor/presentation_outputs.h"

#include "nlohmann/json.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    image::Image make_source()
    {
        const int width = 640;
        const int height = 960;
        image::Image source(8, width, height, 3);
        for (int y = 0; y < height; y++)
        {
            const double vertical = (double)y / (double)(height - 1);
            for (int x = 0; x < width; x++)
            {
                const double horizontal = (double)x / (double)(width - 1);
                source.setf(0, x, y, 0.08 + 0.80 * vertical);
                source.setf(1, x, y, 0.12 + 0.55 * horizontal);
                source.setf(2, x, y, 0.18 + 0.62 * (1.0 - vertical));
            }
        }
        return source;
    }

    bool nonempty(const std::filesystem::path &path)
    {
        return std::filesystem::exists(path) && std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) > 0;
    }

    nlohmann::json load_json(const std::filesystem::path &path)
    {
        std::ifstream input(path.string());
        nlohmann::json value;
        input >> value;
        return value;
    }

    bool validate_settings()
    {
        const nlohmann::json preset = {
            {"presentation",
             {
                 {"enabled", true},
                 {"outputs", { {"minimal", true}, {"presentation", false}, {"legacy_alias", true} }},
                 {"orientation", { {"mode", "rotate_180"}, {"north_up", false} }}
             }}
        };

        const satdump::product_presentation::OutputSettings settings =
            satdump::product_presentation::resolve_output_settings(preset);
        return settings.enabled && settings.save_minimal && !settings.save_editorial && settings.save_legacy_alias &&
               !settings.north_up && settings.orientation_mode == "rotate_180";
    }

    bool validate_sidecar(const std::filesystem::path &path,
                          const std::string &layout,
                          const std::string &transform)
    {
        if (!nonempty(path))
            return false;

        const nlohmann::json sidecar = load_json(path);
        return sidecar.value("schema", "") == "satdump.presentation/2" &&
               sidecar.value("layout", "") == layout &&
               sidecar.contains("orientation") &&
               sidecar["orientation"].value("transform", "") == transform &&
               sidecar["orientation"].value("north_up_requested", false) &&
               sidecar["orientation"].value("north_up_verified", false) &&
               sidecar.contains("source") && sidecar["source"].value("frame", "") == "вертикальный" &&
               sidecar.contains("legend") && sidecar["legend"].value("kind", "") == "composite";
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: satdump-presentation-output-test <font.ttf> [output-directory]\n";
        return 2;
    }

    const std::filesystem::path output_directory = argc >= 3 ? argv[2] : "presentation-output-test";
    std::filesystem::remove_all(output_directory);
    std::filesystem::create_directories(output_directory);

    image::TextDrawer text_drawer;
    text_drawer.init_font(argv[1]);
    if (!text_drawer.font_ready())
    {
        std::cerr << "Could not initialize test font: " << argv[1] << "\n";
        return 3;
    }

    if (!validate_settings())
    {
        std::cerr << "Presentation output settings test failed\n";
        return 4;
    }

    image::Image source = make_source();
    const int original_top = source.get(0, 0, 0);
    const int original_bottom = source.get(0, 0, source.height() - 1);

    satdump::ImageProducts products;
    products.instrument_name = "МСУ-МР";
    products.set_product_source("METEOR-M №2-3");
    products.set_product_timestamp((time_t)1784515700);
    products.contents["pass_direction"] = "ascending";

    satdump::ImageProducts::ImageHolder holder;
    holder.channel_name = "5";
    holder.image = source;
    holder.abs_index = -2;
    products.images.push_back(holder);

    satdump::ImageCompositeCfg composite;
    composite.channels = "ch5,ch4,ch3";
    composite.description_markdown = "Тест условного RGB-композита";

    const nlohmann::json preset = {
        {"presentation",
         {
             {"enabled", true},
             {"title", "Ночная микрофизика облаков"},
             {"outputs", { {"minimal", true}, {"presentation", true}, {"legacy_alias", true} }},
             {"orientation", { {"mode", "auto"}, {"north_up", true} }},
             {"minimal", { {"branding", "SatDump test · minimal"} }},
             {"editorial", { {"branding", "SatDump test · editorial"} }}
         }}
    };

    const nlohmann::json metadata = {
        {"acquisition", { {"downlink", { {"center_frequency_hz", 137900000}, {"sample_rate_hz", 240000} }} }},
        {"quality", { {"score", 96}, {"packet_loss_percent", 0.3}, {"snr_db", 18.2} }}
    };
    const std::vector<double> timestamps = {1784515276.0, 1784515700.0, 1784516138.0};
    const std::filesystem::path base = output_directory / "meteor_cloud_rgb";

    const satdump::product_presentation::OutputResult result =
        satdump::product_presentation::save_outputs(
            source,
            text_drawer,
            products,
            composite,
            preset,
            "Ночная микрофизика облаков",
            timestamps,
            metadata,
            "геокоррекция · картографические слои",
            base.string());

    if (!result.minimal || !result.editorial || !result.legacy_alias || !result.any())
    {
        std::cerr << "Not all configured presentation outputs were saved\n";
        return 5;
    }
    if (result.orientation.transform != image::presentation::RasterTransform::FlipVertical ||
        !result.orientation.north_up_verified || result.orientation.pass_direction != "ascending")
    {
        std::cerr << "Ascending pass was not normalized to north-up\n";
        return 6;
    }

    const std::filesystem::path minimal_png = base.string() + "_annotated_minimal.png";
    const std::filesystem::path minimal_json = base.string() + "_annotated_minimal.json";
    const std::filesystem::path editorial_png = base.string() + "_annotated_presentation.png";
    const std::filesystem::path editorial_json = base.string() + "_annotated_presentation.json";
    const std::filesystem::path legacy_png = base.string() + "_annotated.png";
    const std::filesystem::path legacy_json = base.string() + "_annotated.json";

    for (const std::filesystem::path &path : {minimal_png, editorial_png, legacy_png})
    {
        if (!nonempty(path))
        {
            std::cerr << "Missing output: " << path << "\n";
            return 7;
        }
    }
    if (!validate_sidecar(minimal_json, "minimal", "flip_vertical") ||
        !validate_sidecar(editorial_json, "editorial", "flip_vertical") ||
        !validate_sidecar(legacy_json, "editorial", "flip_vertical"))
    {
        std::cerr << "Presentation sidecar validation failed\n";
        return 8;
    }

    if (source.get(0, 0, 0) != original_top || source.get(0, 0, source.height() - 1) != original_bottom)
    {
        std::cerr << "Scientific source raster was modified in place\n";
        return 9;
    }

    satdump::product_presentation::OutputSettings keep_settings;
    keep_settings.orientation_mode = "keep";
    keep_settings.north_up = true;
    const image::presentation::OrientationInfo keep = satdump::product_presentation::analyze_orientation(
        source, products, timestamps, metadata, "композит", keep_settings);
    if (keep.transform != image::presentation::RasterTransform::None || keep.north_up_verified)
    {
        std::cerr << "Manual keep mode was not respected\n";
        return 10;
    }

    std::cout << "Presentation output integration tests passed; artifacts: " << output_directory << "\n";
    return 0;
}
