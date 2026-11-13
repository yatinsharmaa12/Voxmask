# Producer transport contract — M2 follow-up

The installed macOS endpoint is silent and system-visible. This change adds the
validation layer for the future converted-audio producer. It is deliberately
separate from the AudioServerPlugIn callback until the cross-process transport
mechanism is qualified.

## Handshake

The producer sends a versioned `ProducerHello` containing the fixed magic, protocol
ABI, negotiated sample rate, mono channel count, nonzero session epoch and a
32-byte capability token. The consumer compares the token in constant time and
accepts only 44.1, 48 or 96 kHz mono. One validator accepts one epoch; a second
producer cannot replace it without a new consumer session.

The token is an authorization capability, not a password or a replacement for OS
peer identity. The eventual macOS transport must authenticate the process with a
Mach-service/XPC peer check and keep this token in protected session state. It must
not accept a token from an unauthenticated webpage or model bundle.

## Audio blocks

Each block carries ABI/magic, frame count, rate, epoch, sequence, and a monotonic
capture timestamp followed by up to 960 mono float frames. The validator accepts
only contiguous sequences beginning at zero. It rejects duplicates, gaps, wrong
epochs, future timestamps, data older than 250 ms, unsupported formats, oversized
blocks and non-finite samples. A rejected block never reaches the endpoint.

Close invalidates the epoch. The endpoint's default behavior for a closed producer,
missing block, stale block or validation error is zero-filled output. There is no
raw-input fallback.

The wire format must be serialized explicitly as little-endian fixed-width fields;
the C++ structs are test representations, not a promise that padding can be sent
between processes. A real transport must frame messages, bound message size, reject
version changes, and authenticate the peer before accepting PCM.

## Current implementation and tests

`native/audio/producer_protocol.hpp` implements the handshake and block validator.
`native/tests/producer_protocol_test.cpp` covers bad tokens, repeated handshakes,
epochs, sequence gaps/duplicates, timestamp age, finite values, format limits and
close behavior. It does not open a microphone or contact the installed HAL device.

The protocol is therefore ready for a transport adapter, not yet a connected
producer. The next adapter should use a Mach service listed under
`AudioServerPlugIn_MachServices` in the plug-in bundle, authenticate the peer,
copy validated blocks into a bounded native mailbox, and have `DoIOOperation` read
only from that mailbox. The adapter must be tested with producer crash, disconnect,
token mismatch, stale output and simultaneous clients before it can replace the
current silent read path.

The bundle now declares `local.voxmask.producer` in its Info.plist. Declaration
alone does not publish a Mach port or authenticate a client. The next implementation
must establish the service in the plug-in process, use an OS peer identity check,
perform the hello/token exchange off the real-time thread, and hand only copied
validated frames to the real-time mailbox. `DoIOOperation` must never send a Mach
message, allocate, wait, or parse untrusted variable-length data.
