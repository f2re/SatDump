#include "products/processor/presentation_processor.h"
#include "common/image/io.h"
#include "common/image/presentation_layout.h"
#include "common/image/text.h"
#include "logger.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
    using image::presentation::LegendKind;
    using image::presentation::PresentationSpec;

    bool contains(const std::string &value, const std::string &needle)
    {
        return value.find(needle) != std::string::npos;
    }

    bool has_detail(const PresentationSpec &spec, const std::string &label, const std::string &needle = "")
    {
        for (const auto &field : spec.pass.details)
            if (field.label == label && (needle.empty() || contains(field.value, needle)))
                return true;
        return false;
    }

    bool has_note(const PresentationSpec &spec, const std::string &needle)
    {
        for (const std::string &note : spec.legend.notes)
            if (contains(note, needle))
                return true;
        return false;
    }

    void populate_products(satdump::ImageProducts &result, const std::string &instrument = "msu_mr")
    {
        result.instrument_name = instrument;
        result.set_product_source("METEOR-M2-4");
        result.set_product_timestamp(1788259404);
        for (const std::string &channel : {"1", "2", "3", "4", "5", "6"})
        {
            satdump::ImageProducts::ImageHolder holder;
            holder.channel_name = channel;
            result.images.push_back(holder);
        }
    }

    PresentationSpec make_spec(satdump::ImageProducts &source,
                               const satdump::ImageCompositeCfg &composite,
                               const nlohmann::json &preset,
                               const std::string &name)
    {
        return satdump::product_presentation::build_spec(
            source,
            composite,
            preset,
            name,
            {1788259404.0, 1788260000.0},
            nlohmann::json::object(),
            "геокоррекция · контуры, береговая линия и города");
    }

    satdump::ImageCompositeCfg night_microphysics_composite()
    {
        satdump::ImageCompositeCfg composite;
        composite.equation = "1-(3/200-cch6+cch5)*(200/10.5), 1-(7/200-cch5+cch4)*(200/9.9), ((cch5*200+200-243.7)/(293.2-243.7))";
        composite.description_markdown = "descriptions/NightMicro.md";
        composite.calib_cfg = {
            {"cch4", {{"type", "temperature"}, {"min", 200}, {"max", 400}}},
            {"cch5", {{"type", "temperature"}, {"min", 200}, {"max", 400}}},
            {"cch6", {{"type", "temperature"}, {"min", 200}, {"max", 400}}}};
        return composite;
    }

    satdump::ImageCompositeCfg natural_color_composite()
    {
        satdump::ImageCompositeCfg composite;
        composite.equation = "ch4, ch2, ch1";
        return composite;
    }

    satdump::ImageCompositeCfg sst_composite()
    {
        satdump::ImageCompositeCfg composite;
        composite.channels = "cch5";
        composite.lua = "scripted_compos/sst_landmask.lua";
        composite.description_markdown = "descriptions/SST.md";
        composite.calib_cfg = {
            {"cch5", {{"type", "temperature"}, {"min", 270.65}, {"max", 307.15}}}};
        return composite;
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
                const double front = std::exp(-35.0 * std::pow(ny - (0.28 + 0.18 * std::sin(nx * 5.0)), 2.0));
                const double convection = std::exp(-95.0 * (std::pow(nx - 0.72, 2.0) + std::pow(ny - 0.46, 2.0)));
                source.setf(0, x, y, std::min(1.0, 0.12 + nx * 0.28 + front * 0.58 + convection * 0.42));
                source.setf(1, x, y, std::min(1.0, 0.18 + (1.0 - ny) * 0.24 + front * 0.55 + convection * 0.35));
                source.setf(2, x, y, std::min(1.0, 0.26 + ny * 0.18 + front * 0.50 + convection * 0.30));
            }
        }
        return source;
    }

    bool render_example(image::TextDrawer &drawer,
                        const image::Image &source,
                        const PresentationSpec &spec,
                        const std::filesystem::path &path)
    {
        image::Image rendered = image::presentation::render_layout(
            source, drawer, spec, image::presentation::LayoutKind::Editorial);
        if (rendered.size() == 0 || rendered.width() == 0 || rendered.height() <= source.height())
            return false;
        image::save_img(rendered, path.string());
        return std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
    }

    bool render_semantic_examples(const std::string &font_path, const std::filesystem::path &output_directory)
    {
        image::TextDrawer drawer;
        drawer.init_font(font_path);
        if (!drawer.font_ready())
            return false;
        std::filesystem::create_directories(output_directory);
        const image::Image source = make_source_image(1280, 720);

        satdump::ImageProducts night_products;
        populate_products(night_products);
        PresentationSpec night = make_spec(
            night_products, night_microphysics_composite(), nlohmann::json::object(), "Night Microphysics");
        if (!render_example(drawer, source, night,
                            output_directory / "semantic_night_microphysics_presentation.png"))
            return false;

        satdump::ImageProducts natural_products;
        populate_products(natural_products);
        PresentationSpec natural = make_spec(
            natural_products, natural_color_composite(), nlohmann::json::object(), "Natural Color");
        if (!render_example(drawer, source, natural,
                            output_directory / "semantic_natural_color_presentation.png"))
            return false;

        satdump::ImageProducts sst_products;
        populate_products(sst_products);
        PresentationSpec sst = make_spec(
            sst_products, sst_composite(), nlohmann::json::object(), "Sea Surface Temperature");
        if (!render_example(drawer, source, sst,
                            output_directory / "semantic_sst_presentation.png"))
            return false;

        return true;
    }

    bool validate_night_microphysics()
    {
        satdump::ImageProducts source;
        populate_products(source);
        PresentationSpec spec = make_spec(
            source, night_microphysics_composite(), nlohmann::json::object(), "Night Microphysics");
        return spec.pass.satellite == "Метеор-М №2-4" &&
               spec.pass.instrument == "МСУ-МР" &&
               spec.pass.product == "Ночная микрофизика облаков" &&
               spec.legend.kind == LegendKind::Categorical &&
               spec.legend.categories.size() >= 4 &&
               has_detail(spec, "Назначение", "туман") &&
               has_detail(spec, "Режим", "ночь") &&
               has_detail(spec, "Данные", "RGB") &&
               has_note(spec, "Компонент R");
    }

    bool validate_natural_color()
    {
        satdump::ImageProducts source;
        populate_products(source);
        PresentationSpec spec = make_spec(
            source, natural_color_composite(), nlohmann::json::object(), "Natural Color");
        return spec.pass.product == "Естественные цвета" &&
               spec.legend.kind == LegendKind::Categorical &&
               spec.legend.categories.size() >= 4 &&
               has_note(spec, "Компонент R");
    }

    bool validate_physical_sst_scale()
    {
        satdump::ImageProducts source;
        populate_products(source);
        PresentationSpec spec = make_spec(
            source, sst_composite(), nlohmann::json::object(), "Sea Surface Temperature");
        return spec.pass.product == "Температура поверхности моря" &&
               spec.legend.kind == LegendKind::Continuous &&
               spec.legend.unit == "°C" &&
               spec.legend.ticks.size() >= 6 &&
               contains(spec.legend.ticks.front().label, "-2,5") &&
               has_detail(spec, "Физика", "K");
    }

    bool validate_explicit_legend_wins()
    {
        satdump::ImageProducts source;
        populate_products(source);
        satdump::ImageCompositeCfg composite = natural_color_composite();
        nlohmann::json preset = {
            {"presentation", {
                {"title", "Пользовательский продукт"},
                {"legend", {
                    {"kind", "categorical"},
                    {"title", "Пользовательская легенда"},
                    {"categories", nlohmann::json::array({
                        {{"color", "#123456"}, {"label", "Проверенная категория"}}
                    })}
                }}
            }}
        };
        PresentationSpec spec = make_spec(source, composite, preset, "Natural Color");
        return spec.pass.product == "Пользовательский продукт" &&
               spec.legend.title == "Пользовательская легенда" &&
               spec.legend.categories.size() == 1 &&
               spec.legend.categories.front().label == "Проверенная категория" &&
               has_detail(spec, "Назначение");
    }

    bool validate_generic_cpp_fallback()
    {
        satdump::ImageProducts source;
        populate_products(source, "mtvza");
        satdump::ImageCompositeCfg composite;
        composite.channels = "ch1,ch2";
        composite.cpp = "custom_classifier";
        PresentationSpec spec = make_spec(source, composite, nlohmann::json::object(), "Experimental classifier");
        return spec.pass.instrument == "МТВЗА-ГЯ" &&
               spec.legend.kind == LegendKind::None &&
               spec.legend.title == "Как читать продукт" &&
               has_note(spec, "алгоритмом C++") &&
               has_detail(spec, "Обработка", "C++") &&
               has_detail(spec, "Данные", "C++");
    }
}

