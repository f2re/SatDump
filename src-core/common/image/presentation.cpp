#include "presentation.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace image
{
    namespace presentation
    {
        namespace
        {
            struct TextStyle
            {
                int title = 42;
                int product = 30;
                int body = 23;
                int small = 19;
                int padding = 34;
                int gap = 14;
                int accent = 3;
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
                            int value = source.get(0, x, y);
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
                if (text.empty())
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
                        std::string candidate = line.empty() ? word : line + " " + word;
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

            std::string join_fields(const std::vector<MetadataField> &fields)
            {
                std::string output;
                for (const MetadataField &field : fields)
                {
                    if (field.value.empty())
                        continue;

                    if (!output.empty())
                        output += "   |   ";

                    if (!field.label.empty())
                        output += field.label + ": ";
                    output += field.value;
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

            std::string component_description(const CompositeComponent &component)
            {
                if (!component.description.empty())
                    return component.description;

                std::vector<std::string> parts;
                parts.push_back(component.channel);
                parts.push_back(component.spectral_range);
                parts.push_back(component.quantity);
                std::string output = join_nonempty(parts, "  ·  ");

                if (!component.formula.empty())
                {
                    if (!output.empty())
                        output += "  |  ";
                    output += component.formula;
                }
                return output;
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

            Color sample_stops(const std::vector<ColorStop> &source_stops, double position, const Theme &theme)
            {
                if (source_stops.empty())
                    return theme.accent;

                std::vector<ColorStop> stops = source_stops;
                std::sort(stops.begin(), stops.end(), [](const ColorStop &a, const ColorStop &b)
                          { return a.position < b.position; });

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

            TextStyle build_text_style(size_t width, const Theme &theme)
            {
                double scale = (double)width / (double)std::max(1, theme.reference_width);
                scale = clamp_value(scale, theme.minimum_scale, theme.maximum_scale);

                TextStyle style;
                style.title = scaled(42, scale);
                style.product = scaled(30, scale);
                style.body = scaled(23, scale);
                style.small = scaled(19, scale);
                style.padding = scaled(34, scale);
                style.gap = scaled(14, scale);
                style.accent = scaled(3, scale);
                return style;
            }

            int notes_height(TextDrawer &drawer, const std::vector<std::string> &notes, int font_size, int width, int gap)
            {
                int height = 0;
                const int line_height = measured(drawer, font_size, "Ag").line_height;
                for (const std::string &note : notes)
                {
                    std::vector<std::string> lines = wrap_text(drawer, note, font_size, width);
                    height += (int)lines.size() * line_height;
                    if (!lines.empty())
                        height += gap / 2;
                }
                return height;
            }

            int footer_height(TextDrawer &drawer, size_t width, const PresentationSpec &spec, const TextStyle &style)
            {
                const LegendSpec &legend = spec.legend;
                const int available = std::max(1, (int)width - style.padding * 2);
                const int title_line = measured(drawer, style.product, "Ag").line_height;
                const int body_line = measured(drawer, style.body, "Ag").line_height;
                const int small_line = measured(drawer, style.small, "Ag").line_height;

                int height = style.padding;
                if (!legend.title.empty())
                    height += title_line + style.gap / 2;
                if (!legend.subtitle.empty())
                {
                    height += (int)wrap_text(drawer, legend.subtitle, style.small, available).size() * small_line;
                    height += style.gap / 2;
                }

                if (legend.kind == LegendKind::Continuous)
                {
                    height += scaled(28, (double)style.body / 17.0);
                    height += style.gap + body_line;
                }
                else if (legend.kind == LegendKind::Categorical)
                {
                    const int columns = width >= 1100 ? 2 : 1;
                    const int rows = (int)std::ceil((double)legend.categories.size() / (double)columns);
                    height += rows * (body_line + style.gap);
                }
                else if (legend.kind == LegendKind::Composite)
                {
                    const int description_width = std::max(1, available - scaled(78, (double)style.body / 17.0));
                    for (const CompositeComponent &component : legend.components)
                    {
                        const std::vector<std::string> lines = wrap_text(drawer, component_description(component), style.small, description_width);
                        height += std::max(body_line, (int)lines.size() * small_line) + style.gap;
                    }
                }

                height += notes_height(drawer, legend.notes, style.small, available, style.gap);

                if (spec.show_branding && !spec.branding.empty())
                    height += small_line + style.gap / 2;

                height += style.padding;
                return std::max(height, style.padding * 2);
            }

            void draw_header(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const TextStyle &style, int header_height)
            {
                const Theme &theme = spec.theme;
                fill_rect(output, 0, 0, output.width(), header_height, theme.panel);
                fill_rect(output, 0, header_height - style.accent, output.width(), header_height, theme.accent);

                const int width = output.width();
                int y = style.padding;

                int badge_width = 0;
                int badge_height = 0;
                if (!spec.pass.quality.empty())
                {
                    TextSize quality_size = measured(drawer, style.body, spec.pass.quality);
                    TextSize detail_size = measured(drawer, style.small, spec.pass.quality_detail);
                    badge_width = std::max(quality_size.width, detail_size.width) + style.padding;
                    badge_height = quality_size.line_height + style.gap;
                    if (!spec.pass.quality_detail.empty())
                        badge_height += detail_size.line_height;
                    badge_height += style.gap;
                }

                std::string identity = join_nonempty({spec.pass.satellite, spec.pass.instrument}, "  /  ");
                const int title_available = std::max(1, width - style.padding * 2 - (badge_width > 0 ? badge_width + style.gap * 2 : 0));
                std::vector<std::string> identity_lines = wrap_text(drawer, identity, style.title, title_available);
                if (identity_lines.empty())
                    identity_lines.push_back("");
                if (identity_lines.size() > 2)
                    identity_lines.resize(2);

                for (const std::string &line : identity_lines)
                {
                    if (drawer.font_ready() && !line.empty())
                        drawer.draw_text(output, style.padding, y, theme.text, style.title, line);
                    y += measured(drawer, style.title, line.empty() ? "Ag" : line).line_height;
                }

                if (badge_width > 0)
                {
                    int badge_x = width - style.padding - badge_width;
                    fill_rect(output, badge_x, style.padding, badge_x + badge_width, style.padding + badge_height, theme.panel_secondary);
                    fill_rect(output, badge_x, style.padding, badge_x + style.accent, style.padding + badge_height, theme.accent);
                    if (drawer.font_ready())
                    {
                        drawer.draw_text(output, badge_x + style.gap, style.padding + style.gap / 2, theme.text, style.body, spec.pass.quality);
                        if (!spec.pass.quality_detail.empty())
                            drawer.draw_text(output, badge_x + style.gap, style.padding + style.gap / 2 + measured(drawer, style.body, spec.pass.quality).line_height, theme.muted_text, style.small, spec.pass.quality_detail);
                    }
                }

                if (!spec.pass.product.empty())
                {
                    y += style.gap / 2;
                    std::vector<std::string> product_lines = wrap_text(drawer, spec.pass.product, style.product, width - style.padding * 2);
                    for (const std::string &line : product_lines)
                    {
                        if (drawer.font_ready())
                            drawer.draw_text(output, style.padding, y, theme.text, style.product, line);
                        y += measured(drawer, style.product, line).line_height;
                    }
                }

                std::string pass_line = join_nonempty({spec.pass.acquisition_time, spec.pass.pass_summary}, "   |   ");
                if (!pass_line.empty())
                {
                    y += style.gap / 2;
                    for (const std::string &line : wrap_text(drawer, pass_line, style.body, width - style.padding * 2))
                    {
                        if (drawer.font_ready())
                            drawer.draw_text(output, style.padding, y, theme.muted_text, style.body, line);
                        y += measured(drawer, style.body, line).line_height;
                    }
                }

                std::string detail_line = join_fields(spec.pass.details);
                if (!detail_line.empty())
                {
                    y += style.gap / 2;
                    for (const std::string &line : wrap_text(drawer, detail_line, style.small, width - style.padding * 2))
                    {
                        if (drawer.font_ready())
                            drawer.draw_text(output, style.padding, y, theme.muted_text, style.small, line);
                        y += measured(drawer, style.small, line).line_height;
                    }
                }
            }

            void draw_continuous_legend(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const TextStyle &style, int &y)
            {
                const Theme &theme = spec.theme;
                const LegendSpec &legend = spec.legend;
                const int x0 = style.padding;
                const int x1 = output.width() - style.padding;
                const int bar_height = scaled(28, (double)style.body / 17.0);
                const int bar_width = std::max(1, x1 - x0);

                for (int x = 0; x < bar_width; x++)
                {
                    double position = bar_width <= 1 ? 0.0 : (double)x / (double)(bar_width - 1);
                    Color color = sample_stops(legend.color_stops, position, theme);
                    output.draw_line(x0 + x, y, x0 + x, y + bar_height - 1, color);
                }

                output.draw_line(x0, y, x1 - 1, y, theme.border);
                output.draw_line(x0, y + bar_height - 1, x1 - 1, y + bar_height - 1, theme.border);
                y += bar_height + style.gap / 2;

                const int tick_height = std::max(3, style.gap / 2);
                const int label_y = y + tick_height;
                for (const LegendTick &tick : legend.ticks)
                {
                    const double position = clamp_value(tick.position, 0.0, 1.0);
                    const int x = x0 + (int)std::round(position * (bar_width - 1));
                    output.draw_line(x, y - style.gap / 2, x, y + tick_height, theme.muted_text);

                    if (drawer.font_ready() && !tick.label.empty())
                    {
                        TextSize size = measured(drawer, style.body, tick.label);
                        int label_x = x - size.width / 2;
                        label_x = std::max(x0, std::min(x1 - size.width, label_x));
                        drawer.draw_text(output, label_x, label_y, theme.text, style.body, tick.label);
                    }
                }
                y = label_y + measured(drawer, style.body, "Ag").line_height;
            }

            void draw_categorical_legend(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const TextStyle &style, int &y)
            {
                const Theme &theme = spec.theme;
                const LegendSpec &legend = spec.legend;
                const int columns = output.width() >= 1100 ? 2 : 1;
                const int available = output.width() - style.padding * 2;
                const int cell_width = available / columns;
                const int marker = std::max(8, scaled(13, (double)style.body / 17.0));
                const int line_height = measured(drawer, style.body, "Ag").line_height;

                for (size_t i = 0; i < legend.categories.size(); i++)
                {
                    const int row = (int)i / columns;
                    const int column = (int)i % columns;
                    const int x = style.padding + column * cell_width;
                    const int row_y = y + row * (line_height + style.gap);
                    fill_rect(output, x, row_y + (line_height - marker) / 2, x + marker, row_y + (line_height - marker) / 2 + marker, normalized_color(legend.categories[i].color, theme.accent));
                    if (drawer.font_ready())
                        drawer.draw_text(output, x + marker + style.gap, row_y, theme.text, style.body, legend.categories[i].label);
                }

                const int rows = (int)std::ceil((double)legend.categories.size() / (double)columns);
                y += rows * (line_height + style.gap);
            }

            void draw_composite_legend(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const TextStyle &style, int &y)
            {
                const Theme &theme = spec.theme;
                const LegendSpec &legend = spec.legend;
                const int marker = std::max(10, scaled(14, (double)style.body / 17.0));
                const int component_width = scaled(56, (double)style.body / 17.0);
                const int description_x = style.padding + component_width + style.gap;
                const int description_width = std::max(1, (int)output.width() - style.padding - description_x);
                const int body_line = measured(drawer, style.body, "Ag").line_height;
                const int small_line = measured(drawer, style.small, "Ag").line_height;

                for (const CompositeComponent &component : legend.components)
                {
                    std::vector<std::string> lines = wrap_text(drawer, component_description(component), style.small, description_width);
                    if (lines.empty())
                        lines.push_back("Канал не указан");
                    const int row_height = std::max(body_line, (int)lines.size() * small_line);

                    Color marker_color = component_color(component, theme);
                    fill_rect(output, style.padding, y + (body_line - marker) / 2, style.padding + marker, y + (body_line - marker) / 2 + marker, marker_color);

                    if (drawer.font_ready())
                    {
                        const std::string component_label = component.component.empty() ? "IN" : component.component;
                        drawer.draw_text(output, style.padding + marker + style.gap / 2, y, marker_color, style.body, component_label);
                        for (size_t i = 0; i < lines.size(); i++)
                            drawer.draw_text(output, description_x, y + (int)i * small_line, theme.text, style.small, lines[i]);
                    }

                    y += row_height + style.gap;
                }
            }

            void draw_footer(Image &output, TextDrawer &drawer, const PresentationSpec &spec, const TextStyle &style, int footer_y, int footer_height_value)
            {
                const Theme &theme = spec.theme;
                const LegendSpec &legend = spec.legend;
                fill_rect(output, 0, footer_y, output.width(), footer_y + footer_height_value, theme.panel);
                fill_rect(output, 0, footer_y, output.width(), footer_y + 1, theme.border);

                int y = footer_y + style.padding;
                if (!legend.title.empty())
                {
                    std::string title = legend.title;
                    if (!legend.unit.empty())
                        title += "  [" + legend.unit + "]";
                    if (drawer.font_ready())
                        drawer.draw_text(output, style.padding, y, theme.text, style.product, title);
                    y += measured(drawer, style.product, title).line_height + style.gap / 2;
                }

                if (!legend.subtitle.empty())
                {
                    for (const std::string &line : wrap_text(drawer, legend.subtitle, style.small, output.width() - style.padding * 2))
                    {
                        if (drawer.font_ready())
                            drawer.draw_text(output, style.padding, y, theme.muted_text, style.small, line);
                        y += measured(drawer, style.small, line).line_height;
                    }
                    y += style.gap / 2;
                }

                if (legend.kind == LegendKind::Continuous)
                    draw_continuous_legend(output, drawer, spec, style, y);
                else if (legend.kind == LegendKind::Categorical)
                    draw_categorical_legend(output, drawer, spec, style, y);
                else if (legend.kind == LegendKind::Composite)
                    draw_composite_legend(output, drawer, spec, style, y);

                const int available = output.width() - style.padding * 2;
                for (const std::string &note : legend.notes)
                {
                    for (const std::string &line : wrap_text(drawer, note, style.small, available))
                    {
                        if (drawer.font_ready())
                            drawer.draw_text(output, style.padding, y, theme.muted_text, style.small, line);
                        y += measured(drawer, style.small, line).line_height;
                    }
                    y += style.gap / 2;
                }

                if (spec.show_branding && !spec.branding.empty() && drawer.font_ready())
                {
                    TextSize size = measured(drawer, style.small, spec.branding);
                    int branding_y = footer_y + footer_height_value - style.padding - size.line_height;
                    drawer.draw_text(output, output.width() - style.padding - size.width, branding_y, theme.muted_text, style.small, spec.branding);
                }
            }
        }

        Image render(const Image &source, TextDrawer &text_drawer, const PresentationSpec &spec)
        {
            Image rgb = make_rgb(source);
            const TextStyle style = build_text_style(rgb.width(), spec.theme);

            const int width = rgb.width();
            const int title_line = measured(text_drawer, style.title, "Ag").line_height;
            const int product_line = measured(text_drawer, style.product, "Ag").line_height;
            const int body_line = measured(text_drawer, style.body, "Ag").line_height;
            const int small_line = measured(text_drawer, style.small, "Ag").line_height;

            std::string identity = join_nonempty({spec.pass.satellite, spec.pass.instrument}, "  /  ");
            int quality_reserve = spec.pass.quality.empty() ? 0 : measured(text_drawer, style.body, spec.pass.quality).width + style.padding + style.gap * 2;
            int identity_width = std::max(1, width - style.padding * 2 - quality_reserve);
            int identity_lines = std::max(1, (int)wrap_text(text_drawer, identity, style.title, identity_width).size());
            identity_lines = std::min(2, identity_lines);

            int header_height = style.padding + identity_lines * title_line;
            if (!spec.pass.product.empty())
                header_height += style.gap / 2 + std::max(1, (int)wrap_text(text_drawer, spec.pass.product, style.product, width - style.padding * 2).size()) * product_line;

            std::string pass_line = join_nonempty({spec.pass.acquisition_time, spec.pass.pass_summary}, "   |   ");
            if (!pass_line.empty())
                header_height += style.gap / 2 + std::max(1, (int)wrap_text(text_drawer, pass_line, style.body, width - style.padding * 2).size()) * body_line;

            std::string detail_line = join_fields(spec.pass.details);
            if (!detail_line.empty())
                header_height += style.gap / 2 + std::max(1, (int)wrap_text(text_drawer, detail_line, style.small, width - style.padding * 2).size()) * small_line;

            header_height += style.padding + style.accent;
            const int footer_height_value = footer_height(text_drawer, width, spec, style);

            Image output(rgb.depth(), width, header_height + rgb.height() + footer_height_value, 3);
            output.fill_color(spec.theme.panel);
            output.draw_image(0, rgb, 0, header_height);

            draw_header(output, text_drawer, spec, style, header_height);
            draw_footer(output, text_drawer, spec, style, header_height + rgb.height(), footer_height_value);
            return output;
        }
    }
}
