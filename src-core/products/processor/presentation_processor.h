#pragma once

#include "../image_products.h"
#include "common/image/presentation.h"
#include "common/image/text.h"

#include <cctype>
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

        // Raw 1.2.2 metadata builder. The implementation unit is compiled with a
        // source-local macro that renames its historical build_spec definition to
        // this symbol. New callers use build_spec below and therefore always pass
        // through the semantic interpretation layer.
        image::presentation::PresentationSpec build_spec_raw(
            ImageProducts &products,
            const ImageCompositeCfg &composite,
            const nlohmann::json &composite_preset,
            const std::string &product_name,
            const std::vector<double> &timestamps,
            const nlohmann::json &product_metadata,
            const std::string &source_variant);

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

// Keep the large historical implementation unchanged while exposing an
// enriched public symbol from presentation_semantics.cpp.
#ifdef SATDUMP_PRESENTATION_PROCESSOR_IMPL
#define build_spec build_spec_raw
#endif
