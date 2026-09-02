#pragma once

#include "../image_products.h"
#include "common/image/presentation.h"

#include <nlohmann/json.hpp>
#include <string>

namespace satdump
{
    namespace product_presentation
    {
        // Enriches the generic presentation specification with a scientifically
        // cautious, Russian-language product description and an interpretable
        // legend. Explicit presentation.legend settings always remain authoritative.
        void apply_presentation_semantics(
            image::presentation::PresentationSpec &spec,
            ImageProducts &products,
            const ImageCompositeCfg &composite,
            const nlohmann::json &composite_preset,
            const std::string &product_name);
    }
}
