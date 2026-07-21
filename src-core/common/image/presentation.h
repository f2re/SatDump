#pragma once

#include "image.h"
#include "text.h"

#include <string>
#include <vector>

namespace image
{
    namespace presentation
    {
        using Color = std::vector<double>;

        enum class LegendKind
        {
            None,
            Continuous,
            Categorical,
            Composite
        };

        struct MetadataField
        {
            std::string label;
            std::string value;
        };

        struct PassInfo
        {
            std::string satellite;
            std::string instrument;
            std::string product;
            std::string acquisition_time;
            std::string pass_summary;
            std::vector<MetadataField> details;
            std::string quality;
            std::string quality_detail;
        };

        struct ColorStop
        {
            double position = 0.0; // Normalized position in the range [0, 1]
            Color color = {0.0, 0.0, 0.0};
        };

        struct LegendTick
        {
            double position = 0.0; // Normalized position in the range [0, 1]
            std::string label;
        };

        struct CategoryEntry
        {
            Color color = {0.5, 0.5, 0.5};
            std::string label;
        };

        struct CompositeComponent
        {
            std::string component; // R, G, B, A or an arbitrary input label
            Color marker_color = {0.305882, 0.780392, 0.909804};
            std::string channel;
            std::string spectral_range;
            std::string quantity;
            std::string formula;
            std::string description;
        };

        struct LegendSpec
        {
            LegendKind kind = LegendKind::None;
            std::string title;
            std::string subtitle;
            std::string unit;
            std::vector<ColorStop> color_stops;
            std::vector<LegendTick> ticks;
            std::vector<CategoryEntry> categories;
            std::vector<CompositeComponent> components;
            std::vector<std::string> notes;
        };

        struct Theme
        {
            Color panel = {0.054902, 0.086275, 0.141176};       // #0E1624
            Color panel_secondary = {0.090196, 0.133333, 0.207843}; // #172235
            Color border = {0.164706, 0.227451, 0.313725};      // #2A3A50
            Color text = {0.952941, 0.968627, 0.984314};        // #F3F7FB
            Color muted_text = {0.666667, 0.721569, 0.784314};  // #AAB8C8
            Color accent = {0.305882, 0.780392, 0.909804};      // #4EC7E8
            Color warning = {1.0, 0.705882, 0.329412};          // #FFB454
            Color error = {1.0, 0.419608, 0.419608};            // #FF6B6B
            Color red_component = {0.95, 0.28, 0.28};
            Color green_component = {0.25, 0.82, 0.48};
            Color blue_component = {0.30, 0.58, 1.0};

            double minimum_scale = 0.65;
            double maximum_scale = 2.0;
            int reference_width = 1600;
        };

        struct PresentationSpec
        {
            PassInfo pass;
            LegendSpec legend;
            Theme theme;
            std::string branding = "SatDump 1.2.2";
            bool show_branding = true;
        };

        // Builds a separate presentation image. The source raster is copied without
        // resampling and is placed between a header and a legend/footer panel.
        // Geospatial products should still be saved separately before decoration.
        Image render(const Image &source, TextDrawer &text_drawer, const PresentationSpec &spec);
    }
}