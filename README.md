# VoxMask — M1 native audio harness

This development slice lists devices, captures numeric audio diagnostics, and
optionally monitors original microphone audio through a selected output.
**No AI conversion or virtual microphone is implemented yet. This harness does not
protect voice identity.**

The Go CLI controls a native C++ audio module through a versioned C ABI. The audio
and device-notification callbacks remain entirely in native code. Capture feeds a
bounded native queue; a background C++ worker processes 10 ms chunks; the monitor
callback reads the separate output queue or emits silence. No PCM or audio
callback enters Go. The native module is currently in-process; the production
sidecar and virtual endpoint are later milestones.

## Build and test

Requires Go 1.27+, CGO and a C++17 compiler (Xcode Command Line Tools on macOS).
The local module path is provisional. There are no external Go module dependencies.
A pinned miniaudio header is vendored with its upstream license and source hash.

```sh
go test ./...
go test -race ./...
go vet ./...
go build -o bin/voxmask-audio ./cmd/voxmask-audio

# Native processor tests; no microphone access required.
bash tools/test-native.sh address,undefined
bash tools/test-native.sh thread
```

Go's race detector does not inspect C++ internals. Use the native sanitizer tests
as well. CMake 3.20+ can build the standalone native library and tests:

```sh
cmake -S native -B build/native -DBUILD_TESTING=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

## Run

```sh
./bin/voxmask-audio buffer-check  # synthetic audio only; no device opened
./bin/voxmask-audio devices
./bin/voxmask-audio probe --input '<input ID from devices>' --duration 10s
./bin/voxmask-audio monitor --input '<input ID>' --output '<headphone output ID>' --allow-raw-monitor --duration 10s
```

Use headphones for monitor mode. It plays ORIGINAL voice at approximately -12 dB
gain, with clamping. Choose physical devices, not virtual cables feeding another
application. The harness cannot identify every third-party virtual cable.
Capture is never started by help, tests, build or device listing. macOS may request
microphone access for the launching terminal/app when you run probe or monitor.
No audio files, transcripts or feature caches are created. Ctrl-C stops capture.

Dummy/null audio backend fallback is disabled. macOS uses Core Audio, Windows
uses WASAPI, and this Linux prototype tries PulseAudio compatibility then ALSA.
The production native PipeWire adapter is not implemented. Device IDs must be
selected explicitly. A missing device produces an error, with no automatic default
fallback. IDs may change after reconnection; rerun devices in that case.

A backend-reported stop, interruption or reroute latches the native silence gate.
The control loop reports it within its next one-second poll and tears down the
session. Three seconds without callback progress also stops the session. Native
notification coverage still needs hardware hotplug/suspend qualification; this is
not a system-wide fail-silent virtual endpoint guarantee.

Numeric JSON goes to stdout; errors go to stderr. Device listing includes local
names/IDs, so review it before sharing. The session is single-use after stop.

## Metrics

- `callback_engine`: `native-cpp-worker` confirms the selected implementation.
- `client_rate`: miniaudio client stream rate; backend conversion may occur.
- `backend_capture_rate` / `backend_capture_period_frames`: miniaudio's initialized
  backend fields. These are not independent physical-device or latency measurements.
- `requested_period_ms`: request, not an observed callback period.
- `max_callback_frames`: largest observed callback size. Blocks above 4096 frames
  or containing non-finite samples are silenced/rejected and the session stops.
- `max_callback_work_ns`: native processor duration, excluding backend dispatch,
  device buffering and Go control work. It is not end-to-end latency.
- `input_rms_db`: latest block RMS, floored at -120 dBFS.
- `device_fault`: latched notification state, sampled before intentional final stop.
- Counters are independently atomic snapshots, not transactional measurements.

Callbacks perform no application allocation, blocking lock, I/O or Go dispatch.
32/64-bit counter atomics are required to be lock-free at compile time. This removes
our previous Go-dispatch dependency but does not prove hard-real-time scheduling,
backend internals or acceptable tail latency on every device.

## Source map

- `cmd/voxmask-audio`: CLI commands and explicit raw-monitor acknowledgement.
- `internal/audio`: validation and Go control/diagnostic loop.
- `native/include/voxmask_audio.h`: versioned C ABI and ownership contract.
- `native/audio`: CGO control wrapper, C++ device lifecycle, callbacks and metrics.
  Sources are colocated so Go invalidates cached native builds after edits.
- `native/tests`: deterministic, randomized and concurrent native processor tests.
- `native/third_party/miniaudio`: pinned upstream source and provenance.
- `docs/M1-status.md`: validation evidence and outstanding acceptance work.

No project license has been chosen; resolve it before public redistribution.
Dependency licensing does not automatically license VoxMask's own code.

After updating the vendored external header, run `go clean -cache` before rebuilding.

## Bounded-buffer diagnostic

`buffer-check` exercises the new native SPSC (single producer, single consumer)
frame ring with a synthetic clock: 30 simulated seconds of normal traffic, followed
by a consumer stall and expiry. It reports `mode: synthetic-clock-check` so its
results cannot be confused with measured hardware latency.

The diagnostic uses 8,192 frames at 48 kHz (170.67 ms of capacity) and a 250 ms
maximum source-audio age. Its deliberate overload should report one rejected
block, 8,160 expired frames and 480 zero-filled missing frames. Those are expected
fault-injection outcomes, not defects or physical-device underrun measurements.

The queue is now connected to live capture and a diagnostic worker. This worker
only computes levels and optional raw-monitor gain/clamping; it does not perform
AI inference. See [the worker contract](docs/native-worker-contract.md).

Worker metrics include `processed_frames`, input/output overflow blocks, monitor
missing frames, stale discarded frames and discontinuities. Startup silence and
occasional missing monitor data count as missing frames; these are application
queue counters, not physical-device xrun measurements. Overflow latches a fault
and mutes the session; it never falls back to direct microphone output.
