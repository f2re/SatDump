#pragma once

#include "../image_products.h"
#include "common/image/presentation.h"
#include "common/image/text.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace satdump
{
    namespace product_presentation
    {
        // The presentation layer is additive: it never replaces the scientific
        // raster produced by SatDump. When enabled it writes an annotated PNG and
        // a JSON sidecar next to the normal product.
        bool enabled(const nlohmann::json &composite_preset = nlohmann::json());

        image::presentation::PresentationSpec build_spec(
            ImageProducts &products,
            const ImageCompositeCfg &composite,
            const nlohmann::json &composite_preset,
            const std::string &product_name,
            const std::vector<double> &timestamps,
            const nlohmann::json &product_metadata,
            const std::string &source_variant);

        bool save(
            const image::Image &source,
            image::TextDrawer &text_drawer,
            const image::presentation::PresentationSpec &spec,
            const std::string &png_path);
    }
}