#pragma once

#include "presentation_processor.h"
#include "common/image/presentation_layout.h"

#include <string>
#include <functional>

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
            // Machine-readable per-pass package consumed by the existing PHP
            // meteo board. It contains clean imagery, derivatives and metadata.
            bool prepare_online_board = true;
            std::string online_board_directory = "online-board";
            int online_board_max_width = 2300;
            int online_board_max_height = 1294;
            bool north_up = true;
            std::string orientation_mode = "auto";
        };

        struct OutputResult
        {
            bool minimal = false;
            bool editorial = false;
            bool legacy_alias = false;
            bool online_board = false;
            image::presentation::OrientationInfo orientation;

            bool any() const { return minimal || editorial || legacy_alias || online_board; }
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
            const std::string &base_path,
            const std::function<void(image::Image &, image::presentation::RasterTransform)> &decorate_oriented = {});
    }
}
