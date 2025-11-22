#include <jni.h>
#include <string>
#include <android/log.h>
#include "archive.h"
#include "huffman.h"
#include "lz77.h"
#include "bitstream.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "KittyPress", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "KittyPress", __VA_ARGS__)

static std::string toStr(JNIEnv* env, jstring js) {
    const char* c = env->GetStringUTFChars(js, nullptr);
    std::string s(c);
    env->ReleaseStringUTFChars(js, c);
    return s;
}

static std::vector<std::string> toStrArray(JNIEnv* env, jobjectArray arr) {
    jsize len = env->GetArrayLength(arr);
    std::vector<std::string> out;
    out.reserve(len);
    for (jsize i = 0; i < len; i++) {
        jstring js = (jstring)env->GetObjectArrayElement(arr, i);
        out.push_back(toStr(env, js));
    }
    return out;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_deepion_kittypress_KittyPressNative_compressNative(
        JNIEnv* env, jobject, jobjectArray inputArray, jstring outPath) {
    try {
        auto inputs = toStrArray(env, inputArray);
        std::string out = toStr(env, outPath);
        LOGI("Compressing to: %s", out.c_str());
        createArchive(inputs, out);
        return 0;
    } catch (const std::exception& e) {
        LOGE("Error: %s", e.what());
        return 1;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_deepion_kittypress_KittyPressNative_decompressNative(
        JNIEnv* env, jobject, jstring archivePath, jstring outputFolder) {
    try {
        std::string in = toStr(env, archivePath);
        std::string out = toStr(env, outputFolder);

        std::string extractedName = extractArchive(in, out);
        return env->NewStringUTF(extractedName.c_str());

    } catch (const std::exception& e) {
        LOGE("Error: %s", e.what());
        return nullptr;
    }
}
