#include "buffer_check.h"
#include "frame_ring.hpp"
#include <memory>
#include <new>

extern "C" int32_t vm_buffer_check(uint32_t abi,vm_buffer_report *out) {
    if(!out || out->struct_size!=sizeof(*out) || abi!=VM_BUFFER_CHECK_ABI)return -1;
    *out={};out->struct_size=sizeof(*out);
    using Ring=voxmask::FrameRing<8192>;
    auto ring=std::unique_ptr<Ring>(new(std::nothrow) Ring);
    if(!ring)return -2;
    constexpr uint32_t rate=48000, hop=480;
    constexpr uint64_t limit=250000000, step=10000000, base=1000000000;
    float input[hop],output[hop];
    for(auto &x:input)x=0.25f;
    bool ok=true;
    // 30 simulated seconds of 10 ms callbacks; no elapsed-time performance claim.
    for(uint64_t b=0;b<3000;++b) {
        ok &= ring->push(input,hop,base+b*step,b*hop,1,rate);
        auto r=ring->read(output,hop,base+(b+1)*step,1,limit);
        ok &= r.frames==hop && !r.discontinuity && r.max_age_ns<=step;
        for(float v:output)ok &= v==0.25f;
    }
    out->simulated_frames=3000*hop;
    // Stall consumer: 17 blocks fit (8160 frames); 18th is dropped atomically.
    const uint64_t start=base+3000*step;
    for(uint64_t i=0;i<18;++i) {
        bool accepted=ring->push(input,hop,start+i*step,(3000+i)*hop,1,rate);
        ok &= accepted==(i<17);
    }
    auto r=ring->read(output,hop,start+500000000,1,limit);
    ok &= r.frames==0 && r.discarded==8160;
    for(float v:output)ok &= v==0;
    auto m=ring->metrics();
    out->capacity_frames=Ring::capacity;out->sample_rate=rate;
    out->storage_bytes=Ring::storage_bytes;out->overflow_blocks=m.overflow_blocks;
    out->stale_frames=m.stale_frames;out->missing_frames=m.missing_frames;
    out->passed=ok && m.overflow_blocks==1 && m.stale_frames==8160 && m.missing_frames==hop;
    return out->passed?0:-3;
}
