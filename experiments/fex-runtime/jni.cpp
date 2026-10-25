#include "diagnostic.h"
#include "diagnostic_trace.h"
#include <jni.h>
#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>

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
    armxbox::traceLine("JNI entered; beginning CPU fixtures");
    char report[16384];
    armxbox_fex_selftest(report, sizeof report);
    armxbox::traceLine("JNI fixtures returned; creating Java report");
    __android_log_write(ANDROID_LOG_INFO, "ARMXBOX-FEX", report);
    armxbox_fex_set_trace_fd(-1);
    close(fd);
    return env->NewStringUTF(report);
}
