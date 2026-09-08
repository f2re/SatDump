#pragma once
#include "init.h"
#include "core/config.h"
#include "products/products.h"
#include "logger.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>

// Administrator-owned processing overrides. Never populated from image sidecars.
inline void station_processing_config(const std::string &filename)
{
    if (filename.empty())
        return;
    std::ifstream stream(filename);
    if (!stream)
        throw std::runtime_error("Cannot read processing config: " + filename);
    nlohmann::ordered_json patch;
    stream >> patch;
    if (!patch.is_object())
        throw std::runtime_error("Processing config must be a JSON object");
    satdump::config::main_cfg.merge_patch(patch);
}

inline int main_reprocess(int argc, char *argv[])
{
    if (argc != 3 && argc != 4)
    {
        logger->error("Usage: satdump reprocess <copied-product-directory> [processing.json]");
        return 2;
    }
    try
    {
        const auto directory = std::filesystem::absolute(argv[2]);
        if (!std::filesystem::is_regular_file(directory / "product.cbor"))
            throw std::runtime_error("Missing product.cbor in local product directory");
        // Reprocessing is explicitly offline; use TLE/geolocation saved with the product.
        satdump::tle_do_update_on_init = false;
        satdump::initSatdump();
        completeLoggerInit();
        station_processing_config(argc == 4 ? argv[3] : "");
        auto products = satdump::loadProducts(directory.string());
        if (!products)
            throw std::runtime_error("Could not load product");
        products->contents["autocomposite_cache_enabled"] = false;
        if (satdump::products_loaders.count(products->type) == 0)
            throw std::runtime_error("No product processor for " + products->type);
        satdump::products_loaders[products->type].processProducts(products.get(), directory.string());
        // Caller also verifies a fresh presentation PNG and its machine-readable passport.
        return 0;
    }
    catch (const std::exception &error)
    {
        logger->error("Reprocess failed: %s", error.what());
        return 1;
    }
}
