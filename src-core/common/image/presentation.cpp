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
                int legend_bar = 28;
                bool portrait = false;
                bool wide = false;
            };

            struct HeaderPlan
            {
                std::vector<std::string> identity_lines;
                std::vector<std::string> product_lines;
                std::vector<std::string> pass_lines;
                std::vector<std::string> detail_lines;
                std::vector<std::string> quality_detail_lines;
                std::string quality;
                bool badge = false;
                int badge_width = 0;
                int badge_height = 0;
                int left_width = 1;
                int height = 0;
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
                for (size_t index = 0; index < std::min<size_t>(3, color.size()); index++)
                    output[index] = clamp_value(color[index], 0.0, 1.0);
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

            int line_height(TextDrawer &drawer, int size)
            {
                return measured(drawer, size, "Ag").line_height;
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

            std::vector<std::string> wrap_text(TextDrawer &drawer,
                                               const std::string &text,
                                               int font_size,
                                               int max_width,
                                               int max_lines = -1)
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
                            if (max_lines > 0 && (int)output.size() >= max_lines)
                                break;
                        }
                        else
                        {
                            line = candidate;
                        }
                    }

                    if (max_lines > 0 && (int)output.size() >= max_lines)
                        break;
                    if (!line.empty())
                        output.push_back(line);
                    else if (paragraph.empty())
                        output.push_back("");
                    if (max_lines > 0 && (int)output.size() >= max_lines)
                        break;
                }

                if (max_lines > 0 && (int)output.size() > max_lines)
                    output.resize(max_lines);
                return output;
            }

            int draw_lines(Image &output,
                           TextDrawer &drawer,
                           const std::vector<std::string> &lines,
                           int x,
                           int y,
                           int font_size,
                           const Color &color)
            {
                const int advance = line_height(drawer, font_size);
                for (const std::string &line : lines)
                {
                    if (!line.empty())
                        drawer.draw_text(output, x, y, color, font_size, line);
                    y += advance;
                }
                return y;
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

                std::string output = join_nonempty(
                    {component.channel, component.spectral_range, component.quantity},
                    "  ·  ");
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

            TextStyle build_text_style(size_t width, size_t height, const Theme &theme)
            {
                const double aspect = height == 0 ? 1.0 : (double)width / (double)height;
                double scale = (double)width / (double)std::max(1, theme.reference_width);
                if (aspect < 0.82)
                    scale *= 0.90;
                else if (aspect > 1.75)
                    scale *= 0.94;
                scale = clamp_value(scale, theme.minimum_scale, theme.maximum_scale);

                TextStyle style;
                style.title = scaled(34, scale);
                style.product = scaled(23, scale);
                style.body = scaled(17, scale);
                style.small = scaled(14, scale);
                style.padding = scaled(28, scale);
                style.gap = scaled(10, scale);
                style.accent = scaled(3, scale);
                style.legend_bar = scaled(28, scale);
                style.portrait = aspect < 0.82;
                style.wide = aspect > 1.20 && width >= 980;
                return style;
            }

            HeaderPlan build_header_plan(TextDrawer &drawer,
                                         int width,
                                         const PresentationSpec &spec,
                                         const TextStyle &style)
            {
                HeaderPlan plan;
                const int available = std::max(1, width - style.padding * 2);
                plan.quality = spec.pass.quality;

                plan.badge = !spec.pass.quality.empty() && style.wide;
                if (plan.badge)
                {
                    const int minimum_badge = scaled(190, (double)style.body / 17.0);
                    const int desired_badge = std::max(
                        measured(drawer, style.body, spec.pass.quality).width + style.gap * 2,
                        measured(drawer, style.small, "КАЧЕСТВО").width + style.gap * 2);
                    plan.badge_width = std::min(std::max(minimum_badge, desired_badge), std::max(minimum_badge, available / 3));
                    plan.left_width = std::max(1, available - plan.badge_width - style.gap * 2);
                    plan.quality_detail_lines = wrap_text(
                        drawer,
                        spec.pass.quality_detail,
                        style.small,
                        std::max(1, plan.badge_width - style.gap * 2),
                        3);
                    plan.badge_height =
                        style.gap +
                        line_height(drawer, style.small) +
                        style.gap / 2 +
                        line_height(drawer, style.body) +
                        (int)plan.quality_detail_lines.size() * line_height(drawer, style.small) +
                        style.gap;
                }
                else
                {
                    plan.left_width = available;
                }

                const std::string identity = join_nonempty(
                    {spec.pass.satellite, spec.pass.instrument},
                    "  /  ");
                plan.identity_lines = wrap_text(drawer, identity, style.title, plan.left_width, 2);
                if (plan.identity_lines.empty())
                    plan.identity_lines.push_back("");

                plan.product_lines = wrap_text(
                    drawer,
                    spec.pass.product,
                    style.product,
                    plan.left_width,
                    style.portrait ? 3 : 2);

                std::vector<std::string> pass_parts = {
                    spec.pass.acquisition_time,
                    spec.pass.pass_summary};
                if (!plan.badge)
                {
                    pass_parts.push_back(spec.pass.quality);
                    pass_parts.push_back(spec.pass.quality_detail);
                }
                plan.pass_lines = wrap_text(
                    drawer,
                    join_nonempty(pass_parts, "   |   "),
                    style.body,
                    plan.left_width,
                    style.portrait ? 4 : 3);

                plan.detail_lines = wrap_text(
                    drawer,
                    join_fields(spec.pass.details),
                    style.small,
                    plan.left_width,
                    style.portrait ? 5 : 3);

                int left_height = (int)plan.identity_lines.size() * line_height(drawer, style.title);
                if (!plan.product_lines.empty())
                {
                    left_height += style.gap / 2 +
                                   (int)plan.product_lines.size() * line_height(drawer, style.product);
                }
                if (!plan.pass_lines.empty())
                {
                    left_height += style.gap / 2 +
                                   (int)plan.pass_lines.size() * line_height(drawer, style.body);
                }
                if (!plan.detail_lines.empty())
                {
                    left_height += style.gap / 2 +
                                   (int)plan.detail_lines.size() * line_height(drawer, style.small);
                }

                plan.height =
                    style.padding +
                    std::max(left_height, plan.badge_height) +
                    style.padding +
                    style.accent;
                return plan;
            }

            void draw_header(Image &output,
                             TextDrawer &drawer,
                             const PresentationSpec &spec,
                             const TextStyle &style,
                             const HeaderPlan &plan)
            {
                const Theme &theme = spec.theme;
                fill_rect(output, 0, 0, (int)output.width(), plan.height, theme.panel);
                fill_rect(output, 0, plan.height - style.accent, (int)output.width(), plan.height, theme.accent);

                int y = style.padding;
                y = draw_lines(output, drawer, plan.identity_lines, style.padding, y, style.title, theme.text);

                if (!plan.product_lines.empty())
                {
                    y += style.gap / 2;
                    y = draw_lines(output, drawer, plan.product_lines, style.padding, y, style.product, theme.text);
                }
                if (!plan.pass_lines.empty())
                {
                    y += style.gap / 2;
                    y = draw_lines(output, drawer, plan.pass_lines, style.padding, y, style.body, theme.muted_text);
                }
                if (!plan.detail_lines.empty())
                {
                    y += style.gap / 2;
                    draw_lines(output, drawer, plan.detail_lines, style.padding, y, style.small, theme.muted_text);
                }

                if (plan.badge)
                {
                    const int badge_x = (int)output.width() - style.padding - plan.badge_width;
                    const int badge_y = style.padding;
                    fill_rect(
                        output,
                        badge_x,
                        badge_y,
                        badge_x + plan.badge_width,
                        badge_y + plan.badge_height,
                        theme.panel_secondary);
                    fill_rect(
                        output,
                        badge_x,
                        badge_y,
                        badge_x + style.accent,
                        badge_y + plan.badge_height,
                        theme.accent);

                    int badge_text_y = badge_y + style.gap;
                    drawer.draw_text(
                        output,
                        badge_x + style.gap,
                        badge_text_y,
                        theme.muted_text,
                        style.small,
                        "КАЧЕСТВО");
                    badge_text_y += line_height(drawer, style.small) + style.gap / 2;
                    drawer.draw_text(
                        output,
                        badge_x + style.gap,
                        badge_text_y,
                        theme.text,
                        style.body,
                        plan.quality);
                    badge_text_y += line_height(drawer, style.body);
                    draw_lines(
                        output,
                        drawer,
                        plan.quality_detail_lines,
                        badge_x + style.gap,
                        badge_text_y,
                        style.small,
                        theme.muted_text);
                }
            }

            int title_block_height(TextDrawer &drawer,
                                   int available,
                                   const LegendSpec &legend,
                                   const TextStyle &style)
            {
                int height = 0;
                if (!legend.title.empty())
                {
                    const std::string title = legend.unit.empty()
                                                  ? legend.title
                                                  : legend.title + "  [" + legend.unit + "]";
                    height += std::max(1, (int)wrap_text(drawer, title, style.product, available, 2).size()) *
                              line_height(drawer, style.product);
                    height += style.gap / 2;
                }
                if (!legend.subtitle.empty())
                {
                    height += (int)wrap_text(
                                  drawer,
                                  legend.subtitle,
                                  style.small,
                                  available,
                                  style.portrait ? 4 : 3)
                                  .size() *
                              line_height(drawer, style.small);
                    height += style.gap / 2;
                }
                return height;
            }

            int categorical_height(TextDrawer &drawer,
                                   int available,
                                   const LegendSpec &legend,
                                   const TextStyle &style)
            {
                const int columns = style.wide && available >= 900 ? 2 : 1;
                const int cell_width = std::max(1, available / columns);
                const int marker = std::max(8, scaled(13, (double)style.body / 17.0));
                const int label_width = std::max(1, cell_width - marker - style.gap * 2);
                int height = 0;

                for (size_t row_start = 0; row_start < legend.categories.size(); row_start += (size_t)columns)
                {
                    int row_height = line_height(drawer, style.body);
                    for (int column = 0; column < columns; column++)
                    {
                        const size_t index = row_start + (size_t)column;
                        if (index >= legend.categories.size())
                            break;
                        const int lines = std::max(
                            1,
                            (int)wrap_text(
                                drawer,
                                legend.categories[index].label,
                                style.body,
                                label_width,
                                3)
                                .size());
                        row_height = std::max(row_height, lines * line_height(drawer, style.body));
                    }
                    height += row_height + style.gap;
                }
                return height;
            }

            int composite_height(TextDrawer &drawer,
                                 int available,
                                 const LegendSpec &legend,
                                 const TextStyle &style)
            {
                const int component_width = scaled(72, (double)style.body / 17.0);
                const int description_width = std::max(1, available - component_width - style.gap);
                int height = 0;
                for (const CompositeComponent &component : legend.components)
                {
                    const int lines = std::max(
                        1,
                        (int)wrap_text(
                            drawer,
                            component_description(component),
                            style.small,
                            description_width,
                            style.portrait ? 6 : 4)
                            .size());
                    height += std::max(
                                  line_height(drawer, style.body),
                                  lines * line_height(drawer, style.small)) +
                              style.gap;
                }
                return height;
            }

            int notes_height(TextDrawer &drawer,
                             int available,
                             const LegendSpec &legend,
                             const TextStyle &style)
            {
                int height = 0;
                for (const std::string &note : legend.notes)
                {
                    const int lines = (int)wrap_text(
                                          drawer,
                                          note,
                                          style.small,
                                          available,
                                          style.portrait ? 6 : 4)
                                          .size();
                    if (lines > 0)
                        height += lines * line_height(drawer, style.small) + style.gap / 2;
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
                height += title_block_height(drawer, available, spec.legend, style);

                if (spec.legend.kind == LegendKind::Continuous)
                {
                    height += style.legend_bar + style.gap / 2;
                    height += line_height(drawer, style.body) + style.gap;
                }
                else if (spec.legend.kind == LegendKind::Categorical)
                {
                    height += categorical_height(drawer, available, spec.legend, style);
                }
                else if (spec.legend.kind == LegendKind::Composite)
                {
                    height += composite_height(drawer, available, spec.legend, style);
                }

                height += notes_height(drawer, available, spec.legend, style);
                if (spec.show_branding && !spec.branding.empty())
                    height += line_height(drawer, style.small) + style.gap / 2;
                height += style.padding;
                return std::max(height, style.padding * 2);
            }

            std::vector<size_t> selected_tick_labels(TextDrawer &drawer,
                                                     const LegendSpec &legend,
                                                     const TextStyle &style,
                                                     int x0,
                                                     int bar_width)
            {
                std::vector<size_t> selected;
                if (legend.ticks.empty())
                    return selected;

                const int minimum_gap = std::max(style.gap, style.body / 2);
                const size_t last_index = legend.ticks.size() - 1;
                const LegendTick &last_tick = legend.ticks[last_index];
                const int last_center =
                    x0 + (int)std::round(clamp_value(last_tick.position, 0.0, 1.0) * std::max(0, bar_width - 1));
                const int last_width = measured(drawer, style.body, last_tick.label).width;
                const int last_left = last_center - last_width / 2;

                int previous_right = x0 - minimum_gap;
                for (size_t index = 0; index < legend.ticks.size(); index++)
                {
                    const LegendTick &tick = legend.ticks[index];
                    const int center =
                        x0 + (int)std::round(clamp_value(tick.position, 0.0, 1.0) * std::max(0, bar_width - 1));
                    const int label_width = measured(drawer, style.body, tick.label).width;
                    int left = center - label_width / 2;
                    int right = left + label_width;
                    left = std::max(x0, std::min(x0 + bar_width - label_width, left));
                    right = left + label_width;

                    const bool endpoint = index == 0 || index == last_index;
                    const bool clears_previous = left >= previous_right + minimum_gap;
                    const bool clears_last = index == last_index || right <= last_left - minimum_gap;
                    if (endpoint || (clears_previous && clears_last))
                    {
                        selected.push_back(index);
                        previous_right = right;
                    }
                }

                if (selected.empty() || selected.back() != last_index)
                    selected.push_back(last_index);
                return selected;
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
                    output.draw_line(
                        x0 + x,
                        y,
                        x0 + x,
                        y + style.legend_bar - 1,
                        sample_stops(spec.legend.color_stops, position, spec.theme));
                }
                output.draw_line(x0, y, x1 - 1, y, spec.theme.border);
                output.draw_line(
                    x0,
                    y + style.legend_bar - 1,
                    x1 - 1,
                    y + style.legend_bar - 1,
                    spec.theme.border);
                y += style.legend_bar + style.gap / 2;

                const int tick_height = std::max(3, style.gap / 2);
                for (const LegendTick &tick : spec.legend.ticks)
                {
                    const int x =
                        x0 + (int)std::round(clamp_value(tick.position, 0.0, 1.0) * std::max(0, bar_width - 1));
                    output.draw_line(x, y - style.gap / 2, x, y + tick_height, spec.theme.muted_text);
                }

                const std::vector<size_t> labels = selected_tick_labels(drawer, spec.legend, style, x0, bar_width);
                const int label_y = y + tick_height;
                for (size_t index : labels)
                {
                    const LegendTick &tick = spec.legend.ticks[index];
                    const int x =
                        x0 + (int)std::round(clamp_value(tick.position, 0.0, 1.0) * std::max(0, bar_width - 1));
                    const TextSize text_size = measured(drawer, style.body, tick.label);
                    int label_x = x - text_size.width / 2;
                    label_x = std::max(x0, std::min(x1 - text_size.width, label_x));
                    drawer.draw_text(
                        output,
                        label_x,
                        label_y,
                        spec.theme.text,
                        style.body,
                        tick.label);
                }
                y = label_y + line_height(drawer, style.body) + style.gap / 2;
            }

            void draw_categorical_legend(Image &output,
                                         TextDrawer &drawer,
                                         const PresentationSpec &spec,
                                         const TextStyle &style,
                                         int &y)
            {
                const int available = (int)output.width() - style.padding * 2;
                const int columns = style.wide && available >= 900 ? 2 : 1;
                const int cell_width = std::max(1, available / columns);
                const int marker = std::max(8, scaled(13, (double)style.body / 17.0));
                const int label_width = std::max(1, cell_width - marker - style.gap * 2);

                for (size_t row_start = 0; row_start < spec.legend.categories.size(); row_start += (size_t)columns)
                {
                    std::vector<std::vector<std::string>> row_lines((size_t)columns);
                    int row_height = line_height(drawer, style.body);
                    for (int column = 0; column < columns; column++)
                    {
                        const size_t index = row_start + (size_t)column;
                        if (index >= spec.legend.categories.size())
                            break;
                        row_lines[(size_t)column] = wrap_text(
                            drawer,
                            spec.legend.categories[index].label,
                            style.body,
                            label_width,
                            3);
                        if (row_lines[(size_t)column].empty())
                            row_lines[(size_t)column].push_back("");
                        row_height = std::max(
                            row_height,
                            (int)row_lines[(size_t)column].size() * line_height(drawer, style.body));
                    }

                    for (int column = 0; column < columns; column++)
                    {
                        const size_t index = row_start + (size_t)column;
                        if (index >= spec.legend.categories.size())
                            break;
                        const int x = style.padding + column * cell_width;
                        fill_rect(
                            output,
                            x,
                            y + (line_height(drawer, style.body) - marker) / 2,
                            x + marker,
                            y + (line_height(drawer, style.body) - marker) / 2 + marker,
                            normalized_color(spec.legend.categories[index].color, spec.theme.accent));
                        draw_lines(
                            output,
                            drawer,
                            row_lines[(size_t)column],
                            x + marker + style.gap,
                            y,
                            style.body,
                            spec.theme.text);
                    }
                    y += row_height + style.gap;
                }
            }

            void draw_composite_legend(Image &output,
                                       TextDrawer &drawer,
                                       const PresentationSpec &spec,
                                       const TextStyle &style,
                                       int &y)
            {
                const int available = (int)output.width() - style.padding * 2;
                const int marker = std::max(10, scaled(14, (double)style.body / 17.0));
                const int component_width = scaled(72, (double)style.body / 17.0);
                const int description_x = style.padding + component_width + style.gap;
                const int description_width = std::max(1, available - component_width - style.gap);

                for (const CompositeComponent &component : spec.legend.components)
                {
                    std::vector<std::string> lines = wrap_text(
                        drawer,
                        component_description(component),
                        style.small,
                        description_width,
                        style.portrait ? 6 : 4);
                    if (lines.empty())
                        lines.push_back("Канал не указан");

                    const int row_height = std::max(
                        line_height(drawer, style.body),
                        (int)lines.size() * line_height(drawer, style.small));
                    const Color color = component_color(component, spec.theme);
                    fill_rect(
                        output,
                        style.padding,
                        y + (line_height(drawer, style.body) - marker) / 2,
                        style.padding + marker,
                        y + (line_height(drawer, style.body) - marker) / 2 + marker,
                        color);
                    drawer.draw_text(
                        output,
                        style.padding + marker + style.gap / 2,
                        y,
                        color,
                        style.body,
                        component.component.empty() ? "IN" : component.component);
                    draw_lines(
                        output,
                        drawer,
                        lines,
                        description_x,
                        y,
                        style.small,
                        spec.theme.text);
                    y += row_height + style.gap;
                }
            }

            void draw_footer(Image &output,
                             TextDrawer &drawer,
                             const PresentationSpec &spec,
                             const TextStyle &style,
                             int footer_y,
                             int footer_height_value)
            {
                const int available = (int)output.width() - style.padding * 2;
                fill_rect(
                    output,
                    0,
                    footer_y,
                    (int)output.width(),
                    footer_y + footer_height_value,
                    spec.theme.panel);
                fill_rect(
                    output,
                    0,
                    footer_y,
                    (int)output.width(),
                    footer_y + 1,
                    spec.theme.border);

                int y = footer_y + style.padding;
                if (!spec.legend.title.empty())
                {
                    const std::string title = spec.legend.unit.empty()
                                                  ? spec.legend.title
                                                  : spec.legend.title + "  [" + spec.legend.unit + "]";
                    y = draw_lines(
                        output,
                        drawer,
                        wrap_text(drawer, title, style.product, available, 2),
                        style.padding,
                        y,
                        style.product,
                        spec.theme.text);
                    y += style.gap / 2;
                }

                if (!spec.legend.subtitle.empty())
                {
                    y = draw_lines(
                        output,
                        drawer,
                        wrap_text(
                            drawer,
                            spec.legend.subtitle,
                            style.small,
                            available,
                            style.portrait ? 4 : 3),
                        style.padding,
                        y,
                        style.small,
                        spec.theme.muted_text);
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
                    const std::vector<std::string> lines = wrap_text(
                        drawer,
                        note,
                        style.small,
                        available,
                        style.portrait ? 6 : 4);
                    if (!lines.empty())
                    {
                        y = draw_lines(
                            output,
                            drawer,
                            lines,
                            style.padding,
                            y,
                            style.small,
                            spec.theme.muted_text);
                        y += style.gap / 2;
                    }
                }

                if (spec.show_branding && !spec.branding.empty())
                {
                    const TextSize size = measured(drawer, style.small, spec.branding);
                    const int branding_x = std::max(
                        style.padding,
                        (int)output.width() - style.padding - size.width);
                    drawer.draw_text(
                        output,
                        branding_x,
                        y,
                        spec.theme.muted_text,
                        style.small,
                        spec.branding);
                }
            }
        }

        Image render(const Image &source, TextDrawer &text_drawer, const PresentationSpec &spec)
        {
            Image rgb = make_rgb(source);
            if (rgb.size() == 0)
                return rgb;

            const TextStyle style = build_text_style(rgb.width(), rgb.height(), spec.theme);
            const int width = (int)rgb.width();
            const HeaderPlan header = build_header_plan(text_drawer, width, spec, style);
            const int footer_height_value = footer_height(text_drawer, width, spec, style);

            Image output(
                rgb.depth(),
                rgb.width(),
                (size_t)header.height + rgb.height() + (size_t)footer_height_value,
                3);
            output.fill_color(spec.theme.panel);
            output.draw_image(0, rgb, 0, header.height);

            draw_header(output, text_drawer, spec, style, header);
            draw_footer(
                output,
                text_drawer,
                spec,
                style,
                header.height + (int)rgb.height(),
                footer_height_value);
            return output;
        }
    }
}
