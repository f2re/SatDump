#include "processor.h"
#include "../product_status.h"
#include "logger.h"
#include "../dataset.h"
#include "../image_products.h"
#include "core/config.h"
#include <filesystem>

namespace satdump
{
    void process_product(std::string product_path)
    {
        logger->info(product_path);
        std::shared_ptr<Products> products = loadProducts(product_path);

        if (products_loaders.count(products->type) > 0)
            products_loaders[products->type].processProducts(products.get(), product_path);
        else
            logger->error("No handler for type " + products->type);
    }

    void process_dataset(std::string dataset_path)
    {
        ProductDataSet dataset;
        dataset.load(dataset_path);

        std::string pro_directory = std::filesystem::path(dataset_path).parent_path().string();
        nlohmann::json results = nlohmann::json::array();
        size_t completed = 0;
        for (std::string pro_path : dataset.products_list)
        {
            try
            {
                process_product(pro_directory + "/" + pro_path);
                results.push_back({{"product", pro_path}, {"status", "processed"}});
                ++completed;
            }
            catch (const std::exception &e)
            {
                logger->error("Product %s failed: %s", pro_path.c_str(), e.what());
                results.push_back({{"product", pro_path}, {"status", "failed"}, {"reason", e.what()}});
            }
        }
        write_product_status(pro_directory + "/dataset-processing.json",
            {{"schema", "satdump.dataset-processing/1"}, {"products", results}});
        if (completed == 0) throw std::runtime_error("No products processed; see dataset-processing.json");
    }
}