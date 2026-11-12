#pragma once
#include <array>
#include <cstddef>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace voxmask {
struct RingMetrics {
    uint64_t pushed_frames, read_frames, overflow_blocks, invalid_blocks;
    uint64_t stale_frames, epoch_frames, future_frames, missing_frames;
};
struct ReadResult {
    uint32_t frames = 0;
    uint32_t discarded = 0;
    uint64_t first_capture_ns = 0;
    uint64_t first_source_frame = 0;
    uint64_t max_age_ns = 0;
    bool discontinuity = false;
};

// Fixed mono PCM storage. Exactly one producer calls push; exactly one consumer
// calls read. Metrics may be polled concurrently. Neither side moves the other's
// index. No concurrent reset/destruction. Source/destination must not alias storage.
// The caller supplies timestamps in one monotonic clock domain and nonzero epochs.
// This is transport, not a voice-conversion or security boundary.
template<uint32_t Capacity>
class FrameRing final {
    static_assert(Capacity > 0 && (Capacity & (Capacity-1)) == 0,
                  "capacity must be a power of two");
    static_assert(Capacity <= (1u<<20), "bound per-read discard work");
    static_assert(std::atomic<uint64_t>::is_always_lock_free,
                  "lock-free 64-bit atomics required");
    struct Frame {
        float sample = 0;
        uint64_t capture_ns = 0;
        uint64_t source_frame = 0;
        uint64_t epoch = 0;
    };
public:
    static constexpr uint32_t capacity = Capacity;
    static constexpr size_t storage_bytes = sizeof(Frame) * Capacity;

    // All-or-nothing write. Full queue drops the incoming block, never oldest data.
    bool push(const float *input, uint32_t count, uint64_t first_ns,
              uint64_t source_frame, uint64_t epoch, uint32_t rate) noexcept {
        if (!input || !count || count>Capacity || !epoch || !rate || rate>192000 ||
            source_frame>UINT64_MAX-count ||
            first_ns>UINT64_MAX-(uint64_t(count-1)*1000000000/rate)) {
            invalid.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        const uint64_t w=write_pos.load(std::memory_order_relaxed);
        const uint64_t r=read_pos.load(std::memory_order_acquire);
        if (count>Capacity-(w-r)) {
            overflow.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        for(uint32_t i=0;i<count;++i) {
            if(!std::isfinite(input[i])) {
                invalid.fetch_add(1,std::memory_order_relaxed);
                return false;
            }
        }
        for(uint32_t i=0;i<count;++i) {
            auto &f=data[(w+i)&(Capacity-1)];
            f={input[i],first_ns+uint64_t(i)*1000000000/rate,source_frame+i,epoch};
        }
        write_pos.store(w+count,std::memory_order_release);
        pushed.fetch_add(count,std::memory_order_relaxed);
        return true;
    }

    // Valid requests zero fill first. Invalid pointer/count requests do not touch memory.
    // Discards expired/wrong-epoch/future frames, never replays
    // old samples. Work is bounded by 2*Capacity + requested_count. request<=Capacity.
    // Callers must reset downstream state on discontinuity before trusting a
    // stateful model; this method does not invent continuity across lost samples.
    ReadResult read(float *output, uint32_t requested, uint64_t now_ns,
                    uint64_t epoch, uint64_t max_age_ns) noexcept {
        ReadResult result;
        if(!output || requested>Capacity) {result.discontinuity=true;return result;}
        std::fill_n(output,requested,0.0f);
        if(!epoch) {result.discontinuity=true;return result;}
        uint64_t r=read_pos.load(std::memory_order_relaxed);
        const uint64_t end=write_pos.load(std::memory_order_acquire);
        while(r!=end && result.frames<requested) {
            const Frame &f=data[r&(Capacity-1)];
            bool discard=false;
            if(f.epoch!=epoch) {old_epoch.fetch_add(1,std::memory_order_relaxed);discard=true;}
            else if(f.capture_ns>now_ns) {future.fetch_add(1,std::memory_order_relaxed);discard=true;}
            else if(now_ns-f.capture_ns>max_age_ns) {stale.fetch_add(1,std::memory_order_relaxed);discard=true;}
            if(discard) {++result.discarded;result.discontinuity=true;}
            else {
                if(have_previous && (previous_epoch!=f.epoch || previous_frame+1!=f.source_frame))
                    result.discontinuity=true;
                if(!result.frames) {
                    result.first_capture_ns=f.capture_ns;
                    result.first_source_frame=f.source_frame;
                }
                result.max_age_ns=std::max(result.max_age_ns,now_ns-f.capture_ns);
                output[result.frames++]=f.sample;
                previous_frame=f.source_frame;previous_epoch=f.epoch;have_previous=true;
            }
            ++r;
        }
        // PCM remains transient: clear consumed storage before returning ownership.
        const uint64_t begin=read_pos.load(std::memory_order_relaxed);
        for(uint64_t i=begin;i!=r;++i)data[i&(Capacity-1)]={};
        read_pos.store(r,std::memory_order_release);
        read_count.fetch_add(result.frames,std::memory_order_relaxed);
        if(result.frames<requested) {
            missing.fetch_add(requested-result.frames,std::memory_order_relaxed);
            result.discontinuity=true;
        }
        return result;
    }
    RingMetrics metrics() const noexcept {
        return {pushed.load(),read_count.load(),overflow.load(),invalid.load(),
                stale.load(),old_epoch.load(),future.load(),missing.load()};
    }
private:
    std::array<Frame,Capacity> data{};
    alignas(64) std::atomic<uint64_t> write_pos{0};
    alignas(64) std::atomic<uint64_t> read_pos{0};
    // Only consumer touches these fields.
    bool have_previous=false;
    uint64_t previous_frame=0,previous_epoch=0;
    std::atomic<uint64_t> pushed{0},read_count{0},overflow{0},invalid{0};
    std::atomic<uint64_t> stale{0},old_epoch{0},future{0},missing{0};
};
}
