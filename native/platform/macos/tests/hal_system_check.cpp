// OS integration checker. Opens ONLY the silent prototype's exact device UID.
// Never selects a default/physical microphone and never stores PCM.
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <cstdlib>

struct Counters {
    std::atomic<uint64_t> callbacks{0},frames{0},nonzero{0},invalid{0};
};
static_assert(std::atomic<uint64_t>::is_always_lock_free,"lock-free counters required");
OSStatus capture(AudioObjectID,const AudioTimeStamp *,const AudioBufferList *input,
                 const AudioTimeStamp *,AudioBufferList *,const AudioTimeStamp *,void *data){
    auto &c=*static_cast<Counters *>(data);c.callbacks.fetch_add(1);
    if(!input || input->mNumberBuffers>64){c.invalid.fetch_add(1);return noErr;}
    for(UInt32 b=0;b<input->mNumberBuffers;++b){
        const auto &buffer=input->mBuffers[b];
        if(buffer.mNumberChannels!=1 || buffer.mDataByteSize%sizeof(float) ||
           (buffer.mDataByteSize && !buffer.mData)) {c.invalid.fetch_add(1);continue;}
        const auto *bytes=static_cast<const unsigned char *>(buffer.mData);
        uint64_t nonzero=0;
        for(UInt32 offset=0;offset<buffer.mDataByteSize;offset+=4){
            uint32_t bits;std::memcpy(&bits,bytes+offset,4);if(bits!=0)++nonzero;
        }
        c.frames.fetch_add(buffer.mDataByteSize/4);c.nonzero.fetch_add(nonzero);
    }
    return noErr;
}
int main(int argc,char **argv){
    unsigned seconds=0;
    if(argc!=1){
        if(argc!=3 || std::strcmp(argv[1],"--capture-seconds")){std::cerr<<"Usage: hal-system-check [--capture-seconds 1..30]\n";return 1;}
        char *end=nullptr;auto parsed=std::strtoul(argv[2],&end,10);
        if(!end || *end || parsed<1 || parsed>30){std::cerr<<"Invalid duration\n";return 1;}
        seconds=static_cast<unsigned>(parsed);
    }
    AudioObjectPropertyAddress a={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    UInt32 bytes=0;
    auto status=AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,nullptr,&bytes);
    if(status || bytes%sizeof(AudioDeviceID)){std::cerr<<"Device enumeration failed: "<<status<<'\n';return 1;}
    std::vector<AudioDeviceID> devices(bytes/sizeof(AudioDeviceID));
    if(bytes && (status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,nullptr,&bytes,devices.data()))){std::cerr<<"Device list failed: "<<status<<'\n';return 1;}
    AudioDeviceID selected=kAudioObjectUnknown;
    for(auto id:devices){
        auto uidAddress=AudioObjectPropertyAddress{kAudioDevicePropertyDeviceUID,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        CFStringRef uid=nullptr;UInt32 size=sizeof(uid);
        if(AudioObjectGetPropertyData(id,&uidAddress,0,nullptr,&size,&uid)==noErr && uid){
            if(CFEqual(uid,CFSTR("local.voxmask.prototype.silent.device")))selected=id;
            CFRelease(uid);
        }
    }
    if(selected==kAudioObjectUnknown){std::cout<<"{\"installed_device_visible\":false}\n";return 2;}
    auto streams=AudioObjectPropertyAddress{kAudioDevicePropertyStreams,kAudioObjectPropertyScopeInput,kAudioObjectPropertyElementMain};
    AudioStreamID stream=0;bytes=sizeof(stream);
    status=AudioObjectGetPropertyData(selected,&streams,0,nullptr,&bytes,&stream);
    if(status || bytes!=sizeof(stream)){std::cerr<<"Expected one input stream\n";return 1;}
    auto fa=AudioObjectPropertyAddress{kAudioStreamPropertyVirtualFormat,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    AudioStreamBasicDescription format{};bytes=sizeof(format);
    status=AudioObjectGetPropertyData(stream,&fa,0,nullptr,&bytes,&format);
    if(status || format.mSampleRate!=48000 || format.mChannelsPerFrame!=1 ||
       format.mFormatID!=kAudioFormatLinearPCM || format.mFormatFlags!=kAudioFormatFlagsNativeFloatPacked ||
       format.mBytesPerFrame!=4){std::cerr<<"Unexpected prototype input format\n";return 1;}
    if(!seconds){std::cout<<"{\"installed_device_visible\":true,\"sample_rate\":48000,\"channels\":1,\"capture_started\":false}\n";return 0;}
    Counters counters;AudioDeviceIOProcID proc=nullptr;
    status=AudioDeviceCreateIOProcID(selected,capture,&counters,&proc);
    if(status){std::cerr<<"Create capture client failed: "<<status<<'\n';return 1;}
    status=AudioDeviceStart(selected,proc);
    if(status){AudioDeviceDestroyIOProcID(selected,proc);std::cerr<<"Start capture failed: "<<status<<'\n';return 1;}
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    auto stopped=AudioDeviceStop(selected,proc);
    auto destroyed=AudioDeviceDestroyIOProcID(selected,proc);
    const bool passed=!stopped && !destroyed && counters.frames>0 && !counters.nonzero && !counters.invalid;
    std::cout<<"{\"installed_device_visible\":true,\"passed\":"<<(passed?"true":"false")
        <<",\"callbacks\":"<<counters.callbacks.load()<<",\"frames\":"<<counters.frames.load()
        <<",\"nonzero_samples\":"<<counters.nonzero.load()<<",\"invalid_buffers\":"<<counters.invalid.load()<<"}\n";
    return passed?0:1;
}
