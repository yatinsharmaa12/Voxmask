#pragma once

#include "producer_protocol.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace voxmask {

// One writer (the validated transport worker), any number of HAL readers.
// All shared sample bits are atomic: a reader that races a writer discards the
// affected slot instead of returning torn audio. No allocation or locks in read().
class ProducerMailbox final {
public:
    static constexpr uint64_t max_age_ns = 250000000;
    static constexpr size_t slot_count = 8;

    void publish(const ProducerBlock& block, uint64_t output_frame,
                 uint64_t now_ns) noexcept {
        const auto current_epoch = active_epoch.load(std::memory_order_acquire);
        if (retired_epoch.load(std::memory_order_acquire) == block.epoch ||
            (current_epoch && current_epoch != block.epoch) ||
            block.sample_rate != 48000 || block.frames == 0 || block.frames > producer_max_frames ||
            block.epoch == 0 || now_ns < block.capture_time_ns ||
            now_ns - block.capture_time_ns > max_age_ns) return;
        for (uint32_t i = 0; i < block.frames; ++i)
            if (!std::isfinite(block.samples[i])) return;
        auto& slot = slots[block.sequence % slot_count];
        slot.generation.fetch_add(1, std::memory_order_acq_rel); // writing
        slot.start.store(output_frame, std::memory_order_relaxed);
        slot.frames.store(block.frames, std::memory_order_relaxed);
        slot.epoch.store(block.epoch, std::memory_order_relaxed);
        slot.published_ns.store(now_ns, std::memory_order_relaxed);
        for (uint32_t i = 0; i < block.frames; ++i) {
            uint32_t bits;
            std::memcpy(&bits, &block.samples[i], sizeof(bits));
            slot.samples[i].store(bits, std::memory_order_relaxed);
        }
        slot.generation.fetch_add(1, std::memory_order_release); // published
        active_epoch.store(block.epoch, std::memory_order_release);
        if (retired_epoch.load(std::memory_order_acquire) == block.epoch) {
            uint64_t expected = block.epoch;
            active_epoch.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
        }
    }

    void close(uint64_t epoch) noexcept {
        retired_epoch.store(epoch, std::memory_order_release);
        uint64_t expected = epoch;
        active_epoch.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    }

    void read(uint64_t output_frame, uint32_t frames, uint64_t now_ns,
              float* out) const noexcept {
        if (!out || !frames) return;
        std::memset(out, 0, size_t(frames) * sizeof(float));
        const auto epoch = active_epoch.load(std::memory_order_acquire);
        if (!epoch) return;
        for (const auto& slot : slots) {
            const auto before = slot.generation.load(std::memory_order_acquire);
            if (before & 1) continue;
            const auto slot_epoch = slot.epoch.load(std::memory_order_relaxed);
            const auto start = slot.start.load(std::memory_order_relaxed);
            const auto count = slot.frames.load(std::memory_order_relaxed);
            const auto published = slot.published_ns.load(std::memory_order_relaxed);
            if (slot_epoch != epoch || !count || published > now_ns ||
                now_ns - published > max_age_ns || output_frame >= start + count ||
                start >= output_frame + frames) continue;
            const auto first = output_frame > start ? output_frame : start;
            const auto last = (output_frame + frames < start + count) ?
                output_frame + frames : start + count;
            for (uint64_t frame = first; frame < last; ++frame) {
                uint32_t bits = slot.samples[frame - start].load(std::memory_order_relaxed);
                std::memcpy(&out[frame - output_frame], &bits, sizeof(bits));
            }
            const auto after = slot.generation.load(std::memory_order_acquire);
            if (before != after || active_epoch.load(std::memory_order_acquire) != epoch)
                std::memset(out + (first - output_frame), 0,
                            size_t(last - first) * sizeof(float));
        }
        if (active_epoch.load(std::memory_order_acquire) != epoch)
            std::memset(out, 0, size_t(frames) * sizeof(float));
    }

private:
    struct Slot {
        std::atomic<uint64_t> generation{0}, start{0}, epoch{0}, published_ns{0};
        std::atomic<uint32_t> frames{0};
        std::array<std::atomic<uint32_t>, producer_max_frames> samples{};
    };
    std::array<Slot, slot_count> slots{};
    std::atomic<uint64_t> active_epoch{0};
    std::atomic<uint64_t> retired_epoch{0};
};

} // namespace voxmask
