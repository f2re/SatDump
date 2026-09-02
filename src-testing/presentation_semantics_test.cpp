#include "products/processor/presentation_processor.h"
#include "logger.h"

#include <cstdlib>
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
            "геокоррекция");
    }

    bool validate_night_microphysics()
    {
        satdump::ImageProducts source;
        populate_products(source);
        satdump::ImageCompositeCfg composite;
        composite.equation = "1-(3/200-cch6+cch5)*(200/10.5), 1-(7/200-cch5+cch4)*(200/9.9), ((cch5*200+200-243.7)/(293.2-243.7))";
        composite.description_markdown = "descriptions/NightMicro.md";
        composite.calib_cfg = {
            {"cch4", {{"type", "temperature"}, {"min", 200}, {"max", 400}}},
            {"cch5", {{"type", "temperature"}, {"min", 200}, {"max", 400}}},
            {"cch6", {{"type", "temperature"}, {"min", 200}, {"max", 400}}}};
        PresentationSpec spec = make_spec(source, composite, nlohmann::json::object(), "Night Microphysics");
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
        satdump::ImageCompositeCfg composite;
        composite.equation = "ch4, ch2, ch1";
        PresentationSpec spec = make_spec(source, composite, nlohmann::json::object(), "Natural Color");
        return spec.pass.product == "Естественные цвета" &&
               spec.legend.kind == LegendKind::Categorical &&
               spec.legend.categories.size() >= 4 &&
               has_note(spec, "Компонент R");
    }

    bool validate_physical_sst_scale()
    {
        satdump::ImageProducts source;
        populate_products(source);
        satdump::ImageCompositeCfg composite;
        composite.channels = "cch5";
        composite.lua = "scripted_compos/sst_landmask.lua";
        composite.description_markdown = "descriptions/SST.md";
        composite.calib_cfg = {
            {"cch5", {{"type", "temperature"}, {"min", 270.65}, {"max", 307.15}}}};
        PresentationSpec spec = make_spec(source, composite, nlohmann::json::object(), "Sea Surface Temperature");
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
        satdump::ImageCompositeCfg composite;
        composite.equation = "ch4, ch2, ch1";
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
        std::cerr << "Usage: satdump-presentation-semantics-test <composite_profiles_ru.json>\n";
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

    std::cout << "Presentation semantic legend tests passed\n";
    return 0;
}
