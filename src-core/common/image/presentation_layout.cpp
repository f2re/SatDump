#include "presentation_layout.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace image
{
    namespace presentation
    {
        namespace
        {
            struct MinimalStyle
            {
                int font_size = 22;
                int padding_x = 18;
                int padding_y = 8;
                int accent = 2;
                int header_height = 42;
            };

            struct FittedLine
            {
                std::string text;
                int font_size = 16;
            };

            double clamp_value(double value, double minimum, double maximum)
            {
                return std::max(minimum, std::min(maximum, value));
            }

            int scaled(double value, double scale)
            {
                return std::max(1, (int)std::round(value * scale));
            }

            TextSize measured(TextDrawer &drawer, int size, const std::string &text)
            {
                TextSize result = drawer.measure_text(size, text);
                if (result.line_height <= 0)
                    result.line_height = std::max(1, (int)std::round(size * 1.25));
                if (result.height <= 0 && !text.empty())
                    result.height = result.line_height;
                if (result.width <= 0 && !text.empty())
                    result.width = std::max(1, (int)std::round(text.size() * size * 0.55));
                return result;
            }

            int line_height(TextDrawer &drawer, int font_size)
            {
                return measured(drawer, font_size, "Ag").line_height;
            }

            void fill_rect(Image &image, int x0, int y0, int x1, int y1, const Color &color)
            {
                x0 = std::max(0, std::min((int)image.width(), x0));
                x1 = std::max(0, std::min((int)image.width(), x1));
                y0 = std::max(0, std::min((int)image.height(), y0));
                y1 = std::max(0, std::min((int)image.height(), y1));
                if (x1 <= x0 || y1 <= y0)
                    return;

                for (int y = y0; y < y1; y++)
                    image.draw_line(x0, y, x1 - 1, y, color);
            }

            Image make_rgb(const Image &source)
            {
                Image rgb;
                if (source.channels() == 3)
                {
                    rgb = source;
                }
                else if (source.channels() == 1 || source.channels() == 4)
                {
                    rgb = source;
                    rgb.to_rgb();
                }
                else
                {
                    rgb.init(source.depth(), source.width(), source.height(), 3);
                    for (size_t y = 0; y < source.height(); y++)
                        for (size_t x = 0; x < source.width(); x++)
                        {
                            const int value = source.get(0, x, y);
                            rgb.set(0, x, y, value);
                            rgb.set(1, x, y, value);
                            rgb.set(2, x, y, value);
                        }
                }
                return rgb;
            }

            Image normalize_presentation_raster(const Image &source)
            {
                Image rgb = make_rgb(source);
                if (rgb.size() == 0 || rgb.width() == 0 || rgb.height() == 0)
                    return rgb;

                const double short_side = (double)std::min(rgb.width(), rgb.height());
                const double long_side = (double)std::max(rgb.width(), rgb.height());

                // Small products from low-resolution receivers used to produce an
                // unreadable card whose chrome was larger than the image itself.
                // Upscale only; never discard source detail by downscaling here.
                double scale = 1.0;
                scale = std::max(scale, 720.0 / std::max(1.0, short_side));
                scale = std::max(scale, 1280.0 / std::max(1.0, long_side));
                scale = std::min(scale, 8.0);

                // Guard against pathological, very narrow swaths consuming excessive
                // memory after enlargement.
                const double maximum_dimension = 8192.0;
                if (long_side * scale > maximum_dimension)
                    scale = std::max(1.0, maximum_dimension / long_side);

                if (scale > 1.01)
                {
                    const int width = std::max(1, (int)std::round((double)rgb.width() * scale));
                    const int height = std::max(1, (int)std::round((double)rgb.height() * scale));
                    rgb.resize_bilinear(width, height);
                }
                return rgb;
            }

            std::string collapse_spaces(const std::string &value)
            {
                std::string output;
                bool previous_space = false;
                for (char character : value)
                {
                    const bool is_space = character == ' ' || character == '\t' ||
                                          character == '\r' || character == '\n';
                    if (is_space)
                    {
                        if (!previous_space && !output.empty())
                            output.push_back(' ');
                    }
                    else
                    {
                        output.push_back(character);
                    }
                    previous_space = is_space;
                }
                while (!output.empty() && output.back() == ' ')
                    output.pop_back();
                return output;
            }

            std::string join_nonempty(const std::vector<std::string> &values, const std::string &separator)
            {
                std::string output;
                for (const std::string &raw : values)
                {
                    const std::string value = collapse_spaces(raw);
                    if (value.empty())
                        continue;
                    if (!output.empty())
                        output += separator;
                    output += value;
                }
                return output;
            }

            std::string detail_value(const PresentationSpec &spec, const std::string &label)
            {
                for (const MetadataField &field : spec.pass.details)
                    if (field.label == label && !field.value.empty())
                        return collapse_spaces(field.value);
                return "";
            }

            std::string remove_parenthetical_details(const std::string &value)
            {
                std::string output;
                int depth = 0;
                for (char character : value)
                {
                    if (character == '(')
                    {
                        depth++;
                        continue;
                    }
                    if (character == ')')
                    {
                        depth = std::max(0, depth - 1);
                        continue;
                    }
                    if (depth == 0)
                        output.push_back(character);
                }
                return collapse_spaces(output);
            }

            std::vector<size_t> utf8_boundaries(const std::string &value)
            {
                std::vector<size_t> boundaries;
                boundaries.push_back(0);
                size_t index = 0;
                while (index < value.size())
                {
                    const unsigned char lead = (unsigned char)value[index];
                    size_t length = 1;
                    if ((lead & 0xE0) == 0xC0)
                        length = 2;
                    else if ((lead & 0xF0) == 0xE0)
                        length = 3;
                    else if ((lead & 0xF8) == 0xF0)
                        length = 4;
                    index = std::min(value.size(), index + length);
                    boundaries.push_back(index);
                }
                return boundaries;
            }

            std::string ellipsize_end(TextDrawer &drawer,
                                      const std::string &value,
                                      int font_size,
                                      int maximum_width)
            {
                if (value.empty() || measured(drawer, font_size, value).width <= maximum_width)
                    return value;

                const std::string ellipsis = "...";
                if (measured(drawer, font_size, ellipsis).width > maximum_width)
                    return "";

                const std::vector<size_t> boundaries = utf8_boundaries(value);
                size_t low = 0;
                size_t high = boundaries.empty() ? 0 : boundaries.size() - 1;
                while (low < high)
                {
                    const size_t middle = (low + high + 1) / 2;
                    const std::string candidate = value.substr(0, boundaries[middle]) + ellipsis;
                    if (measured(drawer, font_size, candidate).width <= maximum_width)
                        low = middle;
                    else
                        high = middle - 1;
                }
                return value.substr(0, boundaries[low]) + ellipsis;
            }

            std::string labeled(const std::string &label, const std::string &value)
            {
                return value.empty() ? "" : label + value;
            }

            std::vector<std::string> minimal_line_candidates(const PresentationSpec &spec)
            {
                const std::string identity = join_nonempty(
                    {spec.pass.satellite, spec.pass.instrument}, " / ");
                const std::string channels = detail_value(spec, "Каналы");
                const std::string compact_channels = remove_parenthetical_details(channels);
                const std::string projection = detail_value(spec, "Проекция");
                const std::string quality = join_nonempty(
                    {spec.pass.quality, spec.pass.quality_detail}, " ");
                const std::string time = collapse_spaces(spec.pass.acquisition_time);
                const std::string product = collapse_spaces(spec.pass.product);

                std::vector<std::string> candidates;
                candidates.push_back(join_nonempty(
                    {identity,
                     product,
                     time,
                     labeled("Каналы: ", channels),
                     labeled("Проекция: ", projection),
                     labeled("Качество: ", quality)},
                    "  ·  "));
                candidates.push_back(join_nonempty(
                    {identity,
                     product,
                     time,
                     labeled("Каналы: ", compact_channels.empty() ? channels : compact_channels),
                     labeled("Проекция: ", projection)},
                    "  ·  "));
                candidates.push_back(join_nonempty(
                    {identity,
                     time,
                     labeled("Каналы: ", compact_channels.empty() ? channels : compact_channels),
                     product},
                    "  ·  "));
                candidates.push_back(join_nonempty(
                    {identity,
                     time,
                     labeled("Каналы: ", compact_channels.empty() ? channels : compact_channels)},
                    "  ·  "));

                std::vector<std::string> unique;
                for (const std::string &candidate : candidates)
                {
                    if (candidate.empty())
                        continue;
                    if (std::find(unique.begin(), unique.end(), candidate) == unique.end())
                        unique.push_back(candidate);
                }
                if (unique.empty())
                    unique.push_back("Спутниковый снимок");
                return unique;
            }

            FittedLine fit_minimal_line(TextDrawer &drawer,
                                        const PresentationSpec &spec,
                                        int width,
                                        int horizontal_padding)
            {
                const int available = std::max(1, width - horizontal_padding * 2);
                const int preferred = std::max(18, std::min(96, (int)std::round(width / 58.0)));
                const int readable_floor = std::max(15, (int)std::round(preferred * 0.72));
                const std::vector<std::string> candidates = minimal_line_candidates(spec);

                for (const std::string &candidate : candidates)
                {
                    for (int font_size = preferred; font_size >= readable_floor; font_size--)
                    {
                        if (measured(drawer, font_size, candidate).width <= available)
                            return {candidate, font_size};
                    }
                }

                const std::string essential = candidates.back();
                for (int font_size = readable_floor - 1; font_size >= 12; font_size--)
                {
                    if (measured(drawer, font_size, essential).width <= available)
                        return {essential, font_size};
                }
                return {ellipsize_end(drawer, essential, 12, available), 12};
            }

            MinimalStyle build_minimal_style(TextDrawer &drawer,
                                             const PresentationSpec &spec,
                                             size_t width)
            {
                MinimalStyle style;
                style.padding_x = std::max(12, std::min(64, (int)std::round(width / 70.0)));
                const FittedLine fitted = fit_minimal_line(
                    drawer, spec, (int)width, style.padding_x);
                style.font_size = fitted.font_size;
                style.padding_y = std::max(5, (int)std::round(style.font_size * 0.34));
                style.accent = std::max(2, (int)std::round(style.font_size * 0.10));
                style.header_height = line_height(drawer, style.font_size) +
                                      style.padding_y * 2 + style.accent;
                return style;
            }

            void normalize_legend_for_actual_raster(PresentationSpec &spec,
                                                    const Image &source)
            {
                if (spec.legend.kind != LegendKind::Composite)
                    return;

                if (spec.legend.components.size() == 1)
                {
                    CompositeComponent &component = spec.legend.components.front();
                    component.component = source.channels() >= 3 ? "RGB" : "Канал";
                    spec.legend.title = source.channels() >= 3
                                            ? "Цветовое представление одного исходного канала"
                                            : "Одноканальный продукт";
                    if (spec.legend.subtitle.empty() ||
                        spec.legend.subtitle.find("многоканаль") != std::string::npos)
                    {
                        spec.legend.subtitle =
                            "Это не трёхканальная композиция: цвет или яркость построены "
                            "из одного физического входного канала.";
                    }
                    spec.legend.notes.clear();
                    spec.legend.notes.push_back(
                        "Один входной канал может быть показан в RGB после LUT, Lua/C++-"
                        "обработки или копирования яркости в три цветовые компоненты.");
                }
                else if (spec.legend.components.size() == 2)
                {
                    spec.legend.title = "Двухканальный цветовой синтез";
                }
                else if (!spec.legend.components.empty() &&
                         spec.legend.components.size() != 3 &&
                         spec.legend.components.size() != 4)
                {
                    spec.legend.title = "Входные каналы цветового синтеза";
                }
            }
        }

        FrameKind classify_frame(const Image &source)
        {
            if (source.width() == 0 || source.height() == 0)
                return FrameKind::Square;
            const double aspect = (double)source.width() / (double)source.height();
            if (aspect < 0.82)
                return FrameKind::Portrait;
            if (aspect > 1.22)
                return FrameKind::Landscape;
            return FrameKind::Square;
        }

        std::string frame_kind_name(FrameKind frame)
        {
            if (frame == FrameKind::Portrait)
                return "вертикальный";
            if (frame == FrameKind::Landscape)
                return "горизонтальный";
            return "квадратный";
        }

        std::string raster_transform_name(RasterTransform transform)
        {
            if (transform == RasterTransform::FlipVertical)
                return "flip_vertical";
            if (transform == RasterTransform::FlipHorizontal)
                return "flip_horizontal";
            if (transform == RasterTransform::Rotate180)
                return "rotate_180";
            return "none";
        }

        Image apply_transform(const Image &source, RasterTransform transform)
        {
            if (transform == RasterTransform::None)
                return source;

            Image output(source.depth(), source.width(), source.height(), source.channels());
            for (int channel = 0; channel < source.channels(); channel++)
            {
                for (size_t y = 0; y < source.height(); y++)
                {
                    for (size_t x = 0; x < source.width(); x++)
                    {
                        size_t source_x = x;
                        size_t source_y = y;
                        if (transform == RasterTransform::FlipVertical ||
                            transform == RasterTransform::Rotate180)
                            source_y = source.height() - 1 - y;
                        if (transform == RasterTransform::FlipHorizontal ||
                            transform == RasterTransform::Rotate180)
                            source_x = source.width() - 1 - x;
                        output.set(channel, x, y,
                                   source.get(channel, source_x, source_y));
                    }
                }
            }
            return output;
        }

        Image render_minimal(const Image &source,
                             TextDrawer &text_drawer,
                             const PresentationSpec &spec)
        {
            Image rgb = normalize_presentation_raster(source);
            PresentationSpec tuned = spec;
            normalize_legend_for_actual_raster(tuned, source);

            const MinimalStyle style =
                build_minimal_style(text_drawer, tuned, rgb.width());
            const FittedLine line = fit_minimal_line(
                text_drawer, tuned, (int)rgb.width(), style.padding_x);

            Image output(rgb.depth(), rgb.width(),
                         style.header_height + rgb.height(), 3);
            output.fill_color(tuned.theme.panel);
            output.draw_image(0, rgb, 0, style.header_height);

            fill_rect(output, 0, 0, output.width(), style.header_height,
                      tuned.theme.panel);
            fill_rect(output, 0, style.header_height - style.accent,
                      output.width(), style.header_height, tuned.theme.accent);

            const int y = std::max(
                0, (style.header_height - style.accent -
                    line_height(text_drawer, line.font_size)) /
                       2);
            if (!line.text.empty())
                text_drawer.draw_text(output, style.padding_x, y,
                                      tuned.theme.text, line.font_size, line.text);
            return output;
        }

        Image render_layout(const Image &source,
                            TextDrawer &text_drawer,
                            const PresentationSpec &spec,
                            LayoutKind layout)
        {
            if (layout == LayoutKind::Minimal)
                return render_minimal(source, text_drawer, spec);

            Image normalized = normalize_presentation_raster(source);
            PresentationSpec tuned = spec;
            normalize_legend_for_actual_raster(tuned, source);

            const FrameKind frame = classify_frame(normalized);
            if (frame == FrameKind::Portrait)
            {
                tuned.theme.reference_width = 980;
                tuned.theme.minimum_scale =
                    std::max(tuned.theme.minimum_scale, 0.82);
            }
            else if (frame == FrameKind::Landscape)
            {
                tuned.theme.reference_width = 1450;
                tuned.theme.minimum_scale =
                    std::max(tuned.theme.minimum_scale, 0.88);
            }
            else
            {
                tuned.theme.reference_width = 1280;
                tuned.theme.minimum_scale =
                    std::max(tuned.theme.minimum_scale, 0.86);
            }

            // Large scientific rasters are usually viewed fitted to a monitor.
            // Let the typography grow with the raster so it remains readable after
            // that fit-to-screen downscaling.
            tuned.theme.maximum_scale =
                std::max(tuned.theme.maximum_scale, 5.0);
            return render(normalized, text_drawer, tuned);
        }
    }
}
