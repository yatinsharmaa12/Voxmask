# M2 — macOS silent virtual microphone prototype

Status: built and validated in an isolated mock host on 13 November 2026.
Installed and validated as an OS-enumerated silent input on the development Mac.
The device remains a prototype and has no converted-audio producer.

## What exists

A standalone Core Audio AudioServerPlugIn bundle:

```text
build/macos/VoxMaskSilent.driver
```

It declares a plug-in object, one virtual input device and one capture stream.
Its name is `VoxMask Prototype (Silent)`. The fixed format is mono, 48 kHz,
32-bit packed floating-point PCM. It has no output streams, volume controls,
physical microphone access, network access, producer IPC or model code.

The installed build fills every ReadInput operation with zero. The newer source
build reads a bounded mailbox indexed by HAL sample time; because no producer
transport exists, its production build also returns silence. A test-only symbol
injects synthetic PCM into the isolated test bundle. Neither build can copy raw
microphone audio. This does not prove future producer transport or AI privacy.

The prototype declines eligibility as the system default device. It never changes
system defaults. Whether specific calling applications enumerate/select it still
requires an installed-device integration test.

## Source and build

- `native/platform/macos/hal/VoxMaskSilent.cpp`: HAL object/property interface,
  fixed format negotiation, client lifecycle, clock mapping and mailbox read callback.
- `native/audio/producer_mailbox.hpp`: atomic bounded audio handoff with silence on
  gaps, stale blocks and producer close.
- `native/platform/macos/hal/Info.plist`: bundle identity and CFPlugIn factory mapping.
- `native/platform/macos/tests/hal_test.cpp`: dynamically loaded mock-host tests.
- `tools/build-macos-hal.sh`: build the local architecture and ad-hoc sign the bundle.
- `tools/test-macos-hal.sh`: instrument both plug-in and test host with sanitizers.

```sh
bash tools/build-macos-hal.sh
bash tools/test-macos-hal.sh address,undefined
bash tools/test-macos-hal.sh thread
```

Xcode Command Line Tools and the macOS SDK are required. The current build is for
the machine's architecture, not a universal binary. Ad-hoc signing is for local
development; it is not Developer ID signing, notarization or distribution approval.
The build script does not install files or restart any audio service.

## Properties and lifecycle

The factory exposes Apple's AudioServerPlugIn driver interface. Property lookup,
size and value operations share one dispatcher. The device has a stable prototype
UID, reports a single input stream and rejects unsupported format changes.
Setting the already-active 48 kHz format is a no-op; there is no asynchronous
reconfiguration or alternate-rate support in this prototype.

A fixed control-plane table tracks up to 64 registered clients. Duplicate starts
are idempotent. Stopping one client does not stop another; removing a running client
updates the running count. Control-plane changes notify the host when the aggregate
running state changes. A mutex protects this table, but no I/O or timestamp callback
uses that mutex. The read path checks sample time and copies from the atomic
mailbox without a lock or allocation; if no valid block covers a frame, it is zero.

The clock maps a continuous `mach_absolute_time()` timeline to 48 kHz sample time.
The SDK specifies a minimum zero-timestamp period of 10,923 frames; this prototype
uses 16,384. This timestamp reporting period is not a claimed audio latency or a
requirement to queue that much audio. Host timestamps do not move backwards and the
seed stays fixed because this implementation never resets the clock while loaded.

## Verification completed

- Built the `.driver` bundle against the installed macOS SDK.
- Validated Info.plist syntax and verified its ad-hoc code signature.
- Loaded the factory from the built bundle in a normal, isolated test process.
- Checked object discovery, stream direction, UID translation and string ownership.
- Checked fixed 48 kHz mono format and rejection of 44.1 kHz.
- Checked undersized property buffers and unsupported objects/properties.
- Checked two-client start/stop/removal behavior and host running-state notifications.
- Checked concurrent reads with buffer guards: only requested samples become zero.
- Checked monotonic sample/host timestamps and their 48 kHz conversion.
- Repeated the mock-host suite with AddressSanitizer/UndefinedBehaviorSanitizer and
  ThreadSanitizer instrumentation on both the plug-in and host.
- Tested synthetic mailbox delivery to two mock HAL clients, producer close,
  stale blocks and concurrent readers/writer under sanitizers. The production
  bundle exports no test injection entry point.

These checks do not exercise Core Audio's actual plug-in loader, sandbox, device
registration, calling-app compatibility, client permissions or OS restart behavior.
The bundle is installed under `/Library/Audio/Plug-Ins/HAL` on the development
machine, and Core Audio was restarted once to load it. Native service crash
behavior and real OS multi-client capture remain unverified.

## Remaining M2 work

1. ~~Prepare and review a local test installation/removal procedure.~~ Completed:
   the authenticated helper installed the bundle, and Core Audio was restarted
   explicitly to reload it. Installation
   writes to `/Library/Audio/Plug-Ins/HAL` and requires administrator privileges.
   Schedule any required audio-service restart/reboot outside active audio use.
2. Verify actual OS enumeration, capture silence, format negotiation, simultaneous
   readers and removal on a test machine; retain diagnostics without recordings.
   Enumeration, format and single-reader silence are complete; multi-reader and
   removal tests remain.
3. Design an authenticated producer transport that satisfies the audio host's
   sandbox/identity requirements. The SDK documents explicit Mach-service allowlists
   for plug-in IPC; an arbitrary user-process socket is not assumed to work.
   The protocol validator and bundle service declaration are now in place; the
   broker/plug-in Mach-port implementation remains.
4. Bounded-age/epoch validation and mailbox silence on close are implemented in
   source. Detecting real producer loss and closing its epoch requires the IPC worker.
5. Test the transport with synthetic marker audio before any model integration.
6. Implement distribution signing, installer rollback and supported-OS qualification.

Do not connect the existing raw-monitor worker to the virtual microphone as a
user-facing feature. It does not transform voices. The eventual normal producer
must publish only validated converted audio.

Headphone playback is still pending: the latest real-device enumeration showed
only the built-in microphone and speakers. No speaker playback test was run.

## System integration result — 13 November 2026

The bundle was installed at `/Library/Audio/Plug-Ins/HAL/VoxMaskSilent.driver`
through the native macOS administrator-authentication flow. Core Audio was then
restarted after explicit user approval. The exact UID became visible to the system
and reported 48,000 Hz, one channel, with capture initially stopped.

A three-second Core Audio read test passed:

| Metric | Result |
|---|---:|
| Callbacks | 281 |
| Frames read | 143,872 |
| Nonzero samples | 0 |
| Invalid buffers | 0 |

The test opened only the VoxMask prototype UID and stored no PCM. This validates
the installed silent endpoint; it does not validate producer IPC, AI conversion,
calling-app selection, or virtual microphone output from transformed speech.

Interface reference: installed SDK `CoreAudio.framework/Headers/AudioServerPlugIn.h`
and Apple's [virtual-device sample documentation](https://developer.apple.com/documentation/coreaudio/creating-an-audio-server-driver-plug-in).
