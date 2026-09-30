#pragma once
#include <cstddef>

namespace meteor { namespace mtvza {
// Commit only an ordered, complete scan. Losing one scan never corrupts its neighbours.
class ScanSequence
{
    int first_, last_, next_;
    bool active_ = false;
public:
    size_t rejected_frames = 0, incomplete_scans = 0;
    ScanSequence(int first, int last) : first_(first), last_(last), next_(first) {}
    void finish()
    {
        if (active_) ++incomplete_scans;
        active_ = false;
        next_ = first_;
    }
    bool accept(int counter)
    {
        if (counter == first_) { finish(); active_ = true; }
        if (!active_ || counter != next_ || counter > last_)
        {
            ++rejected_frames;
            finish();
            return false;
        }
        ++next_;
        if (counter == last_) { active_ = false; next_ = first_; }
        return true;
    }
};
} }
