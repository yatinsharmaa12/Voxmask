#pragma once
#include "voxmask_audio.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace voxmask {
static_assert(std::atomic<uint64_t>::is_always_lock_free, "64-bit lock-free atomics required");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "32-bit lock-free atomics required");
class Processor final {
 public:
 // Set before callbacks start. One callback writer; snapshots may run concurrently.
 bool monitor = false;
 std::atomic<uint32_t> fault{0};
 void process(float *out, size_t out_count, const float *in,
              size_t in_count, uint32_t count) noexcept {
  const auto start = std::chrono::steady_clock::now();
  if (out) std::fill_n(out, out_count, 0.0f);
  callbacks.fetch_add(1, std::memory_order_relaxed);
  frames.fetch_add(count, std::memory_order_relaxed);
  if (count > max_frames.load(std::memory_order_relaxed)) max_frames.store(count, std::memory_order_relaxed);
  bool valid = count > 0 && count <= VM_AUDIO_MAX_FRAMES && in && in_count >= count
             && (!out || out_count >= count) && !fault.load(std::memory_order_acquire);
  double sum = 0;
  if (valid) {
   for (uint32_t i=0; i<count; ++i) {
    const double x=in[i];
    if (!std::isfinite(x)) { valid=false; break; }
    sum += x*x;
   }
  }
  double db=-120;
  if (valid) {
   if (sum>0) db=std::max(-120.0,10*std::log10(sum/count));
   if (monitor && out) {
    for (uint32_t i=0;i<count;++i) out[i]=std::clamp(in[i]*0.25f,-1.0f,1.0f);
    // A concurrently reported device fault must silence this completed block.
    if (fault.load(std::memory_order_acquire)) std::fill_n(out,out_count,0.0f);
   }
  } else invalid.fetch_add(1,std::memory_order_relaxed);
  uint64_t bits; std::memcpy(&bits,&db,sizeof(bits));
  rms_bits.store(bits,std::memory_order_relaxed);
  auto elapsed=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-start).count());
  if(elapsed>max_ns.load(std::memory_order_relaxed)) max_ns.store(elapsed,std::memory_order_relaxed);
 }
 vm_audio_metrics snapshot() const noexcept {
  vm_audio_metrics m{};
  m.callbacks=callbacks.load(std::memory_order_relaxed);
  m.frames=frames.load(std::memory_order_relaxed);
  m.invalid_blocks=invalid.load(std::memory_order_relaxed);
  m.max_callback_frames=max_frames.load(std::memory_order_relaxed);
  m.max_callback_work_ns=max_ns.load(std::memory_order_relaxed);
  m.fault=fault.load(std::memory_order_acquire);
  uint64_t bits=rms_bits.load(std::memory_order_relaxed);
  std::memcpy(&m.input_rms_db,&bits,sizeof(bits));
  if(!m.callbacks) m.input_rms_db=-120;
  return m;
 }
 private:
 std::atomic<uint64_t> callbacks{0},frames{0},invalid{0},max_ns{0},rms_bits{UINT64_C(0xc05e000000000000)};
 std::atomic<uint32_t> max_frames{0};
};
}
