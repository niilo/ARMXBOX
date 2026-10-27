// SPDX-License-Identifier: GPL-2.0-or-later
#include "pcm_sink.h"
#include "pcm_source.h"
#include <aaudio/AAudio.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <time.h>
#ifndef ARMXBOX_PCM_DRAIN_TIMEOUT_NS
#define ARMXBOX_PCM_DRAIN_TIMEOUT_NS 3000000000LL
#endif
#ifndef ARMXBOX_PCM_STOP_TIMEOUT_NS
#define ARMXBOX_PCM_STOP_TIMEOUT_NS 2000000000LL
#endif
#ifndef ARMXBOX_PCM_POLL_US
#define ARMXBOX_PCM_POLL_US 2000
#endif
namespace armxbox {
namespace {
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
static_assert(std::atomic<int32_t>::is_always_lock_free);
struct Registry {
    std::mutex mutex;
    std::array<uint8_t, 128> seed{};
    std::atomic<uint64_t> cancelEpoch{ 1 };
    uint64_t nextToken = 1, activeToken = 0, reservedEpoch = 0;
    bool ready = false, running = false, quarantined = false;
} registry;
uint64_t monotonicNs() noexcept
{
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    return uint64_t(now.tv_sec) * 1000000000ULL + uint64_t(now.tv_nsec);
}
struct Session {
    PcmSource source;
    uint64_t token = 0, epoch = 0;
    AAudioStream *stream = nullptr;
    std::atomic<uint32_t> inFlight{ 0 };
    std::atomic<int32_t> callbackError{ AAUDIO_OK };
};
// If close fails, retaining one bounded session is safer than freeing callback
// userdata whose stream lifetime is unknown. Further playback requires restart.
Session *quarantinedSession = nullptr;
bool cancelled(const Session &session) noexcept
{
    return registry.cancelEpoch.load(std::memory_order_acquire) !=
           session.epoch;
}
aaudio_data_callback_result_t dataCallback(AAudioStream *, void *opaque,
                                           void *audio, int32_t frames)
{
    auto &session = *static_cast<Session *>(opaque);
    session.inFlight.fetch_add(1, std::memory_order_acq_rel);
    if (cancelled(session))
        session.source.cancel(session.token);
    const auto result = session.source.render(static_cast<int16_t *>(audio),
                                              frames, session.token);
    if (!result.valid) {
        int32_t noError = AAUDIO_OK;
        session.callbackError.compare_exchange_strong(
            noError, AAUDIO_ERROR_OUT_OF_RANGE, std::memory_order_relaxed);
    }
    session.inFlight.fetch_sub(1, std::memory_order_release);
    return result.cancelled || !result.valid ? AAUDIO_CALLBACK_RESULT_STOP :
                                               AAUDIO_CALLBACK_RESULT_CONTINUE;
}
void errorCallback(AAudioStream *, void *opaque, aaudio_result_t error)
{
    auto &session = *static_cast<Session *>(opaque);
    session.inFlight.fetch_add(1, std::memory_order_acq_rel);
    int32_t noError = AAUDIO_OK;
    session.callbackError.compare_exchange_strong(noError, error,
                                                  std::memory_order_relaxed);
    session.source.cancel(session.token);
    session.inFlight.fetch_sub(1, std::memory_order_release);
}
struct Text {
    char *out;
    size_t capacity, used = 0;
    __attribute__((format(printf, 2, 3))) void line(const char *format,
                                                    ...) noexcept
    {
        if (used + 1 >= capacity)
            return;
        va_list args;
        va_start(args, format);
        const int count =
            std::vsnprintf(out + used, capacity - used, format, args);
        va_end(args);
        if (count > 0)
            used += size_t(count) < capacity - used ? size_t(count) :
                                                      capacity - used - 1;
    }
};
constexpr const char *Checks[] = {
    "PCM owned guest snapshot expands to finite immutable source",
    "PCM actual stream format is mono S16 at 48000 Hz",
    "PCM asynchronous start reaches first data callback",
    "PCM callback supplies all 24000 source frames without invalid access",
    "PCM endpoint consumes the finite content before stop",
    "PCM endpoint timestamp advances while started",
    "PCM xrun count remains measurable and unchanged",
    "PCM controller and error callback report no stream failure",
    "PCM stop reaches a quiescent stream state",
    "PCM close completes before callback userdata retirement",
};
struct Observation {
    std::array<bool, std::size(Checks)> passed{};
    aaudio_result_t apiError = AAUDIO_OK,
                    timestampStatus = AAUDIO_ERROR_INVALID_STATE;
    int32_t actualRate = 0, actualChannels = 0, actualFormat = 0;
    int32_t performance = 0, sharing = 0, burst = 0, capacity = 0, buffer = 0;
    int32_t xrunBefore = -1, xrunAfter = -1;
    int64_t framesWritten = 0, framesRead = 0, contentRead = 0;
    int64_t firstEndpoint = -1, lastEndpoint = -1, firstEndpointTime = 0,
            lastEndpointTime = 0;
    uint64_t startNs = 0, firstCallbackNs = 0, stopNs = 0, closeNs = 0;
    bool userCancelled = false, closed = false, closeFailed = false,
         completed = false;
    const char *stage = "reservation";
    const char *firstErrorStage = "none";
};
void stage(Observation &observation, const char *next,
           void (*log)(const char *)) noexcept
{
    observation.stage = next;
    if (log) {
        try {
            log(next);
        } catch (...) {
            if (observation.apiError == AAUDIO_OK) {
                observation.apiError = AAUDIO_ERROR_INTERNAL;
                observation.firstErrorStage = observation.stage;
            }
        }
    }
}
void recordError(Observation &observation, aaudio_result_t error) noexcept
{
    if (error != AAUDIO_OK && observation.apiError == AAUDIO_OK) {
        observation.apiError = error;
        observation.firstErrorStage = observation.stage;
    }
}
bool quiescent(aaudio_stream_state_t state) noexcept
{
    return state == AAUDIO_STREAM_STATE_STOPPED ||
           state == AAUDIO_STREAM_STATE_OPEN ||
           state == AAUDIO_STREAM_STATE_PAUSED ||
           state == AAUDIO_STREAM_STATE_DISCONNECTED;
}
void stopAndClose(Session &session, Observation &observation,
                  void (*log)(const char *)) noexcept
{
    if (!session.stream)
        return;
    stage(observation, "PCM: request stop and wait for stream quiescence", log);
    const uint64_t beforeStop = monotonicNs();
    session.source.cancel(session.token);
    const auto initialState = AAudioStream_getState(session.stream);
    if (!quiescent(initialState) &&
        initialState != AAUDIO_STREAM_STATE_STOPPING) {
        const auto stop = AAudioStream_requestStop(session.stream);
        const auto afterRequest = AAudioStream_getState(session.stream);
        // Returning STOP in the data callback can race the controller request.
        // Only INVALID_STATE with an already stopping/quiescent state is
        // benign.
        if (!(stop == AAUDIO_ERROR_INVALID_STATE &&
              (afterRequest == AAUDIO_STREAM_STATE_STOPPING ||
               quiescent(afterRequest))))
            recordError(observation, stop);
    }
    auto state = AAudioStream_getState(session.stream);
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::nanoseconds(ARMXBOX_PCM_STOP_TIMEOUT_NS);
    while (!quiescent(state) && std::chrono::steady_clock::now() < deadline) {
        aaudio_stream_state_t next = state;
        const auto wait = AAudioStream_waitForStateChange(session.stream, state,
                                                          &next, 20000000);
        if (wait != AAUDIO_OK && wait != AAUDIO_ERROR_TIMEOUT) {
            recordError(observation, wait);
            break;
        }
        state = AAudioStream_getState(session.stream);
    }
    observation.passed[8] = quiescent(state);
    if (!observation.passed[8])
        recordError(observation, AAUDIO_ERROR_TIMEOUT);
    observation.stopNs = monotonicNs() - beforeStop;
    observation.framesWritten = AAudioStream_getFramesWritten(session.stream);
    observation.framesRead = AAudioStream_getFramesRead(session.stream);
    observation.xrunAfter = AAudioStream_getXRunCount(session.stream);
    stage(observation,
          "PCM: close stream while callback userdata remains owned", log);
    const uint64_t beforeClose = monotonicNs();
    const auto close = AAudioStream_close(session.stream);
    observation.closeNs = monotonicNs() - beforeClose;
    recordError(observation, close);
    observation.closed = close == AAUDIO_OK;
    const bool callbacksQuiescent =
        session.inFlight.load(std::memory_order_acquire) == 0;
    observation.closeFailed = !observation.closed || !callbacksQuiescent;
    if (observation.closed) {
        session.stream = nullptr;
        observation.passed[9] = callbacksQuiescent;
        if (!callbacksQuiescent)
            recordError(observation, AAUDIO_ERROR_INVALID_STATE);
    }
}
void play(Session &session, const std::array<uint8_t, 128> &seed,
          Observation &observation, void (*log)(const char *))
{
    observation.passed[0] = session.source.prepare(seed, session.token);
    if (!observation.passed[0]) {
        recordError(observation, AAUDIO_ERROR_ILLEGAL_ARGUMENT);
        return;
    }
    if (cancelled(session)) {
        observation.userCancelled = true;
        return;
    }
    stage(observation,
          "PCM: create AAudio builder and request mono S16 48000 Hz", log);
    AAudioStreamBuilder *builder = nullptr;
    const auto make = AAudio_createStreamBuilder(&builder);
    recordError(observation, make);
    if (make != AAUDIO_OK || !builder) {
        if (builder)
            recordError(observation, AAudioStreamBuilder_delete(builder));
        if (make == AAUDIO_OK)
            recordError(observation, AAUDIO_ERROR_INTERNAL);
        return;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder,
                                           AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setDataCallback(builder, dataCallback, &session);
    AAudioStreamBuilder_setErrorCallback(builder, errorCallback, &session);
    if (cancelled(session)) {
        observation.userCancelled = true;
        recordError(observation, AAudioStreamBuilder_delete(builder));
        return;
    }
    stage(observation, "PCM: open AAudio output stream", log);
    const auto open = AAudioStreamBuilder_openStream(builder, &session.stream);
    const auto destroy = AAudioStreamBuilder_delete(builder);
    recordError(observation, open);
    recordError(observation, destroy);
    if (open != AAUDIO_OK || !session.stream || destroy != AAUDIO_OK)
        return;
    observation.actualRate = AAudioStream_getSampleRate(session.stream);
    observation.actualChannels = AAudioStream_getChannelCount(session.stream);
    observation.actualFormat = AAudioStream_getFormat(session.stream);
    observation.performance = AAudioStream_getPerformanceMode(session.stream);
    observation.sharing = AAudioStream_getSharingMode(session.stream);
    observation.burst = AAudioStream_getFramesPerBurst(session.stream);
    observation.capacity =
        AAudioStream_getBufferCapacityInFrames(session.stream);
    observation.buffer = AAudioStream_getBufferSizeInFrames(session.stream);
    observation.passed[1] = observation.actualRate == 48000 &&
                            observation.actualChannels == 1 &&
                            observation.actualFormat == AAUDIO_FORMAT_PCM_I16;
    if (!observation.passed[1]) {
        recordError(observation, AAUDIO_ERROR_INVALID_FORMAT);
        return;
    }
    if (observation.burst > 0 && observation.capacity > 0 &&
        observation.burst <= observation.capacity / 2) {
        const auto size = AAudioStream_setBufferSizeInFrames(
            session.stream, 2 * observation.burst);
        if (size < 0) {
            recordError(observation, size);
            return;
        }
        observation.buffer = size;
    }
    observation.xrunBefore = AAudioStream_getXRunCount(session.stream);
    if (cancelled(session)) {
        observation.userCancelled = true;
        return;
    }
    stage(observation, "PCM: request start; callback owns finite source cursor",
          log);
    observation.startNs = monotonicNs();
    const auto start = AAudioStream_requestStart(session.stream);
    recordError(observation, start);
    if (start != AAUDIO_OK)
        return;
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::nanoseconds(ARMXBOX_PCM_DRAIN_TIMEOUT_NS);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled(session)) {
            observation.userCancelled = true;
            break;
        }
        const auto error =
            session.callbackError.load(std::memory_order_acquire);
        if (error != AAUDIO_OK) {
            recordError(observation, error);
            break;
        }
        const auto streamState = AAudioStream_getState(session.stream);
        if (streamState == AAUDIO_STREAM_STATE_DISCONNECTED) {
            recordError(observation, AAUDIO_ERROR_DISCONNECTED);
            break;
        }
        observation.framesRead = AAudioStream_getFramesRead(session.stream);
        observation.framesWritten =
            AAudioStream_getFramesWritten(session.stream);
        if (observation.framesRead < 0 || observation.framesWritten < 0) {
            recordError(observation,
                        aaudio_result_t(observation.framesRead < 0 ?
                                            observation.framesRead :
                                            observation.framesWritten));
            break;
        }
        if (streamState == AAUDIO_STREAM_STATE_STARTED) {
            int64_t frame = -1, time = 0;
            observation.timestampStatus = AAudioStream_getTimestamp(
                session.stream, CLOCK_MONOTONIC, &frame, &time);
            if (observation.timestampStatus == AAUDIO_OK && frame >= 0 &&
                time > 0) {
                if (observation.firstEndpoint < 0) {
                    observation.firstEndpoint = frame;
                    observation.firstEndpointTime = time;
                }
                observation.lastEndpoint = frame;
                observation.lastEndpointTime = time;
                observation.passed[5] = frame > observation.firstEndpoint &&
                                        time > observation.firstEndpointTime;
            }
        }
        const auto snapshot = session.source.snapshot();
        if (snapshot.callbacks && !observation.firstCallbackNs)
            observation.firstCallbackNs =
                monotonicNs(); // Controller observation upper bound, not RT
                               // timing.
        if (snapshot.copiedFrames == 24000 && observation.framesRead >= 24000) {
            observation.completed = true;
            observation.contentRead = observation.framesRead;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::microseconds(ARMXBOX_PCM_POLL_US));
    }
    if (!observation.completed && !observation.userCancelled &&
        observation.apiError == AAUDIO_OK)
        recordError(observation, AAUDIO_ERROR_TIMEOUT);
}
} // namespace
bool publishPcm(std::span<const uint8_t> pcm) noexcept
{
    if (pcm.size() != 128)
        return false;
    try {
        std::lock_guard<std::mutex> lock(registry.mutex);
        if (registry.activeToken || registry.quarantined)
            return false;
        std::memcpy(registry.seed.data(), pcm.data(), 128);
        registry.ready = true;
        return true;
    } catch (...) {
        return false;
    }
}
bool pcmReady() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(registry.mutex);
        return registry.ready && !registry.quarantined && !registry.activeToken;
    } catch (...) {
        return false;
    }
}
uint64_t beginPcmTest() noexcept
{
    try {
        const auto before =
            registry.cancelEpoch.load(std::memory_order_acquire);
        std::lock_guard<std::mutex> lock(registry.mutex);
        if (!registry.ready || registry.activeToken || registry.quarantined ||
            !registry.nextToken)
            return 0;
        const auto token = registry.nextToken++;
        registry.activeToken = token;
        registry.reservedEpoch = before;
        if (registry.cancelEpoch.load(std::memory_order_acquire) != before) {
            registry.activeToken = 0;
            return 0;
        }
        return token;
    } catch (...) {
        return 0;
    }
}
void cancelPcmTest() noexcept
{
    registry.cancelEpoch.fetch_add(1, std::memory_order_acq_rel);
}
void abandonPcmTest(uint64_t token) noexcept
{
    try {
        std::lock_guard<std::mutex> lock(registry.mutex);
        if (token && token == registry.activeToken && !registry.running)
            registry.activeToken = 0;
    } catch (...) {
    }
}
PcmTestResults runPcmTest(uint64_t token, void (*log)(const char *)) noexcept
{
    PcmTestResults result;
    Text text{ result.report, sizeof result.report };
    Observation observation;
    std::unique_ptr<Session> session;
    std::array<uint8_t, 128> seed{};
    bool ownedReservation = false;
    try {
        uint64_t reservedEpoch = 0;
        {
            std::lock_guard<std::mutex> lock(registry.mutex);
            if (!token || token != registry.activeToken || registry.running ||
                registry.quarantined) {
                text.line("Result: PCM REJECTED; stale, missing or busy "
                          "reservation\n");
                return result;
            }
            ownedReservation = true;
            registry.running = true;
            reservedEpoch = registry.reservedEpoch;
            seed = registry.seed;
        }
        session = std::make_unique<Session>();
        session->token = token;
        session->epoch = reservedEpoch;
        play(*session, seed, observation, log);
    } catch (...) {
        recordError(observation, AAUDIO_ERROR_INTERNAL);
    }
    if (session) {
        stopAndClose(*session, observation, log);
        const auto snapshot = session->source.snapshot();
        observation.passed[2] =
            observation.startNs &&
            observation.firstCallbackNs >= observation.startNs &&
            snapshot.callbacks > 0;
        observation.passed[3] = snapshot.copiedFrames == 24000 &&
                                !snapshot.invalidCallbacks &&
                                !snapshot.staleCallbacks;
        observation.passed[4] =
            observation.completed && observation.contentRead >= 24000;
        observation.passed[6] = observation.xrunBefore >= 0 &&
                                observation.xrunAfter == observation.xrunBefore;
        recordError(observation,
                    session->callbackError.load(std::memory_order_acquire));
        observation.passed[7] = observation.apiError == AAUDIO_OK;
        result.cancelled = observation.userCancelled || cancelled(*session);
        text.line("ARMXBOX PCM phase0.12c; direct AAudio sink; no Xbox "
                  "mixer/APU/DSP or game FPS claim\n");
        text.line("Source: owned 64-frame guest S16LE/mono/48000 Hz -> "
                  "host-precomputed 24000 frames (0.5s); deliberate 3 kHz "
                  "diagnostic sawtooth\n");
        text.line("Requested: OUTPUT mono S16 48000 Hz SHARED LOW_LATENCY; "
                  "actual rate=%d channels=%d format=%d sharing=%d "
                  "performance=%d burst=%d capacity=%d selectedBuffer=%d\n",
                  observation.actualRate, observation.actualChannels,
                  observation.actualFormat, observation.sharing,
                  observation.performance, observation.burst,
                  observation.capacity, observation.buffer);
        text.line("Counters: source=%llu callbacks=%llu offered=%llu "
                  "copied=%llu zeroTail=%llu maxCallback=%u invalid=%llu "
                  "stale=%llu inFlight=%u\n",
                  (unsigned long long)snapshot.position,
                  (unsigned long long)snapshot.callbacks,
                  (unsigned long long)snapshot.offeredFrames,
                  (unsigned long long)snapshot.copiedFrames,
                  (unsigned long long)snapshot.silentFrames,
                  snapshot.maxCallbackFrames,
                  (unsigned long long)snapshot.invalidCallbacks,
                  (unsigned long long)snapshot.staleCallbacks,
                  session->inFlight.load(std::memory_order_acquire));
        text.line("Endpoint: framesWritten=%lld framesRead=%lld "
                  "contentReadBeforeStop=%lld timestampStatus=%d "
                  "first=%lld@%lld last=%lld@%lld xrunBefore=%d xrunAfter=%d\n",
                  (long long)observation.framesWritten,
                  (long long)observation.framesRead,
                  (long long)observation.contentRead,
                  observation.timestampStatus,
                  (long long)observation.firstEndpoint,
                  (long long)observation.firstEndpointTime,
                  (long long)observation.lastEndpoint,
                  (long long)observation.lastEndpointTime,
                  observation.xrunBefore, observation.xrunAfter);
        const auto firstLatency =
            observation.firstCallbackNs >= observation.startNs ?
                observation.firstCallbackNs - observation.startNs :
                0;
        text.line("Latency ns: startToObservedFirstCallbackUpperBound=%llu "
                  "stopToQuiescent=%llu close=%llu; APIerror=%d "
                  "firstErrorStage=%s finalStage=%s\n",
                  (unsigned long long)firstLatency,
                  (unsigned long long)observation.stopNs,
                  (unsigned long long)observation.closeNs, observation.apiError,
                  observation.firstErrorStage, observation.stage);
        text.line("First callback latency is controller-observed (poll %d us), "
                  "not an exact callback timestamp.\n",
                  ARMXBOX_PCM_POLL_US);
        bool all = true;
        for (unsigned i = 0; i < observation.passed.size(); ++i) {
            text.line("%s %s\n",
                      observation.passed[i] ?
                          "PASS" :
                          (result.cancelled ? "CANCELLED" : "FAIL"),
                      Checks[i]);
            all &= observation.passed[i];
        }
        result.passed = all && !result.cancelled;
        text.line("Result: PCM %s; endpoint progress and zero xruns do not "
                  "establish audible quality; close/driver teardown is not "
                  "universally bounded\n",
                  result.cancelled ? "CANCELLED" :
                                     (result.passed ? "PASS" : "FAIL"));
    }
    if (!session)
        text.line("Result: PCM FAIL; controller session allocation failed; "
                  "APIerror=%d\n",
                  observation.apiError);
    if (ownedReservation) {
        try {
            std::lock_guard<std::mutex> lock(registry.mutex);
            if (observation.closeFailed && session) {
                registry.quarantined = true;
                quarantinedSession = session.release();
                text.line("PCM close failed: callback userdata retained; "
                          "restart the probe before further playback\n");
            }
            registry.running = false;
            registry.activeToken = 0;
        } catch (...) {
            // Preserve userdata on exceptional registry failure as well.
            if (session && session->stream)
                (void)session.release();
        }
    }
    return result;
}
} // namespace armxbox
