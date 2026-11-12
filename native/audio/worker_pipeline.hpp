#pragma once
#include "processor.hpp"
#include "frame_ring.hpp"
#include <thread>
#include <array>

namespace voxmask {
static_assert(std::atomic<bool>::is_always_lock_free, "lock-free callback flags required");
inline uint64_t monotonic_ns() noexcept {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// One callback thread owns input production/output consumption. One worker owns
// input consumption/output production. Configure once before either starts.
// The worker is a diagnostic gain/clamp processor, NOT AI conversion.
class PipelineCore final {
public:
    static constexpr uint64_t max_age_ns=250000000;
    bool configure(uint32_t sample_rate,bool raw_monitor) noexcept {
        if(configured || (sample_rate!=44100 && sample_rate!=48000 && sample_rate!=96000))return false;
        rate=sample_rate;hop=rate/100;monitor=raw_monitor;
        processor.monitor=monitor;
        configured=true;
        active.store(true,std::memory_order_release);
        return true;
    }
    void silence() noexcept { fault.store(1,std::memory_order_release); }
    bool failed() const noexcept { return fault.load(std::memory_order_acquire)!=0; }

    void callback(float *out,const float *in,uint32_t count,uint64_t now_ns) noexcept {
        const auto begin=monotonic_ns();
        if(out)std::fill_n(out,count,0.0f);
        callbacks.fetch_add(1,std::memory_order_relaxed);
        frames.fetch_add(count,std::memory_order_relaxed);
        if(count>max_frames.load())max_frames.store(count);
        if(active.load(std::memory_order_acquire) && !failed()) {
            if(!in || !count || count>VM_AUDIO_MAX_FRAMES) {
                rejected.fetch_add(1);silence();
            } else {
                // Read older processed data first: never fall back to current raw input.
                if(out && monitor)output.read(out,count,now_ns,1,max_age_ns);
                // miniaudio does not supply physical capture timestamps here. Estimate
                // first sample relative to callback arrival; hardware latency is excluded.
                const auto span=uint64_t(count-1)*1000000000/rate;
                const auto first=now_ns>=span?now_ns-span:0;
                const bool accepted=input.push(in,count,first,source_frame,1,rate);
                source_frame+=count;
                if(!accepted) {
                    if(!input.metrics().overflow_blocks)rejected.fetch_add(1);
                    silence();
                }
            }
        }
        if(failed() && out)std::fill_n(out,count,0.0f);
        const auto elapsed=monotonic_ns()-begin;
        if(elapsed>max_callback_ns.load())max_callback_ns.store(elapsed);
    }

    // Worker thread only. Also independently exercised by deterministic tests.
    // Returns true when it consumed work; caller may sleep when false.
    bool work_once(uint64_t now_ns) noexcept {
        if(!active.load(std::memory_order_acquire) || failed() || input.available()<hop)return false;
        auto read=input.read(input_block.data(),hop,now_ns,1,max_age_ns);
        if(read.discontinuity) {
            discontinuities.fetch_add(1);
            std::fill(input_block.begin(),input_block.end(),0.0f);
            // No stateful model exists yet. Discard this chunk rather than pretending
            // it is contiguous; a future model adapter must reset/re-prime here.
            return true;
        }
        processor.process(monitor?output_block.data():nullptr,monitor?hop:0,
                          input_block.data(),read.frames,read.frames);
        if(!failed() && monitor) {
            // Conservatively stamp every output sample with the oldest contributing
            // input time, so chunk framing cannot make old audio appear younger.
            if(!output.push(output_block.data(),read.frames,read.oldest_capture_ns,
                            read.first_source_frame,1,rate,false))silence();
        }
        std::fill(input_block.begin(),input_block.end(),0.0f);
        std::fill(output_block.begin(),output_block.end(),0.0f);
        return true;
    }
    // Callback/consumer thread only; useful for prefill and deterministic tests.
    uint32_t ready_output_frames() const noexcept { return output.available(); }
    vm_audio_metrics snapshot() const noexcept {
        auto m=processor.snapshot();
        m.processed_frames=m.frames;
        m.callbacks=callbacks.load();m.frames=frames.load();
        m.max_callback_frames=max_frames.load();m.max_callback_work_ns=max_callback_ns.load();
        m.invalid_blocks+=rejected.load();m.fault=fault.load();
        auto in=input.metrics(),out=output.metrics();
        m.input_overflow_blocks=in.overflow_blocks;m.output_overflow_blocks=out.overflow_blocks;
        m.output_missing_frames=out.missing_frames;
        m.stale_frames=in.stale_frames+out.stale_frames;
        m.discontinuities=discontinuities.load();
        return m;
    }
private:
    FrameRing<8192> input;
    FrameRing<4096> output;
    Processor processor;
    std::array<float,960> input_block{},output_block{};
    bool configured=false,monitor=false;
    uint32_t rate=48000,hop=480;
    uint64_t source_frame=0; // callback-owned
    std::atomic<bool> active{false};
    std::atomic<uint32_t> fault{0},max_frames{0};
    std::atomic<uint64_t> callbacks{0},frames{0},max_callback_ns{0},rejected{0},discontinuities{0};
};

// Lifecycle methods run on one control thread; stop/join never run in a callback.
class AudioWorker final {
public:
    PipelineCore core;
    ~AudioWorker(){stop();}
    bool start() noexcept {
        if(used)return false;
        used=true;running.store(true,std::memory_order_release);
        try {
            thread=std::thread([this] {
                while(running.load(std::memory_order_acquire) && !core.failed()) {
                    if(!core.work_once(monotonic_ns()))
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
        } catch(...) {running.store(false);core.silence();return false;}
        return true;
    }
    void stop() noexcept {
        core.silence();running.store(false,std::memory_order_release);
        if(thread.joinable())thread.join();
    }
private:
    std::atomic<bool> running{false};
    bool used=false;
    std::thread thread;
};
}
