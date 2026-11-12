# M1 status — native callback follow-up

## Implemented

- Go CLI with explicit input/output IDs, bounded-duration probe and opt-in raw monitor.
- Versioned C ABI over vendored miniaudio 0.11.23; no external Go module dependency.
- Native-owned device/context memory and callbacks; Go polls numeric counters only.
- Native finite-input validation, zero fill, monitor gain/clamp, frame-size limit,
  lock-free numeric atomics and fault latch on backend stop/interruption/reroute.
- Control-loop detection of stalled callbacks and invalid blocks; no default/dummy fallback.
- Standalone CMake target and direct Clang sanitizer test script.
- Fixed-capacity native frame ring with source timestamps, session epochs, overflow
  rejection, stale/future/old-session rejection, zero fill and continuity flags.
- Offline `buffer-check` CLI diagnostic; no hardware access required.
- Native queues connected to the live probe/monitor path through a background
  diagnostic worker, with startup/underrun silence and overload fault latching.

## Boundaries

This remains a development raw-audio harness, not voice conversion. The native
module runs in the CLI process, so native crashes can still terminate the process.
The native ring/worker pipeline is connected to live audio. No production sidecar,
AI inference or drift controller exists yet. A separate silent macOS driver
prototype is built but not installed or connected to the worker. Physical speaker output cannot be guaranteed silent
by an independent endpoint after the process exits.

Counters do not measure end-to-end latency or hardware underruns. Native backend
period fields are diagnostic configuration data, not measured device latency.
The Go race detector cannot validate C++ concurrency; native sanitizers supplement it.

## Next acceptance work

1. Short built-in microphone capture passed (10 seconds at 48 kHz); complete
   headphone-only monitor and permission-denial/revocation tests.
2. Thirty-minute built-in/USB runs across supported sample rates.
3. Native callback timing traces and independent latency fixture.
4. Hot-unplug, permissions, suspend/resume and real notification coverage.
5. Implement hardware clock mapping/drift correction and model-specific re-priming
   before AI integration. The current worker has no stateful model to reset.
6. macOS virtual endpoint prototype with its own silent producer-failure behavior.

Historical initial slice: Go callback tests and fuzz run passed, and real device
listing succeeded on macOS arm64. That callback implementation has now been replaced;
see [the current verification report](verification-native-M1.md) rather than treating old tests as native evidence.

Current buffer evidence is recorded in [the buffer contract](native-buffer-contract.md).

Worker integration and its validation are described in [the worker contract](native-worker-contract.md).

M2 has begun: [silent macOS endpoint prototype](M2-macos-virtual-microphone.md).
