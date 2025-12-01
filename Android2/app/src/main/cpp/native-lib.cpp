#include <jni.h>
#include <string>
#include <android/log.h>
#include "archive.h"
#include "huffman.h"
#include "lz77.h"
#include "bitstream.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "KittyPress", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "KittyPress", __VA_ARGS__)

// Safe conversion: handles null jstring
static std::string toStr(JNIEnv* env, jstring js) {
    if (js == nullptr) return std::string();
    const char* c = env->GetStringUTFChars(js, nullptr);
    if (!c) return std::string(); // defensive
    std::string s(c);
    env->ReleaseStringUTFChars(js, c);
    return s;
}

// Safe array conversion: deletes local refs to avoid local-ref table overflow
static std::vector<std::string> toStrArray(JNIEnv* env, jobjectArray arr) {
    std::vector<std::string> out;
    if (arr == nullptr) return out;

    jsize len = env->GetArrayLength(arr);
    out.reserve(static_cast<size_t>(len));

    for (jsize i = 0; i < len; ++i) {
        jstring js = (jstring) env->GetObjectArrayElement(arr, i);
        // defensive: check null
        if (js == nullptr) {
            LOGE("toStrArray: null string at index %d", i);
            // still push empty so indices match if you rely on order
            out.emplace_back();
            continue;
        }
        out.push_back(toStr(env, js));
        // IMPORTANT: release the local reference created by GetObjectArrayElement
        env->DeleteLocalRef(js);
    }

    return out;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_deepion_kittypress_KittyPressNative_compressNative(
        JNIEnv* env, jobject, jobjectArray inputArray, jstring outPath) {
    try {
        auto inputs = toStrArray(env, inputArray);
        std::string out = toStr(env, outPath);

        // Log every input so you can verify what JNI passed
        LOGI("Compressing to: %s", out.c_str());
        for (size_t i = 0; i < inputs.size(); ++i) {
            LOGI("  input[%zu] = %s", i, inputs[i].c_str());
        }

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

        LOGI("Decompressing archive: %s -> %s", in.c_str(), out.c_str());
        std::string extractedName = extractArchive(in, out);
        return env->NewStringUTF(extractedName.c_str());

    } catch (const std::exception& e) {
        LOGE("Error: %s", e.what());
        return nullptr;
    }
}
