// A tiny stand-in for Android's jni.h so the PC test can compile. Not used for the real build.
#pragma once
typedef int jint;
struct JavaVM_;
typedef JavaVM_ JavaVM;
#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL
#define JNI_VERSION_1_6 0x00010006
#define JNI_ERR (-1)
