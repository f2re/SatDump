#include "presentation.h"

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
            struct TextStyle
            {
                int title = 32;
                int product = 22;
                int body = 17;
                int small = 14;
                int padding = 24;
                int gap = 10;
                int accent = 3;
                int marker = 14;
                int legend_bar = 28;
                bool portrait = false;
                bool wide = false;
            };

            double clamp_value(double value, double minimum, double maximum)
            {
                return std::max(minimum, std::min(maximum, value));
            }

            int scaled(double value, double scale)
            {
                return std::max(1, (int)std::lround(value * scale));
            }

            Color normalized_color(const Color &color, const Color &fallback)
            {
                Color output = fallback;
                for (size_t index = 0; index < std::min<size_t>(3, color.size()); index++)
                    output[index] = clamp_value(color[index], 0.0, 1.0);
                return output;
            }

            TextSize measured(TextDrawer &drawer, int size, const std::string &text)
            {
                TextSize result = drawer.measure_text(size, text);
                if (result.line_height <= 0)
                    result.line_height = std::max(1, (int)std::lround((double)size * 1.25));
                if (result.height <= 0 && !text.empty())
                    result.height = result.line_height;
                if (result.width <= 0 && !text.empty())
                    result.width = std::max(1, (int)std::lround((double)text.size() * (double)size * 0.55));
                return result;
            }

            void fill_rect(Image &image, int x0, int y0, int x1, int y1, const Color &color)
            {
                const int width = (int)image.width();
                const int height = (int)image.height();
                x0 = std::max(0, std::min(width, x0));
                x1 = std::max(0, std::min(width, x1));
                y0 = std::max(0, std::min(height, y0));
                y1 = std::max(0, std::min(height, y1));
                if (x1 <= x0 || y1 <= y0)
                    return;

                for (int y = y0; y < y1; y++)
                    image.draw_line(x0, y, x1 - 1, y, color);
            }

            Image make_rgb(const Image &source)
            {
                Image rgb = source;
                if (rgb.channels() == 1 || rgb.channels() == 4)
                    rgb.to_rgb();
                else if (rgb.channels() != 3)
                {
                    Image converted(source.depth(), source.width(), source.height(), 3);
                    for (size_t y = 0; y < source.height(); y++)
                        for (size_t x = 0; x < source.width(); x++)
                        {
                            const int value = rgb.get(0, x, y);
                            converted.set(0, x, y, value);
                            converted.set(1, x, y, value);
                            converted.set(2, x, y, value);
                        }
                    rgb = converted;
                }
                return rgb;
            }

            std::vector<std::string> wrap_text(TextDrawer &drawer,
                                                const std::string &text,
                                                int font_size,
                                                int max_width,
                                                size_t max_lines = 0)
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
                            if (max_lines > 0 && output.size() >= max_lines)
                                return output;
                        }
                        else
                            line = candidate;
                    }
                    if (!line.empty())
                        output.push_back(line);
                    else if (paragraph.empty())
                        output.push_back("");
                    if (max_lines > 0 && output.size() >= max_lines)
                    {
                        output.resize(max_lines);
                        return output;
                    }
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
                std::vector<std::string> values;
                for (const MetadataField &field : fields)
                {
                    if (field.value.empty())
                        continue;
                    values.push_back(field.label.empty() ? field.value : field.label + ": " + field.value);
                }
                return join_nonempty(values, "   |   ");
            }

            std::string component_description(const CompositeComponent &component)
            {
                if (!component.description.empty())
                    return component.description;

                std::vector<std::string> parts = {
                    component.channel,
                    component.spectral_range,
                    component.quantity};
                std::string output = join_nonempty(parts, " · ");
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

            Color sample_stops(const std::vector<ColorStop> &source_stops,
                               double position,
                               const Theme &theme)
            {
                if (source_stops.empty())
                    return theme.accent;

                std::vector<ColorStop> stops = source_stops;
                std::sort(stops.begin(), stops.end(), [](const ColorStop &left, const ColorStop &right)
                          { return left.position < right.position; });

                position = clamp_value(position, 0.0, 1.0);
                if (position <= stops.front().position)
                    return normalized_color(stops.front().color, theme.accent);
                if (position >= stops.back().position)
                    return normalized_color(stops.back().color, theme.accent);

                for (size_t index = 1; index < stops.size(); index++)
                {
                    if (position <= stops[index].position)
                    {
                        const Color left = normalized_color(stops[index - 1].color, theme.accent);
                        const Color right = normalized_color(stops[index].color, theme.accent);
                        const double span = std::max(1e-12, stops[index].position - stops[index - 1].position);
                        const double amount = clamp_value((position - stops[index - 1].position) / span, 0.0, 1.0);
                        return {
                            left[0] + (right[0] - left[0]) * amount,
                            left[1] + (right[1] - left[1]) * amount,
                            left[2] + (right[2] - left[2]) * amount};
                    }
                }
                return normalized_color(stops.back().color, theme.accent);
            }

            TextStyle build_text_style(int width, int raster_height, const Theme &theme)
            {
                const double ratio = raster_height > 0 ? (double)width / (double)raster_height : 1.0;
                TextStyle style;
                style.portrait = ratio < 0.82;
                style.wide = ratio > 1.45 && width >= 900;

                const int reference = style.portrait ? std::max(760, theme.reference_width - 420) : theme.reference_width;
                double scale = (double)width / (double)std::max(1, reference);
                scale = clamp_value(scale, theme.minimum_scale, theme.maximum_scale);

                style.title = scaled(style.portrait ? 30 : 35, scale);
                style.product = scaled(style.portrait ? 21 : 24, scale);
                style.body = scaled(style.portrait ? 15 : 17, scale);
                style.small = scaled(style.portrait ? 12 : 14, scale);
                style.padding = scaled(style.portrait ? 22 : 28, scale);
                style.gap = scaled(style.portrait ? 8 : 10, scale);
                style.accent = scaled(3, scale);
                style.marker = scaled(14, scale);
                style.legend_bar = scaled(style.portrait ? 22 : 28, scale);
                return style;
            }

            int wrapped_height(TextDrawer &drawer,
                               const std::string &text,
                               int font_size,
                               int width,
                               size_t max_lines = 0)
            {
                const std::vector<std::string> lines = wrap_text(drawer, text, font_size, width, max_lines);
                return (int)lines.size() * measured(drawer, font_size, "Ag").line_height;
            }

            int header_height(TextDrawer &drawer,
                              int width,
                              const PresentationSpec &spec,
                              const TextStyle &style)
            {
                const std::string identity = join_nonempty({spec.pass.satellite, spec.pass.instrument}, "  /  ");
                const std::string pass_line = join_nonempty({spec.pass.acquisition_time, spec.pass.pass_summary}, "   |   ");
                const std::string detail_line = join_fields(spec.pass.details);

                if (style.wide)
                {
                    const int column_gap = style.padding * 2;
                    const int left_width = std::max(1, (int)std::lround((double)(width - style.padding * 2 - column_gap) * 0.58));
                    const int right_width = std::max(1, width - style.padding * 2 - column_gap - left_width);

                    int left_height = wrapped_height(drawer, identity, style.title, left_width, 2);
                    if (!spec.pass.product.empty())
                        left_height += style.gap / 2 + wrapped_height(drawer, spec.pass.product, style.product, left_width, 3);

                    int right_height = wrapped_height(drawer, pass_line, style.body, right_width, 3);
                    if (!detail_line.empty())
                        right_height += style.gap / 2 + wrapped_height(drawer, detail_line, style.small, right_width, 4);
                    if (!spec.pass.quality.empty())
                        right_height += style.gap + measured(drawer, style.body, spec.pass.quality).line_height;

                    return style.padding * 2 + std::max(left_height, right_height) + style.accent;
                }

                const int available = std::max(1, width - style.padding * 2);
                int height = style.padding + wrapped_height(drawer, identity, style.title, available, 2);
                if (!spec.pass.product.empty())
                    height += style.gap / 2 + wrapped_height(drawer, spec.pass.product, style.product, available, 3);
                if (!pass_line.empty())
                    height += style.gap / 2 + wrapped_height(drawer, pass_line, style.body, available, 4);
                if (!detail_line.empty())
                    height += style.gap / 2 + wrapped_height(drawer, detail_line, style.small, available, 5);
                if (!spec.pass.quality.empty())
                    height += style.gap + measured(drawer, style.body, spec.pass.quality).line_height;
                return height + style.padding + style.accent;
            }

            int legend_content_height(TextDrawer &drawer,
                                      int width,
                                      const LegendSpec &legend,
                                      const TextStyle &style)
            {
                const int available = std::max(1, width - style.padding * 2);
                const int body_line = measured(drawer, style.body, "Ag").line_height;
                const int small_line = measured(drawer, style.small, "Ag").line_height;
                int height = 0;

                if (legend.kind == LegendKind::Continuous)
                    height += style.legend_bar + style.gap + body_line;
                else if (legend.kind == LegendKind::Categorical)
                {
                    const int columns = style.portrait ? 1 : (width >= 1500 ? 3 : 2);
                    const int rows = (int)std::ceil((double)legend.categories.size() / (double)std::max(1, columns));
                    height += rows * (body_line + style.gap);
                }
                else if (legend.kind == LegendKind::Composite)
                {
                    const int description_width = std::max(1, available - style.marker - style.gap * 5);
                    for (const CompositeComponent &component : legend.components)
                    {
                        const int lines = std::max(1, (int)wrap_text(drawer,
                                                                    component_description(component),
                                                                    style.small,
                                                                    description_width,
                                                                    style.portrait ? 5 : 3)
                                                       .size());
                        height += std::max(body_line, lines * small_line) + style.gap;
                    }
                }
                return height;
            }

            int footer_height(TextDrawer &drawer,
                              int width,
                              const PresentationSpec &spec,
                              const TextStyle &style)
            {
                const int available = std::max(1, width - style.padding * 2);
                int height = style.padding;
                if (!spec.legend.title.empty())
                    height += measured(drawer, style.product, "Ag").line_height + style.gap / 2;
                if (!spec.legend.subtitle.empty())
                    height += wrapped_height(drawer, spec.legend.subtitle, style.small, available, style.portrait ? 4 : 3) + style.gap / 2;

                height += legend_content_height(drawer, width, spec.legend, style);
                for (const std::string &note : spec.legend.notes)
                    height += wrapped_height(drawer, note, style.small, available, style.portrait ? 4 : 3) + style.gap / 2;
                if (spec.show_branding && !spec.branding.empty())
                    height += measured(drawer, style.small, "Ag").line_height + style.gap / 2;
                return std::max(style.padding * 2, height + style.padding);
            }

            int draw_wrapped(TextDrawer &drawer,
                             Image &output,
                             int x,
                             int y,
                             int width,
                             const std::string &text,
                             int font_size,
                             const Color &color,
                             size_t max_lines = 0)
            {
                const int line_height = measured(drawer, font_size, "Ag").line_height;
                const std::vector<std::string> lines = wrap_text(drawer, text, font_size, width, max_lines);
                for (size_t index = 0; index < lines.size(); index++)
                    drawer.draw_text(output, x, y + (int)index * line_height, color, font_size, lines[index]);
                return (int)lines.size() * line_height;
            }

            void draw_header(Image &output,
                             TextDrawer &drawer,
                             const PresentationSpec &spec,
                             const TextStyle &style,
                             int header_size)
            {
                const Theme &theme = spec.theme;
                const int width = (int)output.width();
                fill_rect(output, 0, 0, width, header_size, theme.panel);
                fill_rect(output, 0, header_size - style.accent, width, header_size, theme.accent);

                const std::string identity = join_nonempty({spec.pass.satellite, spec.pass.instrument}, "  /  ");
                const std::string pass_line = join_nonempty({spec.pass.acquisition_time, spec.pass.pass_summary}, "   |   ");
                const std::string detail_line = join_fields(spec.pass.details);

                if (style.wide)
                {
                    const int column_gap = style.padding * 2;
                    const int left_width = std::max(1, (int)std::lround((double)(width - style.padding * 2 - column_gap) * 0.58));
                    const int right_x = style.padding + left_width + column_gap;
                    const int right_width = std::max(1, width - style.padding - right_x);

                    int left_y = style.padding;
                    left_y += draw_wrapped(drawer, output, style.padding, left_y, left_width, identity, style.title, theme.text, 2);
                    if (!spec.pass.product.empty())
                    {
                        left_y += style.gap / 2;
                        draw_wrapped(drawer, output, style.padding, left_y, left_width, spec.pass.product, style.product, theme.text, 3);
                    }

                    int right_y = style.padding;
                    right_y += draw_wrapped(drawer, output, right_x, right_y, right_width, pass_line, style.body, theme.muted_text, 3);
                    if (!detail_line.empty())
                    {
                        right_y += style.gap / 2;
                        right_y += draw_wrapped(drawer, output, right_x, right_y, right_width, detail_line, style.small, theme.muted_text, 4);
                    }
                    if (!spec.pass.quality.empty())
                    {
                        right_y += style.gap;
                        const std::string quality = join_nonempty({spec.pass.quality, spec.pass.quality_detail}, " · ");
                        const int badge_height = measured(drawer, style.body, "Ag").line_height + style.gap;
                        fill_rect(output, right_x, right_y, width - style.padding, right_y + badge_height, theme.panel_secondary);
                        fill_rect(output, right_x, right_y, right_x + style.accent, right_y + badge_height, theme.accent);
                        drawer.draw_text(output, right_x + style.gap, right_y + style.gap / 2, theme.text, style.body, quality);
                    }
                    return;
                }

                const int available = std::max(1, width - style.padding * 2);
                int y = style.padding;
                y += draw_wrapped(drawer, output, style.padding, y, available, identity, style.title, theme.text, 2);
                if (!spec.pass.product.empty())
                {
                    y += style.gap / 2;
                    y += draw_wrapped(drawer, output, style.padding, y, available, spec.pass.product, style.product, theme.text, 3);
                }
                if (!pass_line.empty())
                {
                    y += style.gap / 2;
                    y += draw_wrapped(drawer, output, style.padding, y, available, pass_line, style.body, theme.muted_text, 4);
                }
                if (!detail_line.empty())
                {
                    y += style.gap / 2;
                    y += draw_wrapped(drawer, output, style.padding, y, available, detail_line, style.small, theme.muted_text, 5);
                }
                if (!spec.pass.quality.empty())
                {
                    y += style.gap;
                    const std::string quality = join_nonempty({spec.pass.quality, spec.pass.quality_detail}, " · ");
                    fill_rect(output, style.padding, y, width - style.padding, y + measured(drawer, style.body, "Ag").line_height + style.gap, theme.panel_secondary);
                    drawer.draw_text(output, style.padding + style.gap, y + style.gap / 2, theme.text, style.body, quality);
                }
            }

            void draw_continuous_legend(Image &output,
                                        TextDrawer &drawer,
                                        const PresentationSpec &spec,
                                        const TextStyle &style,
                                        int &y)
            {
                const int x0 = style.padding;
                const int x1 = (int)output.width() - style.padding;
                const int bar_width = std::max(1, x1 - x0);
                for (int x = 0; x < bar_width; x++)
                {
                    const double position = bar_width <= 1 ? 0.0 : (double)x / (double)(bar_width - 1);
                    output.draw_line(x0 + x,
                                     y,
                                     x0 + x,
                                     y + style.legend_bar - 1,
                                     sample_stops(spec.legend.color_stops, position, spec.theme));
                }
                output.draw_line(x0, y, x1 - 1, y, spec.theme.border);
                output.draw_line(x0, y + style.legend_bar - 1, x1 - 1, y + style.legend_bar - 1, spec.theme.border);
                y += style.legend_bar + style.gap / 2;

                const int tick_height = std::max(3, style.gap / 2);
                const int label_y = y + tick_height;
                const size_t tick_count = spec.legend.ticks.size();
                const size_t max_labels = (size_t)std::max(2, bar_width / std::max(60, style.body * 5));
                const size_t stride = tick_count > max_labels ? (size_t)std::ceil((double)(tick_count - 1) / (double)(max_labels - 1)) : 1;

                for (size_t index = 0; index < tick_count; index++)
                {
                    if (index != 0 && index + 1 != tick_count && index % stride != 0)
                        continue;
                    const LegendTick &tick = spec.legend.ticks[index];
                    const int x = x0 + (int)std::lround(clamp_value(tick.position, 0.0, 1.0) * (double)(bar_width - 1));
                    output.draw_line(x, y - style.gap / 2, x, y + tick_height, spec.theme.muted_text);
                    if (!tick.label.empty())
                    {
                        const TextSize text_size = measured(drawer, style.body, tick.label);
                        int label_x = x - text_size.width / 2;
                        label_x = std::max(x0, std::min(x1 - text_size.width, label_x));
                        drawer.draw_text(output, label_x, label_y, spec.theme.text, style.body, tick.label);
                    }
                }
                y = label_y + measured(drawer, style.body, "Ag").line_height;
            }

            void draw_categorical_legend(Image &output,
                                         TextDrawer &drawer,
                                         const PresentationSpec &spec,
                                         const TextStyle &style,
                                         int &y)
            {
                const int width = (int)output.width();
                const int columns = style.portrait ? 1 : (width >= 1500 ? 3 : 2);
                const int available = std::max(1, width - style.padding * 2);
                const int cell_width = std::max(1, available / std::max(1, columns));
                const int line_height = measured(drawer, style.body, "Ag").line_height;

                for (size_t index = 0; index < spec.legend.categories.size(); index++)
                {
                    const int row = (int)index / columns;
                    const int column = (int)index % columns;
                    const int x = style.padding + column * cell_width;
                    const int row_y = y + row * (line_height + style.gap);
                    fill_rect(output,
                              x,
                              row_y + (line_height - style.marker) / 2,
                              x + style.marker,
                              row_y + (line_height - style.marker) / 2 + style.marker,
                              normalized_color(spec.legend.categories[index].color, spec.theme.accent));
                    drawer.draw_text(output,
                                     x + style.marker + style.gap,
                                     row_y,
                                     spec.theme.text,
                                     style.body,
                                     spec.legend.categories[index].label);
                }
                const int rows = (int)std::ceil((double)spec.legend.categories.size() / (double)std::max(1, columns));
                y += rows * (line_height + style.gap);
            }

            void draw_composite_legend(Image &output,
                                       TextDrawer &drawer,
                                       const PresentationSpec &spec,
                                       const TextStyle &style,
                                       int &y)
            {
                const int width = (int)output.width();
                const int component_column = style.marker + style.gap * 4;
                const int description_x = style.padding + component_column;
                const int description_width = std::max(1, width - style.padding - description_x);
                const int body_line = measured(drawer, style.body, "Ag").line_height;
                const int small_line = measured(drawer, style.small, "Ag").line_height;

                for (const CompositeComponent &component : spec.legend.components)
                {
                    std::vector<std::string> lines = wrap_text(drawer,
                                                               component_description(component),
                                                               style.small,
                                                               description_width,
                                                               style.portrait ? 5 : 3);
                    if (lines.empty())
                        lines.push_back("Канал не указан");
                    const int row_height = std::max(body_line, (int)lines.size() * small_line);
                    const Color marker_color = component_color(component, spec.theme);
                    fill_rect(output,
                              style.padding,
                              y + (body_line - style.marker) / 2,
                              style.padding + style.marker,
                              y + (body_line - style.marker) / 2 + style.marker,
                              marker_color);
                    drawer.draw_text(output,
                                     style.padding + style.marker + style.gap / 2,
                                     y,
                                     marker_color,
                                     style.body,
                                     component.component.empty() ? "IN" : component.component);
                    for (size_t index = 0; index < lines.size(); index++)
                        drawer.draw_text(output,
                                         description_x,
                                         y + (int)index * small_line,
                                         spec.theme.text,
                                         style.small,
                                         lines[index]);
                    y += row_height + style.gap;
                }
            }

            void draw_footer(Image &output,
                             TextDrawer &drawer,
                             const PresentationSpec &spec,
                             const TextStyle &style,
                             int footer_y,
                             int footer_size)
            {
                const int width = (int)output.width();
                const int available = std::max(1, width - style.padding * 2);
                fill_rect(output, 0, footer_y, width, footer_y + footer_size, spec.theme.panel);
                fill_rect(output, 0, footer_y, width, footer_y + 1, spec.theme.border);

                int y = footer_y + style.padding;
                if (!spec.legend.title.empty())
                {
                    std::string title = spec.legend.title;
                    if (!spec.legend.unit.empty())
                        title += "  [" + spec.legend.unit + "]";
                    drawer.draw_text(output, style.padding, y, spec.theme.text, style.product, title);
                    y += measured(drawer, style.product, title).line_height + style.gap / 2;
                }
                if (!spec.legend.subtitle.empty())
                {
                    y += draw_wrapped(drawer,
                                      output,
                                      style.padding,
                                      y,
                                      available,
                                      spec.legend.subtitle,
                                      style.small,
                                      spec.theme.muted_text,
                                      style.portrait ? 4 : 3);
                    y += style.gap / 2;
                }

                if (spec.legend.kind == LegendKind::Continuous)
                    draw_continuous_legend(output, drawer, spec, style, y);
                else if (spec.legend.kind == LegendKind::Categorical)
                    draw_categorical_legend(output, drawer, spec, style, y);
                else if (spec.legend.kind == LegendKind::Composite)
                    draw_composite_legend(output, drawer, spec, style, y);

                for (const std::string &note : spec.legend.notes)
                {
                    y += draw_wrapped(drawer,
                                      output,
                                      style.padding,
                                      y,
                                      available,
                                      note,
                                      style.small,
                                      spec.theme.muted_text,
                                      style.portrait ? 4 : 3);
                    y += style.gap / 2;
                }

                if (spec.show_branding && !spec.branding.empty())
                {
                    const TextSize size = measured(drawer, style.small, spec.branding);
                    const int branding_y = footer_y + footer_size - style.padding - size.line_height;
                    const int branding_x = std::max(style.padding, width - style.padding - size.width);
                    drawer.draw_text(output, branding_x, branding_y, spec.theme.muted_text, style.small, spec.branding);
                }
            }
        }

        Image render(const Image &source,
                     TextDrawer &text_drawer,
                     const PresentationSpec &spec)
        {
            Image rgb = make_rgb(source);
            const int width = (int)rgb.width();
            const int raster_height = (int)rgb.height();
            const TextStyle style = build_text_style(width, raster_height, spec.theme);
            const int header_size = header_height(text_drawer, width, spec, style);
            const int footer_size = footer_height(text_drawer, width, spec, style);

            Image output(rgb.depth(), rgb.width(), (size_t)(header_size + raster_height + footer_size), 3);
            output.fill_color(spec.theme.panel);
            output.draw_image(0, rgb, 0, header_size);
            draw_header(output, text_drawer, spec, style, header_size);
            draw_footer(output, text_drawer, spec, style, header_size + raster_height, footer_size);
            return output;
        }
    }
}
