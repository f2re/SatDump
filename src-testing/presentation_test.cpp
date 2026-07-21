#include "common/image/io.h"
#include "common/image/presentation.h"
#include "common/image/text.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
    image::Image make_source_image()
    {
        const int width = 1280;
        const int height = 620;
        image::Image source(8, width, height, 3);

        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                const double nx = (double)x / (double)(width - 1);
                const double ny = (double)y / (double)(height - 1);
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
            {"Проекция", "equirectangular"}};
        spec.pass.quality = "94%";
        spec.pass.quality_detail = "потери 0,8% · SNR 17,4 дБ";
        spec.branding = "SatDump 1.2.2 · Presentation smoke test";
        return spec;
    }

    bool validate_result(const image::Image &source, const image::Image &result, const std::string &label)
    {
        if (result.size() == 0)
        {
            std::cerr << label << ": renderer returned an empty image\n";
            return false;
        }
        if (result.width() != source.width())
        {
            std::cerr << label << ": source width was changed\n";
            return false;
        }
        if (result.height() <= source.height())
        {
            std::cerr << label << ": header/footer were not added\n";
            return false;
        }
        if (result.channels() != 3)
        {
            std::cerr << label << ": presentation output must be RGB\n";
            return false;
        }
        return true;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: satdump-presentation-test <font.ttf> [output-directory]\n";
        return 2;
    }

    const std::filesystem::path output_directory = argc >= 3 ? argv[2] : "presentation-test-output";
    std::filesystem::create_directories(output_directory);

    image::TextDrawer text_drawer;
    text_drawer.init_font(argv[1]);
    if (!text_drawer.font_ready())
    {
        std::cerr << "Could not initialize test font: " << argv[1] << "\n";
        return 3;
    }

    const image::Image source = make_source_image();

    image::presentation::PresentationSpec continuous = base_spec();
    continuous.legend.kind = image::presentation::LegendKind::Continuous;
    continuous.legend.title = "Яркостная температура";
    continuous.legend.subtitle = "Канал 10,8 мкм · физическая шкала без эквализации после окрашивания";
    continuous.legend.unit = "K";
    continuous.legend.color_stops = {
        {0.00, {0.105882, 0.094118, 0.266667}},
        {0.25, {0.117647, 0.368627, 0.545098}},
        {0.50, {0.141176, 0.627451, 0.576471}},
        {0.75, {0.588235, 0.788235, 0.349020}},
        {1.00, {0.988235, 0.905882, 0.145098}}};
    for (int index = 0; index < 8; index++)
    {
        const double position = (double)index / 7.0;
        continuous.legend.ticks.push_back({position, std::to_string(180 + index * 20)});
    }
    continuous.legend.notes = {
        "Яркостная температура излучающей поверхности; температура верхней границы облаков требует отдельного алгоритма восстановления."};

    image::Image continuous_image = image::presentation::render(source, text_drawer, continuous);
    if (!validate_result(source, continuous_image, "continuous"))
        return 10;
    image::save_img(continuous_image, (output_directory / "continuous.png").string());

    image::presentation::PresentationSpec categorical = base_spec();
    categorical.pass.product = "Классификация облачности";
    categorical.legend.kind = image::presentation::LegendKind::Categorical;
    categorical.legend.title = "Тип облачности";
    categorical.legend.subtitle = "Категориальный тематический продукт";
    categorical.legend.categories = {
        {{0.08, 0.10, 0.13}, "Ясно"},
        {{0.85, 0.90, 0.95}, "Низкая облачность"},
        {{0.47, 0.66, 0.80}, "Средняя облачность"},
        {{0.21, 0.36, 0.60}, "Высокая облачность"},
        {{0.55, 0.36, 0.66}, "Тонкая перистая"},
        {{0.85, 0.29, 0.29}, "Мощная конвективная"},
        {{0.39, 0.42, 0.45}, "Не определено"}};
    categorical.legend.notes = {"Цвета классов задаются алгоритмом классификации."};

    image::Image categorical_image = image::presentation::render(source, text_drawer, categorical);
    if (!validate_result(source, categorical_image, "categorical"))
        return 11;
    image::save_img(categorical_image, (output_directory / "categorical.png").string());

    image::presentation::PresentationSpec composite = base_spec();
    composite.pass.product = "Ночная микрофизика облаков";
    composite.legend.kind = image::presentation::LegendKind::Composite;
    composite.legend.title = "Состав RGB-композита";
    composite.legend.subtitle = "Условный многоканальный синтез для качественной интерпретации";
    composite.legend.components = {
        {"R", {0.95, 0.28, 0.28}, "", "", "", "T(12,0 мкм) − T(10,8 мкм)", "каналы 5 и 4 · разность яркостных температур · −4…2 K"},
        {"G", {0.25, 0.82, 0.48}, "", "", "", "T(10,8 мкм) − T(3,9 мкм)", "каналы 4 и 3 · разность яркостных температур · 0…15 K"},
        {"B", {0.30, 0.58, 1.00}, "", "", "", "T(10,8 мкм)", "канал 4 · яркостная температура · 243…293 K · инверсия"}};
    composite.legend.notes = {
        "Результирующий цвет зависит от трёх компонентов и не является самостоятельной физической величиной."};

    image::Image composite_image = image::presentation::render(source, text_drawer, composite);
    if (!validate_result(source, composite_image, "composite"))
        return 12;
    image::save_img(composite_image, (output_directory / "composite.png").string());

    std::cout << "Presentation smoke tests passed; artifacts: " << output_directory << "\n";
    return 0;
}
