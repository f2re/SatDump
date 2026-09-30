#include "../plugins/meteor_support/meteor/instruments/mtvza/mtvza_reader.h"
#include "products/product_status.h"
#include "logger.h"
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

static void frame(meteor::mtvza::MTVZAReader &reader, int counter, int value)
{
    std::array<uint8_t, 248> data{};
    data[reader.endian_mode ? 5 : 4] = 255;
    data[reader.endian_mode ? 4 : 5] = counter;
    for (int base : {8, 128})
        for (int i = 0; i < 59; ++i)
        {
            const unsigned sample = 32768 + value;
            data[base + 2 * i + (reader.endian_mode ? 0 : 1)] = sample >> 8;
            data[base + 2 * i + (reader.endian_mode ? 1 : 0)] = sample & 255;
        }
    reader.work(data.data());
}

int main(int argc, char **argv)
{
    try
    {
        for (bool endian : {false, true})
        {
            meteor::mtvza::MTVZAReader r;
            r.endian_mode = endian;
            frame(r, 26, 900); // no start: never a complete row
            require(r.lines == 0, "terminal frame alone created a row");
            for (int i = 2; i <= 26; ++i) frame(r, i, 111);
            for (int i = 2; i <= 26; ++i) if (i != 13) frame(r, i, 999);
            for (int i = 2; i <= 26; ++i) frame(r, i, 222);
            require(r.lines == 2, "lost scan was not discarded/recovery failed");
            for (int ch = 0; ch < 30; ++ch)
            {
                auto image = r.getChannel(ch);
                require(image.width() == 100 && image.height() == 2, "HRPT geometry changed");
                for (size_t x = 0; x < 100; ++x)
                {
                    require(image.get(x) == 111, "previous row overwritten");
                    require(image.get(100 + x) == 222, "pending scan contaminated next row");
                }
            }
            frame(r, 2, 333); frame(r, 3, 333); r.finish();
            require(r.lines == 2 && r.sequence.incomplete_scans == 2, "incomplete end not reported");
            require(r.timestamps.size() == 2, "timestamp/line count mismatch");
        }
        meteor::mtvza::ScanSequence sequence(1, 51);
        for (int i = 1; i <= 51; ++i) require(sequence.accept(i), "complete dump scan rejected");
        require(!sequence.accept(51), "duplicate dump terminator accepted");
        sequence.accept(1); sequence.accept(2);
        require(!sequence.accept(2), "duplicate data frame accepted");
        for (int i = 1; i <= 51; ++i) require(sequence.accept(i), "dump resynchronization failed");
        if (argc == 2)
        {
            const auto root = std::filesystem::path(argv[1]);
            std::filesystem::create_directories(root);
            satdump::write_product_status((root / "status.json").string(), {{"ok", true}});
            require(std::filesystem::file_size(root / "status.json") > 0, "status was not saved");
            bool failed = false;
            try { satdump::write_product_status((root / "missing/status.json").string(), {{"ok", true}}); }
            catch (const std::exception &) { failed = true; }
            require(failed, "I/O failure silently accepted");
        }
        std::cout << "MTVZA: both byte orders, gaps, duplicates, recovery, row isolation and checked status writes OK\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
