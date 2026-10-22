#include "diagnostic.h"
#include <jni.h>
#include <android/log.h>
extern "C" JNIEXPORT jstring JNICALL Java_org_armxbox_fexprobe_MainActivity_runProbe(JNIEnv* env, jclass) {
  char report[8192];
  armxbox_fex_selftest(report, sizeof report);
  __android_log_write(ANDROID_LOG_INFO, "ARMXBOX-FEX", report);
  return env->NewStringUTF(report);
}
