# Native diagnostic worker integration

The live `probe` and `monitor` commands now use:

```text
native capture callback -> input ring -> background C++ worker -> output ring
                                                                -> monitor callback
```

Probe mode drains input, computes levels and discards processed samples. Monitor
mode applies the existing original-voice gain/clamp and publishes that result. It
still requires explicit `--allow-raw-monitor`; there is no AI conversion.

## Scheduling and ownership

The callback owns input-ring writes and output-ring reads. It never waits for the
worker. The worker reads full 10 ms input chunks (441/480/960 frames for the three
supported client rates), runs the diagnostic processor, then publishes output.
When no full chunk is available it sleeps for 1 ms on its own background thread.
Callback thread scheduling priority has not been changed or qualified.

Input holds at most 8,192 frames; output at most 4,096. At 48 kHz those capacities
are 170.67 ms and 85.33 ms. They are capacity bounds, not target queue fill or
latency measurements. All queue/scratch storage is allocated before capture.

Startup and missing output produce zeros. Input or output overflow latches a fault;
the callback clears its output, the worker stops publishing, and the control loop
reports the fault and tears down the session. Invalid capture data also faults.
There is no direct-input fallback. A notification fault has the same behavior.

Stale or discontinuous input chunks are discarded. The next contiguous chunk can
be processed because the diagnostic gain/level processor has no persistent model
state. An AI adapter must add its own reset/context/re-priming contract. This is
not yet a general streaming inference engine.

## Time semantics

Callback arrival uses `steady_clock`. The input's first timestamp is estimated by
subtracting the callback block's sample span. This omits physical capture and OS
buffering delay: it is not an accurate physical capture timestamp.

The worker gives each output sample the oldest contributing input timestamp, so
reframing cannot make it appear younger. Queue reads reject estimated ages above
250 ms. Hardware sample-clock mapping, drift compensation and independent measured
end-to-end latency remain outstanding.

## Lifecycle

Configure once before starting. The control thread starts the worker before the
device. On stop it latches silence, stops device callbacks, then joins the worker.
Native teardown uninitializes the device before releasing its context or buffers.
The session is single-use: reopening requires a new native host. No reset or object
destruction may run concurrently with a callback. No join runs on a callback.

C ABI version is now 2 because the metric layout changed. Go and native code are
built together; mismatched ABI versions are rejected during host creation.

## Automated verification

The native suite covers all three rates, startup silence, normal gain, probe-mode
silence, stale output, expired input/discontinuity handling, input/output overflow,
invalid samples, repeated stop and concurrent worker/callback/metric access.
AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer runs passed.
Go race tests, CLI build, static analysis and the offline buffer check passed.

These tests do not establish hardware timing, headphone quality, long-running
stability, OS failure recovery or production-grade real-time behavior. The worker
still shares a process with the Go CLI; no virtual microphone or crash-isolated
media service has been implemented.

## Live capture evidence — 13 November 2026

A 10-second probe ran against the built-in MacBook Air microphone on macOS arm64.
No audio was recorded to disk or played back. The process stopped successfully.

| Observed metric | Result |
|---|---:|
| Client/backend capture rate | 48,000 Hz |
| Requested/backend period | 10 ms / 480 frames |
| Callbacks at final pre-stop snapshot | 1,001 |
| Captured frames | 480,480 |
| Frames submitted to the diagnostic processor | 480,480 |
| Maximum callback body duration | 63,708 ns (~63.7 microseconds) |
| Invalid blocks | 0 |
| Input/output overflow blocks | 0 / 0 |
| Stale frames / discontinuities | 0 / 0 |
| Reported device fault | false |

The callback count includes an initial callback near startup. Metrics are sampled
independently before stop and are not a transactional drain acknowledgement.
No monitor device was opened, so zero output-missing frames is not a playback
qualification result. Hardware latency, headphones, other rates/devices, long runs
and hotplug/suspend remain untested.
