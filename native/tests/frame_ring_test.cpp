#include "../audio/frame_ring.hpp"
#include "../audio/buffer_check.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <thread>
#include <limits>
#include <deque>
#include <random>
#include <iostream>

int main() {
    using voxmask::FrameRing;
    float input[8]={1,2,3,4,5,6,7,8},output[8];
    constexpr uint64_t start=1000000000;
    FrameRing<8> ring;
    auto r=ring.read(output,8,start,1,1000000000);
    assert(r.frames==0 && r.discontinuity);
    for(float v:output)assert(v==0);
    assert(ring.push(input,8,start,0,1,1000));
    assert(!ring.push(input,1,start,8,1,1000));
    r=ring.read(output,3,start+8000000,1,1000000000);
    assert(r.frames==3 && output[0]==1 && output[2]==3);
    assert(ring.push(input,3,start+8000000,8,1,1000)); // storage wraps
    r=ring.read(output,8,start+11000000,1,1000000000);
    assert(r.frames==8 && !r.discontinuity);
    assert(output[0]==4 && output[4]==8 && output[5]==1);

    // Age cutoff is inclusive; future timestamps and old epochs are rejected.
    FrameRing<8> age;
    assert(age.push(input,1,start,0,1,1000));
    assert(age.read(output,1,start+250000000,1,250000000).frames==1);
    assert(age.push(input,1,start,1,1,1000));
    assert(age.read(output,1,start+250000001,1,250000000).frames==0);
    assert(age.push(input,1,start+1,2,1,1000));
    assert(age.read(output,1,start,1,250000000).frames==0);
    assert(age.push(input,1,start,3,1,1000));
    assert(age.read(output,1,start,2,250000000).frames==0);
    assert(age.push(input,1,start,10,2,1000));
    r=age.read(output,1,start,2,250000000);
    assert(r.frames==1 && r.discontinuity && r.first_source_frame==10);
    assert(age.metrics().stale_frames==1 && age.metrics().future_frames==1 && age.metrics().epoch_frames==1);

    FrameRing<8> invalid;
    float bad[]={1,std::numeric_limits<float>::quiet_NaN()};
    assert(!invalid.push(bad,2,start,0,1,48000));
    assert(!invalid.push(input,1,start,0,0,48000));
    assert(!invalid.push(input,1,start,0,1,0));
    assert(!invalid.push(input,8,UINT64_MAX,0,1,1000));
    assert(!invalid.push(input,1,start,UINT64_MAX,1,1000));
    assert(!invalid.push(nullptr,1,start,0,1,48000));
    assert(invalid.read(output,1,start,1,1000000000).frames==0);

    // Differential FIFO test exercises variable reads/writes and many wraps.
    FrameRing<8> model_ring;
    std::deque<float> model;
    std::mt19937 random(73);
    uint64_t position=0;
    for(int i=0;i<100000;++i) {
        uint32_t count=1+random()%8;
        if(random()%2) {
            bool expected=model.size()+count<=8;
            bool accepted=model_ring.push(input,count,start,position,1,48000);
            assert(accepted==expected);
            if(accepted)for(uint32_t n=0;n<count;++n)model.push_back(input[n]);
            position+=count;
        } else {
            auto result=model_ring.read(output,count,start+1000000,1,1000000000);
            auto expected=std::min<size_t>(count,model.size());
            assert(result.frames==expected);
            for(uint32_t n=0;n<count;++n) {
                if(n<expected){assert(output[n]==model.front());model.pop_front();}
                else assert(output[n]==0);
            }
        }
    }
    // Genuine SPSC concurrency, exact sequence checked for 200,000 frames.
    FrameRing<256> concurrent;
    std::thread producer([&]{
        for(uint64_t i=0;i<200000;++i){
            float v=static_cast<float>(i);
            while(!concurrent.push(&v,1,start,i,1,48000))std::this_thread::yield();
        }
    });
    uint64_t next=0;
    while(next<200000) {
        auto result=concurrent.read(output,8,start,1,1000000000);
        for(uint32_t n=0;n<result.frames;++n)assert(output[n]==static_cast<float>(next++));
        if(!result.frames)std::this_thread::yield();
    }
    producer.join();
    assert(concurrent.metrics().read_frames==200000);
    vm_buffer_report report{};report.struct_size=sizeof(report);
    assert(vm_buffer_check(VM_BUFFER_CHECK_ABI,&report)==0 && report.passed);
    assert(vm_buffer_check(VM_BUFFER_CHECK_ABI+1,&report)!=0);
    std::cout<<"frame ring: bounds, wrap, ordering, epochs, age, differential FIFO, SPSC concurrency and overload simulation passed\n";
}
