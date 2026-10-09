#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace oceanblast {
// Host-only capture of complete, origin-first framebuffer write sweeps.
// This observes guest stores; it does not alter memory or peripheral timing.
class FrameLatch {
public:
    void configure(uint32_t address, size_t bytes) {
        if (address == address_ && bytes == image_.size()) return;
        address_ = address;
        image_.assign(bytes, 0);
        touched_.assign((bytes + 63) / 64, 0);
        covered_ = 0; waitingForOrigin_ = true; ready_ = false;
    }
    void store(size_t offset, size_t bytes, const uint8_t* source) {
        if (offset >= image_.size() || !bytes) return;
        bytes = std::min(bytes, image_.size() - offset);
        if (offset == 0) {
            // A new sweep must not complete coverage left over from an abandoned frame.
            if (covered_) ++abandoned_;
            std::fill(touched_.begin(), touched_.end(), 0);
            covered_ = 0; waitingForOrigin_ = false;
        }
        if (waitingForOrigin_) return;
        while (bytes) {
            const size_t bit = offset & 63, count = std::min(bytes, 64 - bit);
            const uint64_t mask = count == 64 ? ~uint64_t(0) : ((uint64_t(1) << count) - 1) << bit;
            uint64_t& word = touched_[offset >> 6];
            const uint64_t added = mask & ~word;
            if (added == mask) covered_ += count;
            else if (added) covered_ += size_t(__builtin_popcountll(added));
            word |= mask; offset += count; bytes -= count;
        }
        if (covered_ == image_.size()) {
            std::memcpy(image_.data(), source, image_.size());
            ready_ = true; ++completed_; covered_ = 0; waitingForOrigin_ = true;
        }
    }
    const uint8_t* data() const { return image_.data(); }
    size_t size() const { return image_.size(); }
    size_t covered() const { return covered_; }
    bool ready() const { return ready_; }
    uint64_t completed() const { return completed_; }
    uint64_t abandoned() const { return abandoned_; }
private:
    uint32_t address_ = 0;
    std::vector<uint8_t> image_;
    std::vector<uint64_t> touched_;
    size_t covered_ = 0;
    bool waitingForOrigin_ = true, ready_ = false;
    uint64_t completed_ = 0, abandoned_ = 0;
};
}
