#include "mtvza_reader.h"
#include <stdexcept>

namespace meteor { namespace mtvza {
void MTVZAReader::parse_samples(uint8_t *data, int ch_start, int offset, int ch_cnt, int nsamples, int counter)
{
    for (int ch = 0; ch < ch_cnt; ++ch)
        for (int i = 0; i < 4; ++i)
        {
            const int sample = ch * nsamples + offset + (nsamples == 2 ? i / 2 : nsamples == 4 ? i : 0);
            pending[ch_start + ch][counter * 8 + i] =
                (data[8 + sample * 2 + (endian_mode ? 0 : 1)] << 8 |
                 data[8 + sample * 2 + (endian_mode ? 1 : 0)]) - 32768;
            pending[ch_start + ch][counter * 8 + 4 + i] =
                (data[128 + sample * 2 + (endian_mode ? 0 : 1)] << 8 |
                 data[128 + sample * 2 + (endian_mode ? 1 : 0)]) - 32768;
        }
}

void MTVZAReader::work(uint8_t *data)
{
    if (data[endian_mode ? 5 : 4] != 255)
    {
        ++sequence.rejected_frames;
        sequence.finish();
        return;
    }
    const int counter = data[endian_mode ? 4 : 5];
    if (!sequence.accept(counter)) return;
    if (counter == 2)
        for (auto &channel : pending) channel.fill(0);
    parse_samples(data, 0, 0, 5, 1, counter - 2);
    parse_samples(data, 5, 5, 2, 4, counter - 2);
    parse_samples(data, 7, 13, 23, 2, counter - 2);
    if (counter == 26)
    {
        // Preserve the historical 100-pixel HRPT window and its projection.
        // Never write the 200-sample pending scan over an adjacent output row.
        for (int ch = 0; ch < 30; ++ch)
            channels[ch].insert(channels[ch].end(), pending[ch].begin(), pending[ch].begin() + 100);
        timestamps.push_back(latest_msumr_timestamp);
        ++lines;
    }
}

image::Image MTVZAReader::getChannel(int channel)
{
    if (channel < 0 || channel >= 30) throw std::out_of_range("MTVZA channel");
    if (!lines) return image::Image();
    return image::Image(channels[channel].data(), 16, 100, lines, 1);
}
} }
