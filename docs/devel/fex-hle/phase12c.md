# Phase0.12c: explicit finite PCM playback

[Phase0.12b on Pocket S](phase12b-android-report.md) passes all 213 expected
native CPU/offscreen Vulkan checks. This increment adds an isolated AAudio
output sink and explicit playback controls. It is not Xbox APU/DSP, DirectSound,
a streaming mixer, game audio or a title performance measurement.

## Guest capture and finite source

After the first native guest-produced packet passes its Vulkan pixel check,
its 128 PCM bytes are copied into an independent native registry before the
packet lease retires. Playback keeps no guest or queue pointer. The PCM seed is
64 mono S16LE frames at 48 kHz: only 1.33 ms. Before opening an audio stream,
the controller decodes and repeats that exact seed into 24,000 owned samples,
a finite half-second diagnostic. It is an intentional 3 kHz sawtooth, not a
music sample or a test for emulated game crackling. There is no fade or guest
mixer processing. The source cannot be republished during a reserved/running test.

The callback owns its cursor. Variable chunks use at most one source memcpy
and tail memset, plus lock-free atomics for cancellation and counters. There is
no allocation, mutex, FEX execution, JNI, logging, clock call, sleep, stream close
or per-sample conversion/modulo in the callback. Invalid or stale calls stop
without unsafe writes. Generation tokens keep old cancellation from affecting a
new source. The precomputed source and callback userdata outlive stream close.

## AAudio control and lifetime

Direct AAudio adds only system libaaudio.so; required APIs are available since
API26 and fit minimum API29. No SDL/Oboe or new bundled audio dependency is used.
The controller requests OUTPUT, SHARED, LOW_LATENCY, mono S16 at 48 kHz. Actual
rate, channel count and format are verified before start; incompatible formats
are rejected rather than written incorrectly. Actual sharing/performance mode,
burst, capacity and selected buffer size are reported. A two-burst buffer is
requested when capacity permits; this is a diagnostic setting, not a claim of
best device/game buffering.

The callback supplies silence after the finite source while the controller
waits for endpoint frame progress. Returning STOP on the last content callback
would not prove that queued samples drained. The controller polls outside the
real-time callback, measures source/callback/endpoint counters and timestamp
progress while STARTED, then requests stop, waits for a quiescent state and closes.
The first callback latency is a controller-observed upper bound, not an exact
callback timestamp. Stream errors and disconnects are recorded by atomics;
controller code performs shutdown. State/drain waits have deadlines, but driver
close itself is not universally bounded.

If close fails, or callback quiescence cannot be established after successful
close, one bounded session is quarantined rather than freeing possibly live
userdata. Further playback is disabled until process restart. Cancel before
worker entry remains latched through a reserved token. An abandoned reservation
is explicitly released if thread construction/start or JNI journal setup fails.

## UI and recovery

Code18 retains **Run CPU + GPU fixtures** and all 213 accepted native identities.
Only a successful current run plus a native owned PCM snapshot enables
**Play 0.5s PCM test**. Nothing plays automatically. **Stop PCM**, Activity pause,
stop and destruction cancel playback; old Activity instances cannot cancel a
newer owner's test. Workers retain files/static state rather than Activity/View
objects, and UI updates target only the current foreground Activity.

The separate PCM report is appended to the CPU/GPU result for sharing. A bounded
final native summary is persisted after shutdown before Java string/file work,
so a crash during result delivery can recover counters as well as stage progress.
Reopen and share an incomplete run before rerunning; a new test overwrites its
progress file. An explicit cancellation is reported as cancelled, not a natural
playback pass.

## Verification and native acceptance

Cloud source tests cover exact S16LE ownership/decoding, variable chunks, tails,
invalid/stale generations, cancellation and the old-cancel/new-prepare race.
The real controller is also compiled with test-only asynchronous fake AAudio
APIs to inject initialization, format, start, callback, timing, drain, stop and
close failures. Those fakes are not linked into Android and establish no audible
or device behavior. Both source/controller suites run before APK publication.
The existing CPU and Vulkan suites remain required; actual native AAudio linking,
JNI identity, package dependencies, notices, signature and alignment are checked.

**Expected device reports:** 213 unique CPU/GPU PASS checks, followed by ten
separate PCM PASS checks after natural completion: owned source preparation,
actual mono S16/48 kHz format, start/first callback, complete 24,000-frame supply,
endpoint consumption, advancing timestamp, measured unchanged xrun count, no
stream error, quiescent stop and completed close/userdata retirement. A combined
successful report therefore contains 223 PASS identities. Native acceptance of
code18 is pending. Stop/background cancellation may correctly produce CANCELLED
instead of these ten natural-completion outcomes.

Run the CPU/GPU fixtures, tap Play once, wait for its result, then Share report.
Also report whether the brief tone was audible. Endpoint progress and zero xruns
cannot prove audible quality, correct routing or physical speaker latency.
Surface presentation and real streaming audio remain separate subsequent gates;
retail startup still requires minimum kernel/XDK/device service coverage.
