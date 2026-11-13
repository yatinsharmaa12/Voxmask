#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/AudioHardware.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <mach/mach_time.h>
#include <dlfcn.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstring>
#include <array>
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>
#include "../../../audio/producer_protocol.hpp"

std::atomic<unsigned> notifications{0};
OSStatus changed(AudioServerPlugInHostRef,AudioObjectID,UInt32,const AudioObjectPropertyAddress *){notifications.fetch_add(1);return noErr;}
AudioObjectPropertyAddress address(AudioObjectPropertySelector s,AudioObjectPropertyScope scope=kAudioObjectPropertyScopeGlobal){return {s,scope,kAudioObjectPropertyElementMain};}
template<class T>T read(AudioServerPlugInDriverRef d,AudioObjectID id,AudioObjectPropertySelector s,AudioObjectPropertyScope scope=kAudioObjectPropertyScopeGlobal){
    auto a=address(s,scope);UInt32 size=0;T value{};
    assert((*d)->HasProperty(d,id,0,&a));
    assert((*d)->GetPropertyDataSize(d,id,0,&a,0,nullptr,&size)==noErr && size==sizeof(T));
    assert((*d)->GetPropertyData(d,id,0,&a,0,nullptr,sizeof(value),&size,&value)==noErr);
    assert(size==sizeof(T));return value;
}
int main(int argc,char **argv){
    assert(argc==2);void *bundle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!bundle){std::cerr<<dlerror()<<'\n';return 1;}
    using Factory=void *(*)(CFAllocatorRef,CFUUIDRef);
    auto factory=reinterpret_cast<Factory>(dlsym(bundle,"VoxMaskFactory"));assert(factory);
    assert(factory(nullptr,nullptr)==nullptr);
    auto d=static_cast<AudioServerPlugInDriverRef>(factory(nullptr,kAudioServerPlugInTypeUUID));assert(d);
    void *queried=nullptr;
    assert((*d)->QueryInterface(d,CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID),&queried)==S_OK && queried==d);
    (*d)->Release(d);
    AudioServerPlugInHostInterface host{};host.PropertiesChanged=changed;
    assert((*d)->Initialize(d,&host)==noErr);
    auto device=read<AudioObjectID>(d,kAudioObjectPlugInObject,kAudioPlugInPropertyDeviceList);
    auto stream=read<AudioObjectID>(d,device,kAudioDevicePropertyStreams,kAudioObjectPropertyScopeInput);
    auto name=read<CFStringRef>(d,device,kAudioObjectPropertyName);
    assert(CFEqual(name,CFSTR("VoxMask Prototype (Silent)")));CFRelease(name);
    assert(read<Float64>(d,device,kAudioDevicePropertyNominalSampleRate)==48000);
    assert(read<UInt32>(d,device,kAudioDevicePropertyDeviceCanBeDefaultDevice,kAudioObjectPropertyScopeInput)==0);
    assert(read<UInt32>(d,stream,kAudioStreamPropertyDirection)==1);
    auto format=read<AudioStreamBasicDescription>(d,stream,kAudioStreamPropertyPhysicalFormat);
    assert(format.mChannelsPerFrame==1 && format.mSampleRate==48000 && format.mBitsPerChannel==32);
    auto outputs=address(kAudioDevicePropertyStreams,kAudioObjectPropertyScopeOutput);UInt32 size=99;
    assert((*d)->GetPropertyDataSize(d,device,0,&outputs,0,nullptr,&size)==noErr && size==0);
    auto nominal=address(kAudioDevicePropertyNominalSampleRate);
    UInt64 guard=0x1122334455667788ULL;
    assert((*d)->GetPropertyData(d,device,0,&nominal,0,nullptr,1,&size,&guard)==kAudioHardwareBadPropertySizeError);
    assert(guard==0x1122334455667788ULL);
    Float64 unsupported=44100,supported=48000;
    assert((*d)->SetPropertyData(d,device,0,&nominal,0,nullptr,sizeof(unsupported),&unsupported)==kAudioDeviceUnsupportedFormatError);
    assert((*d)->SetPropertyData(d,device,0,&nominal,0,nullptr,sizeof(supported),&supported)==noErr);
    auto unknown=address('nope');assert(!(*d)->HasProperty(d,device,0,&unknown));
    assert(!(*d)->HasProperty(d,9999,0,&nominal));
    auto translate=address(kAudioPlugInPropertyTranslateUIDToDevice);AudioObjectID translated=0;
    CFStringRef uid=CFSTR("local.voxmask.prototype.silent.device");
    assert((*d)->GetPropertyData(d,kAudioObjectPlugInObject,0,&translate,sizeof(uid),&uid,sizeof(translated),&size,&translated)==noErr && translated==device);

    AudioServerPlugInClientInfo a{10,0,true,nullptr},b{20,0,true,nullptr};
    assert((*d)->AddDeviceClient(d,device,&a)==noErr && (*d)->AddDeviceClient(d,device,&b)==noErr);
    assert((*d)->StartIO(d,device,a.mClientID)==noErr);
    assert((*d)->StartIO(d,device,a.mClientID)==noErr); // duplicate start is idempotent
    assert((*d)->StartIO(d,device,b.mClientID)==noErr);
    assert((*d)->StopIO(d,device,a.mClientID)==noErr);
    assert(read<UInt32>(d,device,kAudioDevicePropertyDeviceIsRunning)==1);
    assert((*d)->RemoveDeviceClient(d,device,&b)==noErr);
    assert(read<UInt32>(d,device,kAudioDevicePropertyDeviceIsRunning)==0 && notifications==2);
    assert((*d)->RemoveDeviceClient(d,device,&a)==noErr);
    assert((*d)->StartIO(d,device,999)==kAudioHardwareIllegalOperationError);

    Boolean will=false,inPlace=false;
    assert((*d)->WillDoIOOperation(d,device,10,kAudioServerPlugInIOOperationReadInput,&will,&inPlace)==noErr && will && inPlace);
    assert((*d)->WillDoIOOperation(d,device,10,kAudioServerPlugInIOOperationWriteMix,&will,&inPlace)==noErr && !will);
    AudioServerPlugInIOCycleInfo cycle{};
    // Independent clients and all lifecycle states get the same deterministic silence.
    auto silence=[&](UInt32 client){
        std::array<float,1026> samples;
        for(unsigned n=0;n<1000;++n){
            samples.fill(0.75f);
            assert((*d)->DoIOOperation(d,device,stream,client,kAudioServerPlugInIOOperationReadInput,1024,&cycle,samples.data()+1,nullptr)==noErr);
            assert(samples.front()==.75f && samples.back()==.75f);
            for(unsigned i=1;i<=1024;++i)assert(samples[i]==0);
        }
    };
    std::thread first([&]{silence(10);}),second([&]{silence(20);});first.join();second.join();
    using Publish=void (*)(const voxmask::ProducerBlock *,uint64_t);
    using Close=void (*)(uint64_t);
    auto publish=reinterpret_cast<Publish>(dlsym(bundle,"VoxMaskTestPublish"));
    auto close=reinterpret_cast<Close>(dlsym(bundle,"VoxMaskTestClose"));
    assert(publish && close);
    voxmask::ProducerBlock block{};
    block.epoch=7;block.frames=8;block.capture_time_ns=clock_gettime_nsec_np(CLOCK_MONOTONIC);
    for(unsigned i=0;i<block.frames;++i)block.samples[i]=float(i+1)/16;
    publish(&block,4096);
    cycle.mInputTime.mFlags=kAudioTimeStampSampleTimeValid;
    cycle.mInputTime.mSampleTime=4096;
    for(UInt32 client:{10u,20u}){
        std::array<float,16> pcm{};
        assert((*d)->DoIOOperation(d,device,stream,client,kAudioServerPlugInIOOperationReadInput,16,&cycle,pcm.data(),nullptr)==noErr);
        for(unsigned i=0;i<8;++i)assert(pcm[i]==block.samples[i]);
        for(unsigned i=8;i<16;++i)assert(pcm[i]==0);
    }
    close(block.epoch);
    std::array<float,16> closed{};closed.fill(1);
    assert((*d)->DoIOOperation(d,device,stream,10,kAudioServerPlugInIOOperationReadInput,16,&cycle,closed.data(),nullptr)==noErr);
    for(float sample:closed)assert(sample==0);
    assert((*d)->DoIOOperation(d,device,stream,10,kAudioServerPlugInIOOperationReadInput,1,&cycle,nullptr,nullptr)==kAudioHardwareIllegalOperationError);
    assert((*d)->DoIOOperation(d,device,stream+50,10,kAudioServerPlugInIOOperationReadInput,0,&cycle,nullptr,nullptr)==kAudioHardwareBadObjectError);
    assert((*d)->DoIOOperation(d,device,stream,10,kAudioServerPlugInIOOperationReadInput,0,&cycle,nullptr,nullptr)==noErr);

    Float64 s1=0,s2=0;UInt64 h1=0,h2=0,seed1=0,seed2=0;
    assert((*d)->GetZeroTimeStamp(d,device,10,&s1,&h1,&seed1)==noErr);
    std::this_thread::sleep_for(std::chrono::milliseconds(360));
    assert((*d)->GetZeroTimeStamp(d,device,20,&s2,&h2,&seed2)==noErr);
    auto period=read<UInt32>(d,device,kAudioDevicePropertyZeroTimeStampPeriod);
    assert(period>=10923 && s2>s1 && h2>h1 && h2<=mach_absolute_time() && seed1==seed2);
    assert(std::fmod(s2-s1,period)==0);
    mach_timebase_info_data_t tb{};mach_timebase_info(&tb);
    const long double expected=(s2-s1)*1000000000.0L/48000;
    const long double actual=static_cast<long double>(h2-h1)*tb.numer/tb.denom;
    assert(std::fabs(expected-actual)<1000); // tick rounding tolerance, not latency
    std::cout<<"HAL mock host: factory, properties, multi-client mailbox, silence and clock mapping passed\n";
    dlclose(bundle);
}
