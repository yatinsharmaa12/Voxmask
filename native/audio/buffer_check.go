package nativeaudio

/*
#include "buffer_check.h"
*/
import "C"
import "fmt"

type BufferReport struct {
	Mode            string `json:"mode"`
	Passed          bool   `json:"passed"`
	CapacityFrames  uint32 `json:"capacity_frames"`
	SampleRate      uint32 `json:"sample_rate"`
	SimulatedFrames uint64 `json:"simulated_frames"`
	OverflowBlocks  uint64 `json:"overflow_blocks"`
	StaleFrames     uint64 `json:"stale_frames"`
	MissingFrames   uint64 `json:"missing_frames"`
	StorageBytes    uint64 `json:"pcm_and_metadata_storage_bytes"`
}

// CheckBuffers runs deterministic scenarios with synthetic PCM and a synthetic
// clock. It does not create an audio context or capture microphone input.
func CheckBuffers() (BufferReport, error) {
	r := C.vm_buffer_report{struct_size: C.sizeof_vm_buffer_report}
	code := C.vm_buffer_check(C.VM_BUFFER_CHECK_ABI, &r)
	report := BufferReport{"synthetic-clock-check", r.passed != 0, uint32(r.capacity_frames), uint32(r.sample_rate), uint64(r.simulated_frames), uint64(r.overflow_blocks), uint64(r.stale_frames), uint64(r.missing_frames), uint64(r.storage_bytes)}
	if code != 0 {
		return report, fmt.Errorf("native buffer check failed: %d", code)
	}
	return report, nil
}
