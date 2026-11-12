// Package nativeaudio owns a native audio context. Control methods are serialized
// by the caller; no C callback enters Go and no retained pointer references Go memory.
package nativeaudio

/*
#cgo CXXFLAGS: -std=c++17 -O2
#cgo darwin LDFLAGS: -framework CoreFoundation -framework CoreAudio -framework AudioToolbox
#cgo linux LDFLAGS: -ldl -lpthread -lm
#include <stdlib.h>
#include "voxmask_audio.h"
*/
import "C"
import (
	"fmt"
	"unsafe"
)

type Host struct{ ptr *C.vm_audio }
type Device struct {
	ID      string `json:"id"`
	Name    string `json:"name"`
	Kind    string `json:"kind"`
	Default bool   `json:"default"`
}
type Format struct {
	ClientRate            uint32 `json:"client_rate"`
	CaptureChannels       uint32 `json:"capture_channels"`
	BackendCaptureRate    uint32 `json:"backend_capture_rate"`
	BackendCapturePeriod  uint32 `json:"backend_capture_period_frames"`
	BackendPlaybackRate   uint32 `json:"backend_playback_rate"`
	BackendPlaybackPeriod uint32 `json:"backend_playback_period_frames"`
}
type Metrics struct {
	Callbacks         uint64  `json:"callbacks"`
	Frames            uint64  `json:"frames"`
	InvalidBlocks     uint64  `json:"invalid_blocks"`
	MaxCallbackFrames uint32  `json:"max_callback_frames"`
	MaxCallbackWorkNS uint64  `json:"max_callback_work_ns"`
	InputRMSDB        float64 `json:"input_rms_db"`
	DeviceFault       bool    `json:"device_fault"`
}

func result(r C.int32_t) error {
	if r != 0 {
		return fmt.Errorf("native audio: %s (%d)", C.GoString(C.vm_audio_error(r)), int32(r))
	}
	return nil
}
func New() (*Host, error) {
	h := &Host{}
	if err := result(C.vm_audio_create(C.VM_AUDIO_ABI, &h.ptr)); err != nil {
		return nil, err
	}
	return h, nil
}
func (h *Host) Close() {
	if h.ptr != nil {
		C.vm_audio_destroy(h.ptr)
		h.ptr = nil
	}
}
func (h *Host) Devices() ([]Device, error) {
	if h.ptr == nil {
		return nil, fmt.Errorf("native audio host closed")
	}
	n := C.size_t(C.VM_AUDIO_MAX_DEVICES)
	mem := C.calloc(n, C.size_t(C.sizeof_vm_device_info))
	if mem == nil {
		return nil, fmt.Errorf("device list allocation failed")
	}
	defer C.free(mem)
	var count C.uint32_t
	if err := result(C.vm_audio_list(h.ptr, (*C.vm_device_info)(mem), C.uint32_t(n), &count)); err != nil {
		return nil, err
	}
	raw := unsafe.Slice((*C.vm_device_info)(mem), int(count))
	ds := make([]Device, 0, len(raw))
	for i := range raw {
		d := &raw[i]
		kind := "output"
		if d.capture != 0 {
			kind = "input"
		}
		ds = append(ds, Device{C.GoString(&d.id[0]), C.GoString(&d.name[0]), kind, d.is_default != 0})
	}
	return ds, nil
}
func (h *Host) Open(input, output string, monitor bool, rate, period uint32) (Format, error) {
	in := C.CString(input)
	defer C.free(unsafe.Pointer(in))
	out := C.CString(output)
	defer C.free(unsafe.Pointer(out))
	o := C.vm_audio_options{struct_size: C.sizeof_vm_audio_options, abi_version: C.VM_AUDIO_ABI, rate: C.uint32_t(rate), period_ms: C.uint32_t(period), input_id: in, output_id: out}
	if monitor {
		o.monitor = 1
	}
	var f C.vm_audio_format
	if err := result(C.vm_audio_open(h.ptr, &o, &f)); err != nil {
		return Format{}, err
	}
	return Format{uint32(f.client_rate), uint32(f.capture_channels), uint32(f.backend_capture_rate), uint32(f.backend_capture_period_frames), uint32(f.backend_playback_rate), uint32(f.backend_playback_period_frames)}, nil
}
func (h *Host) Start() error { return result(C.vm_audio_start(h.ptr)) }
func (h *Host) Stop() error  { return result(C.vm_audio_stop(h.ptr)) }
func (h *Host) Snapshot() (Metrics, error) {
	var m C.vm_audio_metrics
	if err := result(C.vm_audio_snapshot(h.ptr, &m)); err != nil {
		return Metrics{}, err
	}
	return Metrics{uint64(m.callbacks), uint64(m.frames), uint64(m.invalid_blocks), uint32(m.max_callback_frames), uint64(m.max_callback_work_ns), float64(m.input_rms_db), m.fault != 0}, nil
}
