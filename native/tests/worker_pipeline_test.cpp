#include "../audio/worker_pipeline.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <iostream>
#include <limits>
using voxmask::PipelineCore;
constexpr uint64_t base=1000000000,step=10000000;
void silent(const float *out,uint32_t n){for(uint32_t i=0;i<n;++i)assert(out[i]==0);}
int main() {
    float input[960],output[960];std::fill_n(input,960,0.5f);
    for(uint32_t rate:{44100,48000,96000}) {
        auto p=std::make_unique<PipelineCore>();assert(p->configure(rate,true));
        const uint32_t hop=rate/100;
        p->callback(output,input,hop,base);silent(output,hop); // startup underrun
        assert(p->work_once(base));
        p->callback(output,input,hop,base+step);
        for(uint32_t i=0;i<hop;++i)assert(output[i]==0.125f);
        assert(!p->failed());
        assert(p->snapshot().processed_frames==hop);
        p->silence();p->callback(output,input,hop,base+2*step);silent(output,hop);
    }
    auto probe=std::make_unique<PipelineCore>();assert(probe->configure(48000,false));
    probe->callback(output,input,480,base);assert(probe->work_once(base));
    probe->callback(output,input,480,base+step);silent(output,480);

    auto stale=std::make_unique<PipelineCore>();stale->configure(48000,true);
    stale->callback(output,input,480,base);assert(stale->work_once(base));
    stale->callback(output,input,480,base+500000000);silent(output,480);
    assert(stale->snapshot().stale_frames==480);

    auto gap=std::make_unique<PipelineCore>();gap->configure(48000,true);
    gap->callback(output,input,480,base);
    assert(gap->work_once(base+500000000)); // expires whole chunk; no processing
    assert(gap->snapshot().discontinuities==1 && gap->snapshot().processed_frames==0);
    gap->callback(output,input,480,base+600000000);
    assert(gap->work_once(base+600000000));
    gap->callback(output,input,480,base+610000000);
    for(int i=0;i<480;++i)assert(output[i]==0.125f);

    auto overload=std::make_unique<PipelineCore>();overload->configure(48000,true);
    for(uint64_t i=0;i<18;++i)overload->callback(output,input,480,base+i*step);
    assert(overload->failed() && overload->snapshot().input_overflow_blocks==1);
    silent(output,480);

    auto output_full=std::make_unique<PipelineCore>();output_full->configure(48000,true);
    for(uint64_t i=0;i<9;++i){output_full->callback(nullptr,input,480,base+i*step);output_full->work_once(base+i*step);}
    assert(output_full->failed() && output_full->snapshot().output_overflow_blocks==1);

    auto bad=std::make_unique<PipelineCore>();bad->configure(48000,true);
    input[0]=std::numeric_limits<float>::quiet_NaN();
    bad->callback(output,input,480,base);silent(output,480);assert(bad->failed());input[0]=.5f;

    // Real worker lifecycle, concurrent callback + worker + metric reader.
    for(int cycle=0;cycle<10;++cycle) {
        auto w=std::make_unique<voxmask::AudioWorker>();assert(w->core.configure(48000,true));assert(w->start());
        std::atomic<bool> done{false};
        std::thread reader([&]{while(!done.load()){auto m=w->core.snapshot();assert(std::isfinite(m.input_rms_db));std::this_thread::yield();}});
        w->core.callback(output,input,480,voxmask::monotonic_ns());
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(w->core.ready_output_frames()<480 && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        assert(w->core.ready_output_frames()==480 && w->core.snapshot().processed_frames==480);
        w->core.callback(output,input,480,voxmask::monotonic_ns());
        for(int i=0;i<480;++i)assert(output[i]==.125f);
        w->stop();w->stop();assert(!w->start());
        w->core.callback(output,input,480,voxmask::monotonic_ns());silent(output,480);
        done.store(true);reader.join();
    }
    std::cout<<"worker pipeline: startup, gain, probe silence, expiry, discontinuity, overload, fault and concurrent lifecycle passed\n";
}
