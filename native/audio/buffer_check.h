#ifndef VOXMASK_BUFFER_CHECK_H
#define VOXMASK_BUFFER_CHECK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VM_BUFFER_CHECK_ABI 1
// Offline synthetic clock simulation. Does not open any device or report measured latency.
typedef struct {
 uint32_t struct_size;
 uint32_t capacity_frames, sample_rate;
 uint64_t simulated_frames, overflow_blocks, stale_frames, missing_frames;
 uint64_t storage_bytes;
 uint32_t passed;
} vm_buffer_report;
int32_t vm_buffer_check(uint32_t abi_version, vm_buffer_report *out);
#ifdef __cplusplus
}
#endif
#endif
