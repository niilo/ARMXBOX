// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Test-only ABI-shaped declarations. Not used in Android builds.
#include <cstdint>
#include <ctime>
extern "C" {
struct AAudioStream;
struct AAudioStreamBuilder;
using aaudio_result_t = int32_t;
using aaudio_direction_t = int32_t;
using aaudio_format_t = int32_t;
using aaudio_sharing_mode_t = int32_t;
using aaudio_performance_mode_t = int32_t;
using aaudio_stream_state_t = int32_t;
using aaudio_data_callback_result_t = int32_t;
constexpr int32_t AAUDIO_OK = 0, AAUDIO_ERROR_DISCONNECTED = -899,
                  AAUDIO_ERROR_UNIMPLEMENTED = -898,
                  AAUDIO_ERROR_INTERNAL = -896,
                  AAUDIO_ERROR_INVALID_STATE = -895,
                  AAUDIO_ERROR_ILLEGAL_ARGUMENT = -880,
                  AAUDIO_ERROR_TIMEOUT = -885, AAUDIO_ERROR_OUT_OF_RANGE = -883,
                  AAUDIO_ERROR_INVALID_FORMAT = -882;
constexpr int32_t AAUDIO_DIRECTION_OUTPUT = 0, AAUDIO_DIRECTION_INPUT = 1;
constexpr int32_t AAUDIO_FORMAT_PCM_I16 = 1, AAUDIO_FORMAT_PCM_FLOAT = 2;
constexpr int32_t AAUDIO_SHARING_MODE_EXCLUSIVE = 0,
                  AAUDIO_SHARING_MODE_SHARED = 1;
constexpr int32_t AAUDIO_PERFORMANCE_MODE_NONE = 10,
                  AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12;
constexpr int32_t AAUDIO_CALLBACK_RESULT_CONTINUE = 0,
                  AAUDIO_CALLBACK_RESULT_STOP = 1;
constexpr int32_t AAUDIO_STREAM_STATE_UNINITIALIZED = 0,
                  AAUDIO_STREAM_STATE_UNKNOWN = 1, AAUDIO_STREAM_STATE_OPEN = 2,
                  AAUDIO_STREAM_STATE_STARTING = 3,
                  AAUDIO_STREAM_STATE_STARTED = 4,
                  AAUDIO_STREAM_STATE_PAUSED = 6,
                  AAUDIO_STREAM_STATE_STOPPING = 9,
                  AAUDIO_STREAM_STATE_STOPPED = 10,
                  AAUDIO_STREAM_STATE_CLOSING = 11,
                  AAUDIO_STREAM_STATE_CLOSED = 12,
                  AAUDIO_STREAM_STATE_DISCONNECTED = 13;
using AAudioStream_dataCallback =
    aaudio_data_callback_result_t (*)(AAudioStream *, void *, void *, int32_t);
using AAudioStream_errorCallback = void (*)(AAudioStream *, void *,
                                            aaudio_result_t);
aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **);
aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder *);
void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *,
                                      aaudio_direction_t);
void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *,
                                        aaudio_sharing_mode_t);
void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *,
                                            aaudio_performance_mode_t);
void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *, aaudio_format_t);
void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *, int32_t);
void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *,
                                         AAudioStream_dataCallback, void *);
void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder *,
                                          AAudioStream_errorCallback, void *);
aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *,
                                               AAudioStream **);
aaudio_format_t AAudioStream_getFormat(AAudioStream *);
int32_t AAudioStream_getChannelCount(AAudioStream *);
int32_t AAudioStream_getSampleRate(AAudioStream *);
aaudio_performance_mode_t AAudioStream_getPerformanceMode(AAudioStream *);
aaudio_sharing_mode_t AAudioStream_getSharingMode(AAudioStream *);
int32_t AAudioStream_getFramesPerBurst(AAudioStream *);
int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream *);
int32_t AAudioStream_getBufferSizeInFrames(AAudioStream *);
int32_t AAudioStream_setBufferSizeInFrames(AAudioStream *, int32_t);
int32_t AAudioStream_getXRunCount(AAudioStream *);
int64_t AAudioStream_getFramesWritten(AAudioStream *);
int64_t AAudioStream_getFramesRead(AAudioStream *);
aaudio_result_t AAudioStream_getTimestamp(AAudioStream *, clockid_t, int64_t *,
                                          int64_t *);
aaudio_stream_state_t AAudioStream_getState(AAudioStream *);
aaudio_result_t AAudioStream_requestStart(AAudioStream *);
aaudio_result_t AAudioStream_requestStop(AAudioStream *);
aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *,
                                                aaudio_stream_state_t,
                                                aaudio_stream_state_t *,
                                                int64_t);
aaudio_result_t AAudioStream_close(AAudioStream *);
const char *AAudio_convertResultToText(aaudio_result_t);
}
