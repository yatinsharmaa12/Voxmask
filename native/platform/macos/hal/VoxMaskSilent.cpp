// Development-only silent AudioServerPlugIn. No physical input, IPC or inference.
#include <CoreAudio/AudioServerPlugIn.h>
#include <CoreAudio/AudioHardware.h>
#include <CoreFoundation/CFPlugInCOM.h>
#include <mach/mach_time.h>
#include <atomic>
#include <array>
#include <mutex>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <time.h>
#include "../../../audio/producer_mailbox.hpp"

namespace {
constexpr AudioObjectID plugin=kAudioObjectPlugInObject, device=2, stream=3;
constexpr Float64 rate=48000;
constexpr UInt32 period=16384; // SDK minimum zero timestamp period is 10923.
constexpr UInt32 maxIOFrames=65536;
AudioServerPlugInDriverInterface interface{};
AudioServerPlugInDriverInterface *interfacePointer=&interface;
const AudioServerPlugInDriverRef driver=&interfacePointer;
std::atomic<UInt32> references{1},running{0};
voxmask::ProducerMailbox producerMailbox;
static_assert(std::atomic<UInt32>::is_always_lock_free,"lock-free state required");
AudioServerPlugInHostRef host=nullptr;
UInt64 anchor=0;
long double ticksPerFrame=0;
struct Client { UInt32 id=0; bool occupied=false,started=false; };
std::array<Client,64> clients{};
std::mutex clientsMutex; // control functions only, never used by timestamp/IO callbacks
bool valid(AudioObjectID id){return id==plugin || id==device || id==stream;}
AudioStreamBasicDescription format(){
    return {rate,kAudioFormatLinearPCM,kAudioFormatFlagsNativeFloatPacked,4,1,4,1,32,0};
}
void notifyRunning(){
    if(host && host->PropertiesChanged){
        const AudioObjectPropertyAddress a={kAudioDevicePropertyDeviceIsRunning,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        host->PropertiesChanged(host,device,1,&a);
    }
}

template<class T> OSStatus scalar(const T &value,UInt32 cap,UInt32 *written,void *out){
    *written=sizeof(T);
    if(!out)return noErr; // internal size-only query
    if(cap<sizeof(T))return kAudioHardwareBadPropertySizeError;
    std::memcpy(out,&value,sizeof(T));return noErr;
}
OSStatus list(AudioObjectID value,bool present,UInt32 cap,UInt32 *written,void *out){
    *written=present?sizeof(value):0;
    if(!out || !present)return noErr;
    // HAL permits partial array reads; a sub-element buffer returns no items.
    if(cap<sizeof(value)){*written=0;return noErr;}
    std::memcpy(out,&value,sizeof(value));return noErr;
}
OSStatus stringValue(CFStringRef value,UInt32 cap,UInt32 *written,void *out){
    if(out && cap>=sizeof(value))CFRetain(value);
    return scalar(value,cap,written,out);
}

// One dispatcher keeps HasProperty, size and value behavior consistent.
OSStatus property(AudioObjectID id,const AudioObjectPropertyAddress *a,UInt32 qs,
                  const void *q,UInt32 cap,UInt32 *written,void *out){
    if(!valid(id))return kAudioHardwareBadObjectError;
    if(!a || !written)return kAudioHardwareIllegalOperationError;
    *written=0;
    if(a->mElement!=kAudioObjectPropertyElementMain)return kAudioHardwareUnknownPropertyError;
    const auto s=a->mSelector;
    const bool global=a->mScope==kAudioObjectPropertyScopeGlobal;
    const bool input=a->mScope==kAudioObjectPropertyScopeInput;
    const bool output=a->mScope==kAudioObjectPropertyScopeOutput;
    if(!global && !(id==device && (input||output)))return kAudioHardwareUnknownPropertyError;
    if(global){
        switch(s){
        case kAudioObjectPropertyBaseClass: return scalar(UInt32(kAudioObjectClassID),cap,written,out);
        case kAudioObjectPropertyClass: return scalar(UInt32(id==plugin?kAudioPlugInClassID:id==device?kAudioDeviceClassID:kAudioStreamClassID),cap,written,out);
        case kAudioObjectPropertyOwner: return scalar(UInt32(id==plugin?kAudioObjectUnknown:id==device?plugin:device),cap,written,out);
        case kAudioObjectPropertyName: return stringValue(id==plugin?CFSTR("VoxMask Prototype Plug-in"):id==device?CFSTR("VoxMask Prototype (Silent)"):CFSTR("VoxMask Silent Input"),cap,written,out);
        case kAudioObjectPropertyManufacturer: return stringValue(CFSTR("VoxMask"),cap,written,out);
        case kAudioObjectPropertyOwnedObjects: {
            bool present=id!=stream;
            if(qs){
                if(qs%sizeof(AudioClassID) || !q)return kAudioHardwareBadPropertySizeError;
                const AudioClassID childClass=id==plugin?kAudioDeviceClassID:kAudioStreamClassID;
                present=false;
                for(UInt32 offset=0;offset<qs;offset+=sizeof(AudioClassID)){
                    AudioClassID wanted;std::memcpy(&wanted,static_cast<const char *>(q)+offset,sizeof(wanted));
                    if(id!=stream && (wanted==childClass || wanted==kAudioObjectClassID))present=true;
                }
            }
            return list(id==plugin?device:stream,present,cap,written,out);
        }
        default: break;
        }
    }
    if(id==plugin && global){
        switch(s){
        case kAudioPlugInPropertyDeviceList:return list(device,true,cap,written,out);
        case kAudioPlugInPropertyResourceBundle:return stringValue(CFSTR(""),cap,written,out);
        case kAudioPlugInPropertyTranslateUIDToDevice: {
            if(!out){*written=sizeof(AudioObjectID);return noErr;}
            if(qs!=sizeof(CFStringRef) || !q)return kAudioHardwareBadPropertySizeError;
            CFStringRef uid;std::memcpy(&uid,q,sizeof(uid));
            AudioObjectID result=uid && CFEqual(uid,CFSTR("local.voxmask.prototype.silent.device"))?device:kAudioObjectUnknown;
            return scalar(result,cap,written,out);
        }
        default:break;
        }
    }
    if(id==device){
        switch(s){
        case kAudioDevicePropertyStreams:return list(stream,!output,cap,written,out);
        case kAudioObjectPropertyControlList:*written=0;return noErr;
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset:return scalar(UInt32(0),cap,written,out);
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:return scalar(UInt32(0),cap,written,out);
        case kAudioDevicePropertyStreamConfiguration: {
            const UInt32 size=offsetof(AudioBufferList,mBuffers)+(output?0:sizeof(AudioBuffer));
            *written=size;if(!out)return noErr;if(cap<size)return kAudioHardwareBadPropertySizeError;
            AudioBufferList value{};value.mNumberBuffers=output?0:1;value.mBuffers[0]={1,0,nullptr};
            std::memcpy(out,&value,size);return noErr;
        }
        default:break;
        }
        if(global)switch(s){
        case kAudioDevicePropertyDeviceUID:return stringValue(CFSTR("local.voxmask.prototype.silent.device"),cap,written,out);
        case kAudioDevicePropertyModelUID:return stringValue(CFSTR("local.voxmask.prototype.silent.model"),cap,written,out);
        case kAudioDevicePropertyTransportType:return scalar(UInt32(kAudioDeviceTransportTypeVirtual),cap,written,out);
        case kAudioDevicePropertyRelatedDevices:return list(device,true,cap,written,out);
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyIsHidden:return scalar(UInt32(0),cap,written,out);
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyClockIsStable:return scalar(UInt32(1),cap,written,out);
        case kAudioDevicePropertyDeviceIsRunning:return scalar(UInt32(running.load(std::memory_order_relaxed)!=0),cap,written,out);
        case kAudioDevicePropertyNominalSampleRate:return scalar(rate,cap,written,out);
        case kAudioDevicePropertyAvailableNominalSampleRates:return scalar(AudioValueRange{rate,rate},cap,written,out);
        case kAudioDevicePropertyZeroTimeStampPeriod:return scalar(period,cap,written,out);
        case kAudioDevicePropertyClockAlgorithm:return scalar(UInt32(kAudioDeviceClockAlgorithmRaw),cap,written,out);
        default:break;
        }
    }
    if(id==stream && global)switch(s){
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyStartingChannel:return scalar(UInt32(1),cap,written,out);
        case kAudioStreamPropertyTerminalType:return scalar(UInt32(kAudioStreamTerminalTypeMicrophone),cap,written,out);
        case kAudioStreamPropertyLatency:return scalar(UInt32(0),cap,written,out);
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:return scalar(format(),cap,written,out);
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:return scalar(AudioStreamRangedDescription{format(),{rate,rate}},cap,written,out);
        default:break;
    }
    return kAudioHardwareUnknownPropertyError;
}

ULONG addRef(void *){return references.fetch_add(1)+1;}
ULONG release(void *){
    auto n=references.load();while(n>1 && !references.compare_exchange_weak(n,n-1)){}
    return n>1?n-1:1; // static singleton remains alive
}
HRESULT query(void *d,REFIID uuid,LPVOID *out){
    if(!out || d!=driver)return E_NOINTERFACE;
    *out=nullptr;auto requested=CFUUIDCreateFromUUIDBytes(nullptr,uuid);
    const bool supported=requested && (CFEqual(requested,IUnknownUUID)||CFEqual(requested,kAudioServerPlugInDriverInterfaceUUID));
    if(requested)CFRelease(requested);
    if(!supported)return E_NOINTERFACE;
    *out=driver;addRef(d);return S_OK;
}
OSStatus initialize(AudioServerPlugInDriverRef d,AudioServerPlugInHostRef h){
    if(d!=driver || !h)return kAudioHardwareIllegalOperationError;
    host=h;mach_timebase_info_data_t tb{};mach_timebase_info(&tb);
    ticksPerFrame=(1000000000.0L/rate)*tb.denom/tb.numer;
    anchor=mach_absolute_time();return noErr;
}
OSStatus createDevice(AudioServerPlugInDriverRef,CFDictionaryRef,const AudioServerPlugInClientInfo *,AudioObjectID *){return kAudioHardwareUnsupportedOperationError;}
OSStatus destroyDevice(AudioServerPlugInDriverRef,AudioObjectID){return kAudioHardwareUnsupportedOperationError;}
OSStatus addClient(AudioServerPlugInDriverRef d,AudioObjectID id,const AudioServerPlugInClientInfo *c){
    if(d!=driver || id!=device)return kAudioHardwareBadObjectError;
    if(!c)return kAudioHardwareIllegalOperationError;
    std::lock_guard<std::mutex> lock(clientsMutex);
    for(auto &slot:clients)if(slot.occupied && slot.id==c->mClientID)return noErr;
    for(auto &slot:clients)if(!slot.occupied){slot={c->mClientID,true,false};return noErr;}
    return kAudioHardwareIllegalOperationError;
}
OSStatus removeClient(AudioServerPlugInDriverRef d,AudioObjectID id,const AudioServerPlugInClientInfo *c){
    if(d!=driver || id!=device)return kAudioHardwareBadObjectError;
    if(!c)return kAudioHardwareIllegalOperationError;
    bool notify=false,found=false;
    {std::lock_guard<std::mutex> lock(clientsMutex);
        for(auto &slot:clients)if(slot.occupied && slot.id==c->mClientID){
            if(slot.started)notify=running.fetch_sub(1)==1;
            slot={};found=true;break;
        }
    }
    if(notify)notifyRunning();return found?noErr:kAudioHardwareIllegalOperationError;
}
OSStatus configChange(AudioServerPlugInDriverRef d,AudioObjectID id,UInt64,void *){
    return d==driver && id==device?kAudioHardwareUnsupportedOperationError:kAudioHardwareBadObjectError;
}
Boolean has(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t,const AudioObjectPropertyAddress *a){
    UInt32 size=0;return d==driver && property(id,a,0,nullptr,0,&size,nullptr)==noErr;
}
bool writable(AudioObjectID id,AudioObjectPropertySelector selector){
    return (id==device && selector==kAudioDevicePropertyNominalSampleRate) ||
        (id==stream && (selector==kAudioStreamPropertyPhysicalFormat || selector==kAudioStreamPropertyVirtualFormat));
}
OSStatus settable(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t pid,const AudioObjectPropertyAddress *a,Boolean *out){
    if(!out)return kAudioHardwareIllegalOperationError;
    if(!has(d,id,pid,a))return valid(id)?kAudioHardwareUnknownPropertyError:kAudioHardwareBadObjectError;
    *out=writable(id,a->mSelector);return noErr;
}
OSStatus sizeOf(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 *size){
    if(d!=driver)return kAudioHardwareBadObjectError;
    return property(id,a,qs,q,0,size,nullptr);
}
OSStatus get(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t,const AudioObjectPropertyAddress *a,UInt32 qs,const void *q,UInt32 cap,UInt32 *written,void *out){
    if(d!=driver)return kAudioHardwareBadObjectError;
    if(!out)return kAudioHardwareIllegalOperationError;
    return property(id,a,qs,q,cap,written,out);
}
OSStatus set(AudioServerPlugInDriverRef d,AudioObjectID id,pid_t pid,const AudioObjectPropertyAddress *a,UInt32,const void *,UInt32 bytes,const void *data){
    if(!has(d,id,pid,a))return kAudioHardwareUnknownPropertyError;
    if(!writable(id,a->mSelector))return kAudioHardwareUnsupportedOperationError;
    if(!data)return kAudioHardwareIllegalOperationError;
    if(id==device){
        if(bytes!=sizeof(Float64))return kAudioHardwareBadPropertySizeError;
        Float64 value;std::memcpy(&value,data,sizeof(value));
        return value==rate?noErr:kAudioDeviceUnsupportedFormatError;
    }
    if(bytes!=sizeof(AudioStreamBasicDescription))return kAudioHardwareBadPropertySizeError;
    AudioStreamBasicDescription f;std::memcpy(&f,data,sizeof(f));const auto expected=format();
    return f.mSampleRate==expected.mSampleRate && f.mFormatID==expected.mFormatID &&
        f.mFormatFlags==expected.mFormatFlags && f.mBytesPerPacket==4 && f.mFramesPerPacket==1 &&
        f.mBytesPerFrame==4 && f.mChannelsPerFrame==1 && f.mBitsPerChannel==32?
        noErr:kAudioDeviceUnsupportedFormatError;
}
OSStatus changeIO(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32 client,bool start){
    if(d!=driver || id!=device)return kAudioHardwareBadObjectError;
    bool found=false,notify=false;
    {std::lock_guard<std::mutex> lock(clientsMutex);
        for(auto &slot:clients)if(slot.occupied && slot.id==client){
            found=true;
            if(slot.started!=start){slot.started=start;notify=start?running.fetch_add(1)==0:running.fetch_sub(1)==1;}
            break;
        }
    }
    if(notify)notifyRunning();return found?noErr:kAudioHardwareIllegalOperationError;
}
OSStatus startIO(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32 c){return changeIO(d,id,c,true);}
OSStatus stopIO(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32 c){return changeIO(d,id,c,false);}
OSStatus timestamp(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32,Float64 *samples,UInt64 *ticks,UInt64 *seed){
    if(d!=driver || id!=device)return kAudioHardwareBadObjectError;
    if(!samples || !ticks || !seed || ticksPerFrame<=0)return kAudioHardwareIllegalOperationError;
    const UInt64 now=mach_absolute_time();
    const long double n=std::floor(static_cast<long double>(now-anchor)/(ticksPerFrame*period));
    *samples=static_cast<Float64>(n*period);
    *ticks=anchor+static_cast<UInt64>(n*period*ticksPerFrame);*seed=1;
    return noErr;
}
OSStatus willIO(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32,UInt32 op,Boolean *will,Boolean *inPlace){
    if(d!=driver || id!=device)return kAudioHardwareBadObjectError;
    if(!will || !inPlace)return kAudioHardwareIllegalOperationError;
    *will=op==kAudioServerPlugInIOOperationReadInput;*inPlace=true;return noErr;
}
OSStatus boundaryIO(AudioServerPlugInDriverRef d,AudioObjectID id,UInt32,UInt32,UInt32,const AudioServerPlugInIOCycleInfo *){
    return d==driver && id==device?noErr:kAudioHardwareBadObjectError;
}
OSStatus doIO(AudioServerPlugInDriverRef d,AudioObjectID id,AudioObjectID sid,UInt32,UInt32 op,UInt32 frames,const AudioServerPlugInIOCycleInfo *cycle,void *main,void *){
    if(d!=driver || id!=device || sid!=stream)return kAudioHardwareBadObjectError;
    if(op!=kAudioServerPlugInIOOperationReadInput)return kAudioHardwareUnsupportedOperationError;
    if(frames>maxIOFrames || (frames && !main))return kAudioHardwareIllegalOperationError;
    if(frames){
        const auto validTime=cycle && (cycle->mInputTime.mFlags & kAudioTimeStampSampleTimeValid) &&
            std::isfinite(cycle->mInputTime.mSampleTime) && cycle->mInputTime.mSampleTime>=0 &&
            cycle->mInputTime.mSampleTime < static_cast<double>(UINT64_MAX-maxIOFrames);
        if(validTime){
            const auto frame=static_cast<uint64_t>(cycle->mInputTime.mSampleTime);
            producerMailbox.read(frame,frames,clock_gettime_nsec_np(CLOCK_MONOTONIC),static_cast<float *>(main));
        }else std::memset(main,0,size_t(frames)*sizeof(Float32));
    }
    return noErr;
}
struct BindInterface {
    BindInterface(){
        interface={nullptr,query,addRef,release,initialize,createDevice,destroyDevice,
            addClient,removeClient,configChange,configChange,has,settable,sizeOf,get,set,
            startIO,stopIO,timestamp,willIO,boundaryIO,doIO,boundaryIO};
    }
} bindInterface;
} // namespace

extern "C" __attribute__((visibility("default"))) void *VoxMaskFactory(CFAllocatorRef,CFUUIDRef type){
    return type && CFEqual(type,kAudioServerPlugInTypeUUID)?driver:nullptr;
}

#ifdef VOXMASK_HAL_TESTING
// Only present in the isolated test bundle. Production audio requires an
// authenticated transport worker; no injection symbol ships in the driver.
extern "C" __attribute__((visibility("default"))) void VoxMaskTestPublish(
    const voxmask::ProducerBlock *block,uint64_t output_frame){
    if(block)producerMailbox.publish(*block,output_frame,clock_gettime_nsec_np(CLOCK_MONOTONIC));
}
extern "C" __attribute__((visibility("default"))) void VoxMaskTestClose(uint64_t epoch){
    producerMailbox.close(epoch);
}
#endif
