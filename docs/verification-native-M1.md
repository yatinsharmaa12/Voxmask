# Native M1 verification — 10 November 2026

Environment: macOS arm64; Go 1.27.1; Apple Clang C++17.

| Check | Result |
|---|---|
| CLI build with native C++ callback path | Passed |
| Go unit tests and closed-native-handle checks | Passed |
| Go race detector | Passed; does not instrument C++ |
| Go vet | Passed |
| Native processor under AddressSanitizer + UndefinedBehaviorSanitizer | Passed |
| Native processor under ThreadSanitizer | Passed |
| Standalone C++/C ABI build, independent of Go | Passed |
| Invalid ABI version / null native handle checks | Passed |
| 100,000 deterministic randomized processor blocks per native test run | Passed |
| Concurrent callback writes and metric reads | Passed under native sanitizers |
| C++ new/new[] prohibition during 100,000 processor calls | Passed |
| Real Core Audio device enumeration outside execution sandbox | Passed |
| Vendored header SHA-256 | Matches recorded provenance |

Real-device enumeration found the built-in microphone and speakers. No live
microphone capture or playback was started for this verification.

The native allocation check detects C++ new/new[] calls in the tested processor
path; it is not a malloc interceptor or an audit of the platform audio backend.
Counters/timings are not evidence of achieved real-time latency.

Not yet verified: actual capture/monitor behavior, permission prompts, headphone
routing, hotplug/suspend notifications, 30-minute runs, physical latency, Windows,
Linux, or the CMake invocation (CMake is not installed in this environment).
The same native implementation compiled successfully via CGO and standalone Clang.
