#include "diagnostic.h"
#include "diagnostic_trace.h"
#include "audio/pcm_sink.h"
#include <jni.h>
#include <android/log.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace {
// Controller thread only: persist the complete bounded PCM summary before Java
// allocation/file writes, retaining useful evidence even if those steps fail.
bool persistPcmSummary(int fd, const char *text, size_t capacity)
{
    const size_t length = strnlen(text, capacity);
    size_t offset = 0;
    while (offset < length) {
        const ssize_t written = write(fd, text + offset, length - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        offset += static_cast<size_t>(written);
    }
    int status;
    do {
        status = fsync(fd);
    } while (status < 0 && errno == EINTR);
    return status == 0;
}
} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_org_armxbox_fexprobe_MainActivity_runProbe(JNIEnv *env, jclass,
                                                jstring path)
{
    if (!path)
        return env->NewStringUTF("FAIL missing progress journal path");
    const char *name = env->GetStringUTFChars(path, nullptr);
    if (!name)
        return nullptr; // Preserve the pending JNI exception.
    int fd = open(name, O_WRONLY | O_APPEND | O_CLOEXEC);
    env->ReleaseStringUTFChars(path, name);
    if (fd < 0)
        return env->NewStringUTF("FAIL opening persistent progress journal");
    armxbox_fex_set_trace_fd(fd);
    armxbox::traceLine(
        "JNI entered; beginning CPU and offscreen Vulkan fixtures");
    char report[24576];
    armxbox_fex_selftest(report, sizeof report);
    armxbox::traceLine("JNI fixtures returned; creating Java report");
    __android_log_write(ANDROID_LOG_INFO, "ARMXBOX-FEX", report);
    armxbox_fex_set_trace_fd(-1);
    close(fd);
    return env->NewStringUTF(report);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_armxbox_fexprobe_MainActivity_pcmReady(JNIEnv *, jclass)
{
    return armxbox::pcmReady() ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jlong JNICALL
Java_org_armxbox_fexprobe_MainActivity_beginPcmTest(JNIEnv *, jclass)
{
    return static_cast<jlong>(armxbox::beginPcmTest());
}
extern "C" JNIEXPORT void JNICALL
Java_org_armxbox_fexprobe_MainActivity_cancelPcmTest(JNIEnv *, jclass)
{
    armxbox::cancelPcmTest();
}
extern "C" JNIEXPORT void JNICALL
Java_org_armxbox_fexprobe_MainActivity_abandonPcmTest(JNIEnv *, jclass,
                                                      jlong token)
{
    armxbox::abandonPcmTest(static_cast<uint64_t>(token));
}
extern "C" JNIEXPORT jstring JNICALL
Java_org_armxbox_fexprobe_MainActivity_runPcmTest(JNIEnv *env, jclass,
                                                  jlong token, jstring path)
{
    if (!path) {
        armxbox::abandonPcmTest(static_cast<uint64_t>(token));
        return env->NewStringUTF("FAIL missing PCM progress journal path");
    }
    const char *name = env->GetStringUTFChars(path, nullptr);
    if (!name) {
        armxbox::abandonPcmTest(static_cast<uint64_t>(token));
        return nullptr;
    }
    int fd = open(name, O_WRONLY | O_APPEND | O_CLOEXEC);
    env->ReleaseStringUTFChars(path, name);
    if (fd < 0) {
        armxbox::abandonPcmTest(static_cast<uint64_t>(token));
        return env->NewStringUTF(
            "FAIL opening persistent PCM progress journal");
    }
    armxbox_fex_set_trace_fd(fd);
    armxbox::traceLine("JNI entered; beginning explicit finite PCM test");
    const auto result =
        armxbox::runPcmTest(static_cast<uint64_t>(token), armxbox::traceLine);
    armxbox::traceLine(
        "JNI PCM controller returned; persisting complete PCM summary");
    const bool persisted =
        persistPcmSummary(fd, result.report, sizeof result.report);
    __android_log_write(ANDROID_LOG_INFO, "ARMXBOX-FEX", result.report);
    armxbox_fex_set_trace_fd(-1);
    close(fd);
    if (!persisted) {
        char warning[sizeof result.report + 128];
        std::snprintf(
            warning, sizeof warning,
            "%s\nWARNING: PCM progress summary could not be persisted.\n",
            result.report);
        return env->NewStringUTF(warning);
    }
    return env->NewStringUTF(result.report);
}
