#include "presentation_layout.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace image
{
    namespace presentation
    {
        namespace
        {
            struct CompactStyle
            {
                int title = 24;
                int body = 16;
                int small = 13;
                int padding = 18;
                int gap = 8;
                int accent = 3;
                int legend_bar = 18;
                bool portrait = false;
                bool landscape = false;
            };

            double clamp_value(double value, double minimum, double maximum)
            {
                return std::max(minimum, std::min(maximum, value));
            }

            int scaled(double value, double scale)
            {
                return std::max(1, (int)std::round(value * scale));
            }

            Color normalized_color(const Color &color, const Color &fallback)
            {
                Color output = fallback;
                for (size_t i = 0; i < std::min<size_t>(3, color.size()); i++)
                    output[i] = clamp_value(color[i], 0.0, 1.0);
                return output;
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

            std::vector<std::string> wrap_text(TextDrawer &drawer, const std::string &text, int font_size, int max_width)
            {
                std::vector<std::string> output;
                if (text.empty() || max_width <= 0)
                    return output;

                std::stringstream paragraphs(text);
                std::string paragraph;
                while (std::getline(paragraphs, paragraph, '\n'))
                {
                    std::stringstream words(paragraph);
                    std::string word;
                    std::string line;
                    while (words >> word)
                    {
                        const std::string candidate = line.empty() ? word : line + " " + word;
                        if (!line.empty() && measured(drawer, font_size, candidate).width > max_width)
                        {
                            output.push_back(line);
                            line = word;
                        }
                        else
                        {
                            line = candidate;
                        }
                    }
                    if (!line.empty())
                        output.push_back(line);
                    else if (paragraph.empty())
                        output.push_back("");
                }
                return output;
            }

            std::string join_nonempty(const std::vector<std::string> &values, const std::string &separator)
            {
                std::string output;
                for (const std::string &value : values)
                {
                    if (value.empty())
                        continue;
                    if (!output.empty())
                        output += separator;
                    output += value;
                }
                return output;
            }

            std::string join_fields(const std::vector<MetadataField> &fields)
            {
                std::vector<std::string> parts;
                for (const MetadataField &field : fields)
                {
                    if (field.value.empty())
                        continue;
                    parts.push_back(field.label.empty() ? field.value : field.label + ": " + field.value);
                }
                return join_nonempty(parts, "  ·  ");
            }

            std::string component_description(const CompositeComponent &component)
            {
                if (!component.description.empty())
                    return component.description;

                std::string result = join_nonempty({component.channel, component.spectral_range, component.quantity}, " · ");
                if (!component.formula.empty())
                {
                    if (!result.empty())
                        result += " | ";
                    result += component.formula;
                }
                return result;
            }

            Color component_color(const CompositeComponent &component, const Theme &theme)
            {
                if (component.component == "R" || component.component == "r")
                    return theme.red_component;
                if (component.component == "G" || component.component == "g")
                    return theme.green_component;
                if (component.component == "B" || component.component == "b")
                    return theme.blue_component;
                return normalized_color(component.marker_color, theme.accent);
            }

            Color sample_stops(const std::vector<ColorStop> &input, double position, const Theme &theme)
            {
                if (input.empty())
                    return theme.accent;

                std::vector<ColorStop> stops = input;
                std::sort(stops.begin(), stops.end(), [](const ColorStop &left, const ColorStop &right)
                          { return left.position < right.position; });
                position = clamp_value(position, 0.0, 1.0);
                if (position <= stops.front().position)
                    return normalized_color(stops.front().color, theme.accent);
                if (position >= stops.back().position)
                    return normalized_color(stops.back().color, theme.accent);

                for (size_t i = 1; i < stops.size(); i++)
                {
                    if (position <= stops[i].position)
                    {
                        const Color left = normalized_color(stops[i - 1].color, theme.accent);
                        const Color right = normalized_color(stops[i].color, theme.accent);
                        const double span = std::max(1e-12, stops[i].position - stops[i - 1].position);
                        const double amount = clamp_value((position - stops[i - 1].position) / span, 0.0, 1.0);
                        return {
                            left[0] + (right[0] - left[0]) * amount,
                            left[1] + (right[1] - left[1]) * amount,
                            left[2] + (right[2] - left[2]) * amount};
                    }
                }
                return normalized_color(stops.back().color, theme.accent);
            }

            CompactStyle build_style(size_t width, size_t height, const Theme &theme)
            {
                const double aspect = height == 0 ? 1.0 : (double)width / (double)height;
                double scale = (double)width / 1500.0;
                if (aspect < 0.8)
                    scale = (double)width / 980.0;
                else if (aspect > 1.8)
                    scale = (double)width / 1850.0;
                scale = clamp_value(scale, theme.minimum_scale * 0.85, theme.maximum_scale * 0.90);

                CompactStyle style;
                style.title = scaled(25, scale);
                style.body = scaled(16, scale);
                style.small = scaled(13, scale);
                style.padding = scaled(18, scale);
                style.gap = scaled(8, scale);
                style.accent = scaled(3, scale);
                style.legend_bar = scaled(18, scale);
                style.portrait = aspect < 0.8;
                style.landscape = aspect > 1.35;
                return style;
            }

            int line_height(TextDrawer &drawer, int font_size)
            {
                return measured(drawer, font_size, "Ag").line_height;
            }

            int compact_header_height(TextDrawer &drawer, size_t width, const PresentationSpec &spec, const CompactStyle &style)
            {
                const int available = std::max(1, (int)width - style.padding * 2);
                const std::string identity = join_nonempty({spec.pass.satellite, spec.pass.instrument}, " / ");
                const std::string quality_line = join_nonempty({spec.pass.quality, spec.pass.quality_detail}, " · ");
                const std::string time_line = join_nonempty({spec.pass.acquisition_time, spec.pass.pass_summary, quality_line}, "  ·  ");
                const std::string details = join_fields(spec.pass.details);

                const int title_rows = std::max(1, (int)wrap_text(drawer, identity, style.title, available).size());
                const int product_rows = spec.pass.product.empty() ? 0 : std::min(2, std::max(1, (int)wrap_text(drawer, spec.pass.product, style.body, available).size()));
                const int time_rows = time_line.empty() ? 0 : std::min(2, std::max(1, (int)wrap_text(drawer, time_line, style.small, available).size()));
                const int detail_rows = details.empty() ? 0 : std::min(2, std::max(1, (int)wrap_text(drawer, details, style.small, available).size()));
                const int sections = 1 + (product_rows > 0 ? 1 : 0) + (time_rows > 0 ? 1 : 0) + (detail_rows > 0 ? 1 : 0);

                return style.padding * 2 +
                       title_rows * line_height(drawer, style.title) +
                       product_rows * line_height(drawer, style.body) +
                       (time_rows + detail_rows) * line_height(drawer, style.small) +
                       style.gap * std::max(0, sections - 1) +
                       style.accent;
            }

            int compact_footer_height(TextDrawer &drawer, size_t width, const PresentationSpec &spec, const CompactStyle &style)
            {
                const int available = std::max(1, (int)width - style.padding * 2);
                int height = style.padding;
                if (!spec.legend.title.empty())
                    height += line_height(drawer, style.body) + style.gap / 2;
                if (!spec.legend.subtitle.empty() && !style.portrait)
                    height += std::min(1, (int)wrap_text(drawer, spec.legend.subtitle, style.small, available).size()) * line_height(drawer, style.small) + style.gap / 2;

                if (spec.legend.kind == LegendKind::Continuous)
                    height += style.legend_bar + style.gap + line_height(drawer, style.small);
                else if (spec.legend.kind == LegendKind::Categorical)
                {
                    const int columns = style.landscape && width >= 900 ? 2 : 1;
                    const int rows = (int)std::ceil((double)spec.legend.categories.size() / (double)columns);
                    height += rows * (line_height(drawer, style.small) + style.gap / 2);
                }
                else if (spec.legend.kind == LegendKind::Composite)
                {
                    const int description_width = std::max(1, available - scaled(58, (double)style.body / 16.0));
                    for (const CompositeComponent &component : spec.legend.components)
                    {
                        const int lines = std::max(1, (int)wrap_text(drawer, component_description(component), style.small, description_width).size());
                        height += lines * line_height(drawer, style.small) + style.gap / 2;
                    }
                }

                if (!spec.legend.notes.empty())
                    height += std::min(2, (int)wrap_text(drawer, spec.legend.notes.front(), style.small, available).size()) * line_height(drawer, style.small) + style.gap / 2;
                if (spec.show_branding && !spec.branding.empty())
                    height += line_height(drawer, style.small) + style.gap / 2;
                height += style.padding;
                return std::max(height, style.padding * 2);
            }

            void draw_wrapped(Image &output, TextDrawer &drawer, const std::string &text, int x, int &y, int width, int font_size, const Color &color, int max_lines = -1)
            {
                std::vector<std::string> lines = wrap_text(drawer, text, font_size, width);
                if (max_lines >= 0 && (int)lines.size() > max_lines)
                    lines.resize(max_lines);
                for (const std::string &line : lines)
                {
                    if (!line.empty())
                        drawer.draw_text(output, x, y, color, font_size, line);
                    y += line_height(drawer, font_size);
                }
            }

            void draw_compact_header(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const CompactStyle &style, int header_height)
            {
                const Theme &theme = spec.theme;
                fill_rect(output, 0, 0, output.width(), header_height, theme.panel);
                fill_rect(output, 0, header_height - style.accent, output.width(), header_height, theme.accent);

                const int available = std::max(1, (int)output.width() - style.padding * 2);
                int y = style.padding;
                const std::string identity = join_nonempty({spec.pass.satellite, spec.pass.instrument}, " / ");
                draw_wrapped(output, drawer, identity, style.padding, y, available, style.title, theme.text, 2);

                if (!spec.pass.product.empty())
                {
                    y += style.gap / 2;
                    draw_wrapped(output, drawer, spec.pass.product, style.padding, y, available, style.body, theme.text, 2);
                }

                const std::string quality_line = join_nonempty({spec.pass.quality, spec.pass.quality_detail}, " · ");
                const std::string time_line = join_nonempty({spec.pass.acquisition_time, spec.pass.pass_summary, quality_line}, "  ·  ");
                if (!time_line.empty())
                {
                    y += style.gap / 2;
                    draw_wrapped(output, drawer, time_line, style.padding, y, available, style.small, theme.muted_text, 2);
                }

                const std::string details = join_fields(spec.pass.details);
                if (!details.empty())
                {
                    y += style.gap / 2;
                    draw_wrapped(output, drawer, details, style.padding, y, available, style.small, theme.muted_text, 2);
                }
            }

            void draw_compact_continuous(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const CompactStyle &style, int &y)
            {
                const int x0 = style.padding;
                const int x1 = (int)output.width() - style.padding;
                const int width = std::max(1, x1 - x0);
                for (int x = 0; x < width; x++)
                {
                    const double position = width <= 1 ? 0.0 : (double)x / (double)(width - 1);
                    output.draw_line(x0 + x, y, x0 + x, y + style.legend_bar - 1, sample_stops(spec.legend.color_stops, position, spec.theme));
                }
                output.draw_line(x0, y, x1 - 1, y, spec.theme.border);
                output.draw_line(x0, y + style.legend_bar - 1, x1 - 1, y + style.legend_bar - 1, spec.theme.border);
                y += style.legend_bar + style.gap / 2;

                const int max_labels = std::max(2, width / std::max(48, style.small * 4));
                const int count = (int)spec.legend.ticks.size();
                const int stride = count <= max_labels ? 1 : std::max(1, (count - 1) / (max_labels - 1));
                for (int i = 0; i < count; i++)
                {
                    if (i != 0 && i != count - 1 && i % stride != 0)
                        continue;
                    const LegendTick &tick = spec.legend.ticks[i];
                    const int x = x0 + (int)std::round(clamp_value(tick.position, 0.0, 1.0) * (width - 1));
                    output.draw_line(x, y - style.gap / 2, x, y + std::max(2, style.gap / 3), spec.theme.muted_text);
                    const TextSize size = measured(drawer, style.small, tick.label);
                    int text_x = x - size.width / 2;
                    text_x = std::max(x0, std::min(x1 - size.width, text_x));
                    drawer.draw_text(output, text_x, y + std::max(2, style.gap / 3), spec.theme.text, style.small, tick.label);
                }
                y += line_height(drawer, style.small) + style.gap / 2;
            }

            void draw_compact_categories(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const CompactStyle &style, int &y)
            {
                const int columns = style.landscape && output.width() >= 900 ? 2 : 1;
                const int available = (int)output.width() - style.padding * 2;
                const int cell_width = std::max(1, available / columns);
                const int row_height = line_height(drawer, style.small) + style.gap / 2;
                const int marker = std::max(7, style.small - 2);

                for (size_t i = 0; i < spec.legend.categories.size(); i++)
                {
                    const int row = (int)i / columns;
                    const int column = (int)i % columns;
                    const int x = style.padding + column * cell_width;
                    const int row_y = y + row * row_height;
                    fill_rect(output, x, row_y + (row_height - marker) / 2, x + marker, row_y + (row_height - marker) / 2 + marker, normalized_color(spec.legend.categories[i].color, spec.theme.accent));
                    drawer.draw_text(output, x + marker + style.gap, row_y, spec.theme.text, style.small, spec.legend.categories[i].label);
                }
                const int rows = (int)std::ceil((double)spec.legend.categories.size() / (double)columns);
                y += rows * row_height;
            }

            void draw_compact_components(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const CompactStyle &style, int &y)
            {
                const int marker = std::max(8, style.small - 1);
                const int label_width = std::max(marker + style.gap + measured(drawer, style.body, "IN").width, scaled(52, (double)style.body / 16.0));
                const int description_x = style.padding + label_width;
                const int description_width = std::max(1, (int)output.width() - style.padding - description_x);

                for (const CompositeComponent &component : spec.legend.components)
                {
                    std::vector<std::string> lines = wrap_text(drawer, component_description(component), style.small, description_width);
                    if (lines.empty())
                        lines.push_back("Канал не указан");
                    const int row_height = std::max(line_height(drawer, style.body), (int)lines.size() * line_height(drawer, style.small));
                    const Color color = component_color(component, spec.theme);
                    fill_rect(output, style.padding, y + (line_height(drawer, style.body) - marker) / 2, style.padding + marker, y + (line_height(drawer, style.body) - marker) / 2 + marker, color);
                    drawer.draw_text(output, style.padding + marker + style.gap / 2, y, color, style.body, component.component.empty() ? "IN" : component.component);
                    for (size_t i = 0; i < lines.size(); i++)
                        drawer.draw_text(output, description_x, y + (int)i * line_height(drawer, style.small), spec.theme.text, style.small, lines[i]);
                    y += row_height + style.gap / 2;
                }
            }

            void draw_compact_footer(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const CompactStyle &style, int footer_y, int footer_height)
            {
                fill_rect(output, 0, footer_y, output.width(), footer_y + footer_height, spec.theme.panel);
                fill_rect(output, 0, footer_y, output.width(), footer_y + 1, spec.theme.border);
                int y = footer_y + style.padding;
                const int available = std::max(1, (int)output.width() - style.padding * 2);

                if (!spec.legend.title.empty())
                {
                    std::string title = spec.legend.title;
                    if (!spec.legend.unit.empty())
                        title += " [" + spec.legend.unit + "]";
                    draw_wrapped(output, drawer, title, style.padding, y, available, style.body, spec.theme.text, 2);
                    y += style.gap / 2;
                }
                if (!spec.legend.subtitle.empty() && !style.portrait)
                {
                    draw_wrapped(output, drawer, spec.legend.subtitle, style.padding, y, available, style.small, spec.theme.muted_text, 1);
                    y += style.gap / 2;
                }

                if (spec.legend.kind == LegendKind::Continuous)
                    draw_compact_continuous(output, drawer, spec, style, y);
                else if (spec.legend.kind == LegendKind::Categorical)
                    draw_compact_categories(output, drawer, spec, style, y);
                else if (spec.legend.kind == LegendKind::Composite)
                    draw_compact_components(output, drawer, spec, style, y);

                if (!spec.legend.notes.empty())
                {
                    draw_wrapped(output, drawer, spec.legend.notes.front(), style.padding, y, available, style.small, spec.theme.muted_text, 2);
                    y += style.gap / 2;
                }

                if (spec.show_branding && !spec.branding.empty())
                {
                    const TextSize size = measured(drawer, style.small, spec.branding);
                    const int x = std::max(style.padding, (int)output.width() - style.padding - size.width);
                    const int branding_y = footer_y + footer_height - style.padding - size.line_height;
                    drawer.draw_text(output, x, branding_y, spec.theme.muted_text, style.small, spec.branding);
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
                        if (transform == RasterTransform::FlipVertical || transform == RasterTransform::Rotate180)
                            source_y = source.height() - 1 - y;
                        if (transform == RasterTransform::FlipHorizontal || transform == RasterTransform::Rotate180)
                            source_x = source.width() - 1 - x;
                        output.set(channel, x, y, source.get(channel, source_x, source_y));
                    }
                }
            }
            return output;
        }

        Image render_minimal(const Image &source, TextDrawer &text_drawer, const PresentationSpec &spec)
        {
            Image rgb = make_rgb(source);
            const CompactStyle style = build_style(rgb.width(), rgb.height(), spec.theme);
            const int header_height = compact_header_height(text_drawer, rgb.width(), spec, style);
            const int footer_height = compact_footer_height(text_drawer, rgb.width(), spec, style);

            Image output(rgb.depth(), rgb.width(), header_height + rgb.height() + footer_height, 3);
            output.fill_color(spec.theme.panel);
            output.draw_image(0, rgb, 0, header_height);
            draw_compact_header(output, text_drawer, spec, style, header_height);
            draw_compact_footer(output, text_drawer, spec, style, header_height + rgb.height(), footer_height);
            return output;
        }

        Image render_layout(const Image &source, TextDrawer &text_drawer, const PresentationSpec &spec, LayoutKind layout)
        {
            if (layout == LayoutKind::Minimal)
                return render_minimal(source, text_drawer, spec);

            PresentationSpec tuned = spec;
            const FrameKind frame = classify_frame(source);
            if (frame == FrameKind::Portrait)
            {
                tuned.theme.reference_width = 1050;
                tuned.theme.minimum_scale = std::min(tuned.theme.minimum_scale, 0.52);
                tuned.theme.maximum_scale = std::min(tuned.theme.maximum_scale, 1.45);
            }
            else if (frame == FrameKind::Landscape)
            {
                tuned.theme.reference_width = 1750;
                tuned.theme.minimum_scale = std::max(tuned.theme.minimum_scale, 0.68);
            }
            return render(source, text_drawer, tuned);
        }
    }
}
