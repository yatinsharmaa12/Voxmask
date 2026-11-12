# Native frame-ring contract and verification

Implemented and checked on 13 November 2026. This is a tested transport component;
it has not been connected to the live capture/monitor callback or an AI worker.

## Ownership and bounds

`FrameRing<Capacity>` has one producer and one consumer. Capacity is a compile-time
power of two. All PCM and per-frame metadata storage is allocated with the object,
not per operation. The diagnostic uses 8,192 mono frames: 262,144 bytes of frame
storage plus counters/padding. At 48 kHz this is 170.67 ms of queued audio, not an
assertion of added latency or a target queue fill.

The producer owns the write cursor. The consumer owns the read cursor. Release/
acquire publication prevents reading partially written frames or overwriting
unconsumed frames. Both cursors use lock-free 64-bit atomics, enforced at compile
time. A full queue rejects the entire incoming block and increments an overflow
counter. It never moves the consumer cursor to overwrite older frames.

Each frame carries a mono sample, capture timestamp, source frame index and session
epoch. Timestamps must use the reader's monotonic clock domain. The producer derives
per-frame timestamps from a block's first sample and rate; it does not infer the
physical capture time. Integrating the hardware timestamp mapping remains work.

## Read behavior

Valid read requests first clear the destination. The consumer discards wrong-epoch,
future-dated and expired samples. Age equal to the configured limit is accepted;
age above it is rejected. Missing output remains zero. A read processes only the
write cursor snapshot observed at entry, so a continuously writing producer cannot
extend the read loop indefinitely. At most Capacity frames are examined/cleared,
plus the caller's bounded output fill.

Consumed slots are cleared before returning them to the producer. This reduces
in-process residual data but is not a forensic erasure guarantee. Destruction or
reset must not race with either endpoint; the owner must stop and join participants
first. There is deliberately no concurrent reset method.

`ReadResult` includes the first returned capture timestamp/source index, maximum
returned sample age and a discontinuity flag. Dropped/stale samples, source-index
gaps, epoch changes and missing output flag discontinuity. A future stateful worker
must reset or re-prime before accepting discontinuous input. The queue does not
preserve wall-clock playout across gaps, correct drift, or perform inference.

Invalid output pointer or requests larger than Capacity return discontinuity without
touching memory. Caller-provided pointer lengths remain a C++ API precondition.
This is not an API for parsing arbitrary untrusted process memory.

## Offline diagnostic

Build the CLI, then run:

```sh
./bin/voxmask-audio buffer-check
```

This opens no audio context. It sends 1,440,000 synthetic frames in 480-frame blocks
through the queue, then stalls its consumer. Expected fault-injection totals:

| Result | Expected |
|---|---:|
| Incoming blocks rejected when full | 1 |
| Expired queued frames discarded | 8,160 |
| Missing destination frames zero-filled | 480 |

The clock is synthetic. Thirty simulated seconds are not a 30-second physical audio
test and these counters are not measured hardware xruns or latency benchmarks.

## Verification

- Native processor and ring suites passed AddressSanitizer/UndefinedBehaviorSanitizer.
- Both suites passed ThreadSanitizer.
- Ring tests compare 100,000 randomized operations against a reference FIFO.
- A genuine producer/consumer thread test verifies order for 200,000 frames.
- Deterministic cases cover wraparound, overflow, partial reads, empty reads,
  invalid samples, invalid rates, arithmetic overflow, epochs, age cutoff and
  future timestamps.
- Go tests with the race detector passed, including the CGO diagnostic contract.
- CLI build and static analysis passed.

Next: native worker integration with explicit reset/priming behavior, followed by
live-device qualification and the macOS virtual endpoint. No virtual microphone,
model inference or production crash-isolation boundary is implemented here.
