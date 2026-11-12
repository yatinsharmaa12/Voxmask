#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio/miniaudio.h"
#include "worker_pipeline.hpp"
#include <new>
#include <cstring>
#include <memory>

constexpr int32_t vm_bad_state=-10000, vm_bad_abi=-10001;
struct vm_audio {
 ma_context context{};
 ma_device device{};
 voxmask::AudioWorker worker;
 bool context_ready=false, device_ready=false, started=false;
};
static_assert(sizeof(ma_device_id)*2+1 <= sizeof(vm_device_info::id),"device ID capacity");
static void id_string(const ma_device_id &id,char *out) noexcept {
 const auto *bytes=reinterpret_cast<const unsigned char *>(&id);
 size_t n=sizeof(id); while(n>1 && bytes[n-1]==0) --n;
 const char hex[]="0123456789abcdef";
 for(size_t i=0;i<n;++i){out[i*2]=hex[bytes[i]>>4];out[i*2+1]=hex[bytes[i]&15];}
 out[n*2]=0;
}
static bool find_id(ma_device_info *items,ma_uint32 n,const char *wanted,ma_device_id &out) noexcept {
 if(!wanted || !wanted[0]) return false;
 for(ma_uint32 i=0;i<n;++i){char id[1025];id_string(items[i].id,id);if(std::strcmp(id,wanted)==0){out=items[i].id;return true;}}
 return false;
}
static void data_callback(ma_device *d,void *out,const void *in,ma_uint32 frames) {
 auto *host=static_cast<vm_audio *>(d->pUserData);
 host->worker.core.callback(static_cast<float *>(out),static_cast<const float *>(in),
     frames,voxmask::monotonic_ns());
}
static void notification(const ma_device_notification *n) {
 auto *host=static_cast<vm_audio *>(n->pDevice->pUserData);
 if(n->type==ma_device_notification_type_stopped || n->type==ma_device_notification_type_rerouted ||
    n->type==ma_device_notification_type_interruption_began)
  host->worker.core.silence();
}
extern "C" int32_t vm_audio_create(uint32_t abi,vm_audio **out) {
 if(!out)return MA_INVALID_ARGS;*out=nullptr;
 if(abi!=VM_AUDIO_ABI)return vm_bad_abi;
 auto h=std::unique_ptr<vm_audio>(new(std::nothrow) vm_audio);
 if(!h)return MA_OUT_OF_MEMORY;
 // Explicit lists prevent miniaudio's dummy backend fallback.
#if defined(__APPLE__)
 const ma_backend backends[]={ma_backend_coreaudio};
#elif defined(_WIN32)
 const ma_backend backends[]={ma_backend_wasapi};
#elif defined(__linux__)
 const ma_backend backends[]={ma_backend_pulseaudio,ma_backend_alsa};
#else
 return MA_NO_BACKEND;
#endif
 auto cfg=ma_context_config_init();
 auto result=ma_context_init(backends,sizeof(backends)/sizeof(backends[0]),&cfg,&h->context);
 if(result!=MA_SUCCESS)return result;
 h->context_ready=true;*out=h.release();return MA_SUCCESS;
}
extern "C" int32_t vm_audio_list(vm_audio *h,vm_device_info *items,uint32_t capacity,uint32_t *written) {
 if(!h||!written||(!items&&capacity))return MA_INVALID_ARGS;
 *written=0;
 ma_device_info *playback=nullptr,*capture=nullptr;ma_uint32 np=0,nc=0;
 auto result=ma_context_get_devices(&h->context,&playback,&np,&capture,&nc);
 if(result!=MA_SUCCESS)return result;
 if(uint64_t(np)+nc>capacity)return MA_OUT_OF_RANGE;
 for(uint32_t k=0;k<2;++k){auto *src=k?playback:capture;auto n=k?np:nc;
  for(ma_uint32 i=0;i<n;++i){auto &dst=items[(*written)++];std::memset(&dst,0,sizeof(dst));
   id_string(src[i].id,dst.id);std::strncpy(dst.name,src[i].name,sizeof(dst.name)-1);
   dst.capture=k?0:1;dst.is_default=src[i].isDefault;
  }
 }
 return MA_SUCCESS;
}
extern "C" int32_t vm_audio_open(vm_audio *h,const vm_audio_options *o,vm_audio_format *out) {
 if(!h||!o||!out)return MA_INVALID_ARGS;
 if(o->struct_size!=sizeof(*o)||o->abi_version!=VM_AUDIO_ABI)return vm_bad_abi;
 if(h->device_ready)return vm_bad_state;
 if((o->rate!=44100&&o->rate!=48000&&o->rate!=96000)||o->period_ms<2||o->period_ms>40||o->monitor>1)return MA_INVALID_ARGS;
 ma_device_info *ps=nullptr,*cs=nullptr;ma_uint32 np=0,nc=0;
 auto result=ma_context_get_devices(&h->context,&ps,&np,&cs,&nc);if(result!=MA_SUCCESS)return result;
 ma_device_id input{},output{};
 if(!find_id(cs,nc,o->input_id,input))return MA_NO_DEVICE;
 if(o->monitor&&!find_id(ps,np,o->output_id,output))return MA_NO_DEVICE;
 auto cfg=ma_device_config_init(o->monitor?ma_device_type_duplex:ma_device_type_capture);
 cfg.sampleRate=o->rate;cfg.periodSizeInMilliseconds=o->period_ms;
 cfg.capture.pDeviceID=&input;cfg.capture.format=ma_format_f32;cfg.capture.channels=1;
 if(o->monitor){cfg.playback.pDeviceID=&output;cfg.playback.format=ma_format_f32;cfg.playback.channels=1;}
 cfg.dataCallback=data_callback;cfg.notificationCallback=notification;cfg.pUserData=h;
 
 result=ma_device_init(&h->context,&cfg,&h->device);if(result!=MA_SUCCESS)return result;
 h->device_ready=true;
 if(!h->worker.core.configure(h->device.sampleRate,o->monitor!=0))return vm_bad_state;
 *out={h->device.sampleRate,h->device.capture.channels,h->device.capture.internalSampleRate,
       h->device.capture.internalPeriodSizeInFrames,h->device.playback.internalSampleRate,
       h->device.playback.internalPeriodSizeInFrames};
 return MA_SUCCESS;
}
extern "C" int32_t vm_audio_start(vm_audio *h) {
 if(!h||!h->device_ready||h->started)return vm_bad_state;
 // Sessions are single-use: no reuse of metrics/fault state after stop.
 if(h->worker.core.failed() || !h->worker.start())return vm_bad_state;
 auto r=ma_device_start(&h->device);
 if(r==MA_SUCCESS)h->started=true;else h->worker.stop();
 return r;
}
extern "C" int32_t vm_audio_stop(vm_audio *h) {
 if(!h)return MA_INVALID_ARGS;
 h->worker.core.silence();
 auto r=h->started?ma_device_stop(&h->device):MA_SUCCESS;
 h->worker.stop();
 if(r==MA_SUCCESS)h->started=false;
 return r;
}
extern "C" int32_t vm_audio_snapshot(vm_audio *h,vm_audio_metrics *out) {
 if(!h||!out)return MA_INVALID_ARGS;*out=h->worker.core.snapshot();return MA_SUCCESS;
}
extern "C" void vm_audio_destroy(vm_audio *h) {
 if(!h)return;
 if(h->device_ready){vm_audio_stop(h);ma_device_uninit(&h->device);}
 if(h->context_ready)ma_context_uninit(&h->context);
 delete h;
}
extern "C" const char *vm_audio_error(int32_t code) {
 if(code==vm_bad_state)return "invalid native audio lifecycle state";
 if(code==vm_bad_abi)return "unsupported audio ABI or structure size";
 return ma_result_description(static_cast<ma_result>(code));
}
