#pragma once

#include "presentation_processor.h"
#include "common/image/presentation_layout.h"

#include <string>

namespace satdump
{
    namespace product_presentation
    {
        struct OutputSettings
        {
            bool enabled = true;
            bool save_minimal = true;
            bool save_editorial = true;
            bool save_legacy_alias = false;
            bool north_up = true;
            std::string orientation_mode = "auto";
        };

        struct OutputResult
        {
            bool minimal = false;
            bool editorial = false;
            bool legacy_alias = false;
            image::presentation::OrientationInfo orientation;

            bool any() const { return minimal || editorial || legacy_alias; }
        };

        OutputSettings resolve_output_settings(const nlohmann::json &composite_preset = nlohmann::json());

        image::presentation::OrientationInfo analyze_orientation(
            const image::Image &source,
            ImageProducts &products,
            const std::vector<double> &timestamps,
            const nlohmann::json &product_metadata,
            const std::string &source_variant,
            const OutputSettings &settings);

        OutputResult save_outputs(
            const image::Image &source,
            image::TextDrawer &text_drawer,
            ImageProducts &products,
            const ImageCompositeCfg &composite,
            const nlohmann::json &composite_preset,
            const std::string &product_name,
            const std::vector<double> &timestamps,
            const nlohmann::json &product_metadata,
            const std::string &source_variant,
            const std::string &base_path);
    }
}
