#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "common/image/image.h"
#include "scan_sequence.h"

namespace meteor { namespace mtvza {
class MTVZAReader
{
    std::vector<uint16_t> channels[30];
    std::array<std::array<uint16_t, 200>, 30> pending{};
    void parse_samples(uint8_t *, int, int, int, int, int);
public:
    MTVZAReader() = default;
    ~MTVZAReader() = default;
    int lines = 0;
    std::vector<double> timestamps;
    ScanSequence sequence{2, 26};
    double latest_msumr_timestamp = -1;
    void work(uint8_t *data);
    void finish() { sequence.finish(); }
    image::Image getChannel(int channel);
    bool endian_mode = false;
};
} }
