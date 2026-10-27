// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only asynchronous AAudio model exercises the real native controller.
// This establishes orchestration/lifetime behavior, not audible playback.
#include "pcm_sink.h"
#include "pcm_source.h"
#include <aaudio/AAudio.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
namespace {
enum class Mode {
    Happy,
    Create,
    Open,
    Format,
    Rate,
    Channels,
    Start,
    CallbackError,
    InvalidCallback,
    NoTimestamp,
    StalledEndpoint,
    Stop,
    Close,
    MidCancel,
    Xrun,
    Buffer,
    StopRace
};
Mode mode{};
std::atomic<unsigned> opens{}, starts{}, closes{}, callbacks{}, builders{};
std::atomic<bool> stopRaceSeen{};
bool badRequest = false;
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
} // namespace
struct AAudioStreamBuilder {
    aaudio_direction_t direction = -1;
    aaudio_sharing_mode_t sharing = -1;
    aaudio_performance_mode_t performance = -1;
    aaudio_format_t format = -1;
    int32_t channels = 0, rate = 0;
    AAudioStream_dataCallback data{};
    AAudioStream_errorCallback error{};
    void *dataContext{}, *errorContext{};
};
struct AAudioStream {
    AAudioStreamBuilder options;
    std::atomic<int32_t> state{ AAUDIO_STREAM_STATE_OPEN };
    std::atomic<bool> stop{};
    std::atomic<int64_t> written{}, read{};
    std::thread worker;
};
AAudioStream *quarantined = nullptr;
extern "C" {
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **out)
{
    if (mode == Mode::Create)
        return AAUDIO_ERROR_INTERNAL;
    *out = new AAudioStreamBuilder;
    ++builders;
    return AAUDIO_OK;
}
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *b)
{
    if (b) {
        delete b;
        --builders;
    }
    return AAUDIO_OK;
}
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *b,
                                      aaudio_direction_t v)
{
    b->direction = v;
}
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *b,
                                        aaudio_sharing_mode_t v)
{
    b->sharing = v;
}
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *b,
                                            aaudio_performance_mode_t v)
{
    b->performance = v;
}
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b, aaudio_format_t v)
{
    b->format = v;
}
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b, int32_t v)
{
    b->channels = v;
}
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *b, int32_t v)
{
    b->rate = v;
}
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b,
                                         AAudioStream_dataCallback f, void *p)
{
    b->data = f;
    b->dataContext = p;
}
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *b,
                                          AAudioStream_errorCallback f, void *p)
{
    b->error = f;
    b->errorContext = p;
}
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b,
                                               AAudioStream **out)
{
    ++opens;
    badRequest |= b->direction != AAUDIO_DIRECTION_OUTPUT ||
                  b->sharing != AAUDIO_SHARING_MODE_SHARED ||
                  b->performance != AAUDIO_PERFORMANCE_MODE_LOW_LATENCY ||
                  b->format != AAUDIO_FORMAT_PCM_I16 || b->channels != 1 ||
                  b->rate != 48000 || !b->data || !b->error;
    if (mode == Mode::Open)
        return AAUDIO_ERROR_INTERNAL;
    *out = new AAudioStream;
    (*out)->options = *b;
    return AAUDIO_OK;
}
aaudio_format_t AAudioStream_getFormat(AAudioStream *)
{
    return mode == Mode::Format ? AAUDIO_FORMAT_PCM_FLOAT :
                                  AAUDIO_FORMAT_PCM_I16;
}
int32_t AAudioStream_getChannelCount(AAudioStream *)
{
    return mode == Mode::Channels ? 2 : 1;
}
int32_t AAudioStream_getSampleRate(AAudioStream *)
{
    return mode == Mode::Rate ? 44100 : 48000;
}
aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *)
{
    return AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
}
aaudio_sharing_mode_t AAudioStream_getSharingMode(AAudioStream *)
{
    return AAUDIO_SHARING_MODE_SHARED;
}
int32_t AAudioStream_getFramesPerBurst(AAudioStream *)
{
    return 192;
}
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *)
{
    return 1536;
}
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *)
{
    return 384;
}
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *, int32_t v)
{
    return mode == Mode::Buffer ? AAUDIO_ERROR_INTERNAL : v;
}
int32_t AAudioStream_getXRunCount(AAudioStream *s)
{
    return mode == Mode::Xrun && s->written.load() ? 1 : 0;
}
int64_t AAudioStream_getFramesWritten(AAudioStream *s)
{
    return s->written.load();
}
int64_t AAudioStream_getFramesRead(AAudioStream *s)
{
    return s->read.load();
}
aaudio_result_t AAudioStream_getTimestamp(AAudioStream *s, clockid_t clock,
                                          int64_t *frame, int64_t *time)
{
    if (mode == Mode::NoTimestamp ||
        s->state.load() != AAUDIO_STREAM_STATE_STARTED)
        return AAUDIO_ERROR_INVALID_STATE;
    timespec stamp{};
    clock_gettime(clock, &stamp);
    *frame = s->read.load();
    *time = int64_t(stamp.tv_sec) * 1000000000 + stamp.tv_nsec;
    return AAUDIO_OK;
}
aaudio_stream_state_t AAudioStream_getState(AAudioStream *s)
{
    return s->state.load();
}
aaudio_result_t AAudioStream_requestStart(AAudioStream *s)
{
    ++starts;
    if (mode == Mode::Start)
        return AAUDIO_ERROR_INTERNAL;
    s->state = AAUDIO_STREAM_STATE_STARTED;
    s->worker = std::thread([s] {
        std::array<int16_t, 20000> output{};
        while (!s->stop.load()) {
            if (mode == Mode::CallbackError) {
                s->state = AAUDIO_STREAM_STATE_DISCONNECTED;
                s->options.error(s, s->options.errorContext,
                                 AAUDIO_ERROR_DISCONNECTED);
                break;
            }
            int32_t frames = mode == Mode::InvalidCallback ? 20000 : 384;
            auto result = s->options.data(s, s->options.dataContext,
                                          output.data(), frames);
            ++callbacks;
            if (mode == Mode::MidCancel)
                armxbox::cancelPcmTest();
            if (result == AAUDIO_CALLBACK_RESULT_STOP) {
                if (mode != Mode::StopRace)
                    s->state = AAUDIO_STREAM_STATE_STOPPED;
                break;
            }
            s->written.fetch_add(frames);
            if (mode != Mode::StalledEndpoint)
                s->read.fetch_add(frames);
            std::this_thread::sleep_for(std::chrono::microseconds(80));
        }
    });
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_requestStop(AAudioStream *s)
{
    s->stop = true;
    if (mode == Mode::StopRace) {
        stopRaceSeen = true;
        s->state = AAUDIO_STREAM_STATE_STOPPING;
        return AAUDIO_ERROR_INVALID_STATE;
    }
    s->state = AAUDIO_STREAM_STATE_STOPPED;
    return mode == Mode::Stop ? AAUDIO_ERROR_INTERNAL : AAUDIO_OK;
}
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *s,
                                                aaudio_stream_state_t,
                                                aaudio_stream_state_t *next,
                                                int64_t)
{
    if (mode == Mode::StopRace && s->state == AAUDIO_STREAM_STATE_STOPPING)
        s->state = AAUDIO_STREAM_STATE_STOPPED;
    *next = s->state.load();
    return AAUDIO_OK;
}
aaudio_result_t AAudioStream_close(AAudioStream *s)
{
    ++closes;
    s->stop = true;
    if (s->worker.joinable())
        s->worker.join();
    if (mode == Mode::Close) {
        quarantined = s;
        return AAUDIO_ERROR_INTERNAL;
    }
    delete s;
    return AAUDIO_OK;
}
const char *AAudio_convertResultToText(aaudio_result_t code)
{
    return code == AAUDIO_OK ? "OK" : "injected AAudio failure";
}
}
namespace {
void reset(Mode next)
{
    mode = next;
    opens = 0;
    starts = 0;
    closes = 0;
    callbacks = 0;
    badRequest = false;
    stopRaceSeen = false;
}
std::array<uint8_t, 128> seed()
{
    std::array<uint8_t, 128> bytes;
    for (unsigned i = 0; i < 64; ++i) {
        const auto sample = uint16_t((int(i % 16) - 8) * 512);
        bytes[2 * i] = sample;
        bytes[2 * i + 1] = sample >> 8;
    }
    return bytes;
}
armxbox::PcmTestResults run(Mode next)
{
    reset(next);
    auto bytes = seed();
    require(armxbox::publishPcm(bytes), "publish owned PCM source");
    bytes.fill(0x5d);
    require(armxbox::pcmReady(), "source not ready before reservation");
    const auto token = armxbox::beginPcmTest();
    require(token && !armxbox::beginPcmTest(),
            "concurrent reservation was not refused");
    require(!armxbox::publishPcm(seed()),
            "publication replaced a reserved source");
    auto result = armxbox::runPcmTest(token, nullptr);
    require(!badRequest,
            "AAudio settings/callbacks did not match request contract");
    require(builders.load() == 0, "builder leaked after run");
    if (next != Mode::Close)
        require(armxbox::pcmReady(), "safe run did not release reservation");
    return result;
}
} // namespace
int main()
{
    try {
        require(!armxbox::pcmReady() && !armxbox::beginPcmTest(),
                "missing source permitted playback");
        std::array<uint8_t, 127> shortSeed{};
        require(!armxbox::publishPcm(shortSeed), "short seed admitted");
        reset(Mode::Happy);
        require(armxbox::publishPcm(seed()), "publish cancellation seed");
        auto token = armxbox::beginPcmTest();
        require(token, "reserve cancelled test");
        armxbox::cancelPcmTest();
        auto cancelled = armxbox::runPcmTest(token, nullptr);
        require(cancelled.cancelled && !cancelled.passed && starts == 0 &&
                    opens == 0,
                "cancellation before worker entry was reset or started audio");
        require(armxbox::pcmReady(), "cancelled reservation not released");
        std::puts("PASS PCM controller: pre-worker cancellation stays latched "
                  "without opening audio");
        token = armxbox::beginPcmTest();
        require(token, "reserve thread-start failure");
        armxbox::abandonPcmTest(token);
        auto stale = armxbox::runPcmTest(token, nullptr);
        require(!stale.passed && !starts && !opens && armxbox::pcmReady(),
                "abandoned token executed playback");
        std::puts("PASS PCM controller: abandoned and concurrent reservations "
                  "cannot start playback");
        for (auto entry : std::array{
                 std::pair{ Mode::Happy, "happy finite source and endpoint" },
                 std::pair{ Mode::StopRace,
                            "callback stop races controller request" },
                 std::pair{ Mode::Create, "builder creation failure" },
                 std::pair{ Mode::Open, "open failure" },
                 std::pair{ Mode::Format, "wrong actual format" },
                 std::pair{ Mode::Rate, "wrong actual sample rate" },
                 std::pair{ Mode::Channels, "wrong actual channels" },
                 std::pair{ Mode::Start, "asynchronous start failure" },
                 std::pair{ Mode::CallbackError, "callback disconnect" },
                 std::pair{ Mode::InvalidCallback, "oversized callback" },
                 std::pair{ Mode::NoTimestamp,
                            "unavailable endpoint timestamp" },
                 std::pair{ Mode::StalledEndpoint, "endpoint fails to drain" },
                 std::pair{ Mode::Stop, "stop failure" },
                 std::pair{ Mode::Xrun, "underrun counter increases" },
                 std::pair{ Mode::Buffer, "buffer configuration failure" } }) {
            auto result = run(entry.first);
            if (entry.first == Mode::Happy || entry.first == Mode::StopRace) {
                if (!result.passed)
                    std::fprintf(stderr, "%s\n", result.report);
                require(result.passed && !result.cancelled,
                        "happy controller failed");
                require(callbacks > 0 && opens == 1 && closes == 1 &&
                            starts == 1,
                        "happy call counts");
                if (entry.first == Mode::StopRace)
                    require(stopRaceSeen, "stop-race failure was not injected");
            } else {
                require(!result.passed && !result.cancelled,
                        "injected failure silently passed");
                if (entry.first == Mode::Format || entry.first == Mode::Rate ||
                    entry.first == Mode::Channels)
                    require(starts == 0 && callbacks == 0,
                            "format mismatch reached callback");
            }
            std::printf("PASS PCM controller: %s\n", entry.second);
        }
        auto midCancel = run(Mode::MidCancel);
        require(
            midCancel.cancelled && !midCancel.passed && starts == 1 &&
                callbacks > 0,
            "cancellation during playback did not stop and release controller");
        std::puts("PASS PCM controller: cancellation during active callback "
                  "playback");
        auto closeFailure = run(Mode::Close);
        require(!closeFailure.passed && quarantined && !armxbox::pcmReady() &&
                    !armxbox::beginPcmTest(),
                "failed close did not quarantine userdata and reject restart");
        // A late call through retained userdata must remain safe even when the
        // modeled close failed. This runs under ASan in the dedicated test.
        std::array<int16_t, 32> late;
        late.fill(0x5d5d);
        auto lateResult = quarantined->options.data(
            quarantined, quarantined->options.dataContext, late.data(),
            late.size());
        require(lateResult == AAUDIO_CALLBACK_RESULT_STOP,
                "quarantined callback did not stop");
        for (auto value : late)
            require(value == 0, "quarantined callback did not silence output");
        std::puts("PASS PCM controller: failed close retains late-callback "
                  "userdata and rejects restart");
        std::puts("PASS PCM controller: 19 asynchronous API/lifetime/failure "
                  "controls; no audible playback proof");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL PCM controller: %s\n", error.what());
        return 1;
    }
}