int main(int argc, char **argv)
{
    initLogger();
    if (argc < 2)
    {
        std::cerr << "Usage: satdump-presentation-semantics-test <composite_profiles_ru.json> [font.ttf output-directory]\n";
        return 2;
    }
#ifdef _WIN32
    _putenv_s("SATDUMP_PRESENTATION_PROFILES", argv[1]);
#else
    setenv("SATDUMP_PRESENTATION_PROFILES", argv[1], 1);
#endif

    if (!validate_night_microphysics())
    {
        std::cerr << "Night Microphysics semantic profile failed\n";
        return 3;
    }
    if (!validate_natural_color())
    {
        std::cerr << "Natural Color semantic profile failed\n";
        return 4;
    }
    if (!validate_physical_sst_scale())
    {
        std::cerr << "SST physical scale failed\n";
        return 5;
    }
    if (!validate_explicit_legend_wins())
    {
        std::cerr << "Explicit legend precedence failed\n";
        return 6;
    }
    if (!validate_generic_cpp_fallback())
    {
        std::cerr << "Generic C++ fallback failed\n";
        return 7;
    }
    if (argc >= 4 && !render_semantic_examples(argv[2], argv[3]))
    {
        std::cerr << "Semantic presentation rendering failed\n";
        return 8;
    }

    std::cout << "Presentation semantic legend tests passed\n";
    return 0;
}
