#ifndef VOXMASK_AUDIO_H
#define VOXMASK_AUDIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VM_AUDIO_ABI 2
#define VM_AUDIO_MAX_FRAMES 4096
#define VM_AUDIO_MAX_DEVICES 512
// This is a development monitor ABI. No conversion or endpoint is implemented.
typedef struct vm_audio vm_audio;
typedef struct { char id[1025]; char name[256]; uint32_t capture; uint32_t is_default; } vm_device_info;
typedef struct {
 uint32_t struct_size, abi_version;
 uint32_t rate, period_ms, monitor;
 const char *input_id; const char *output_id;
} vm_audio_options;
typedef struct {
 uint32_t client_rate, capture_channels;
 uint32_t backend_capture_rate, backend_capture_period_frames;
 uint32_t backend_playback_rate, backend_playback_period_frames;
} vm_audio_format;
typedef struct {
 uint64_t callbacks, frames, invalid_blocks, max_callback_work_ns;
 uint32_t max_callback_frames, fault;
 double input_rms_db;
 uint64_t processed_frames, input_overflow_blocks, output_overflow_blocks;
 uint64_t output_missing_frames, stale_frames, discontinuities;
} vm_audio_metrics;
// Control calls are serialized by the caller. Snapshot may overlap native callbacks.
// Strings are borrowed only during calls. Destroy waits for callback termination.
int32_t vm_audio_create(uint32_t abi_version, vm_audio **out);
int32_t vm_audio_list(vm_audio *, vm_device_info *items, uint32_t capacity, uint32_t *written);
int32_t vm_audio_open(vm_audio *, const vm_audio_options *, vm_audio_format *);
int32_t vm_audio_start(vm_audio *);
int32_t vm_audio_stop(vm_audio *);
int32_t vm_audio_snapshot(vm_audio *, vm_audio_metrics *);
void vm_audio_destroy(vm_audio *);
const char *vm_audio_error(int32_t code);
#ifdef __cplusplus
}
#endif
#endif
