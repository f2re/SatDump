#pragma once
#include "nlohmann/json.hpp"
#include "common/image/io.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace satdump {
// The temporary file is promoted only after its writer closes successfully.
inline void write_product_bytes(const std::string &path, const std::vector<uint8_t> &bytes)
{
    const std::string temp = path + ".partial";
    try
    {
        std::ofstream out;
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out.open(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        out.flush();
        out.close();
        std::filesystem::rename(temp, path);
    }
    catch (...)
    {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
}
inline void write_product_status(const std::string &path, const nlohmann::json &status)
{
    const std::string text = status.dump(2) + "\n";
    write_product_bytes(path, std::vector<uint8_t>(text.begin(), text.end()));
}
inline std::string save_checked_image(image::Image &img, std::string path)
{
    if (!img.size() || !img.width() || !img.height()) throw std::runtime_error("Empty image: " + path);
    if (!image::append_ext(&path)) throw std::runtime_error("Unsupported image format: " + path);
    const std::string temp = path + ".partial";
    try
    {
        image::save_img(img, temp);
        if (!std::filesystem::is_regular_file(temp) || !std::filesystem::file_size(temp))
            throw std::runtime_error("Image encoder did not write: " + path);
        std::filesystem::rename(temp, path);
    }
    catch (...)
    {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        throw;
    }
    return path;
}
}
