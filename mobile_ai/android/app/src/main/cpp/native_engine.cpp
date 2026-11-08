#include "colibri_mobile/engine.hpp"

#include <jni.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct EngineHandle {
    colibri::mobile::MemoryManager memory;
    std::unique_ptr<colibri::mobile::TensorRouter> router;

    explicit EngineHandle(const std::string& files_dir)
        : memory(3ULL * 1024ULL * 1024ULL * 1024ULL),
          router(std::make_unique<colibri::mobile::TensorRouter>(
              memory, std::vector<colibri::mobile::ExpertSpec>{}, 2)) {
        (void)files_dir;
        memory.registerBaseResident(64ULL * 1024ULL * 1024ULL);
    }
};

std::string toUtf8(JNIEnv* env, jstring value) {
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result(chars == nullptr ? "" : chars);
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(value, chars);
    }
    return result;
}
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_colibri_mobile_NativeEngine_create(JNIEnv* env, jclass,
                                            jstring files_dir) {
    try {
        return reinterpret_cast<jlong>(
            new EngineHandle(toUtf8(env, files_dir)));
    } catch (...) {
        return 0;
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_colibri_mobile_NativeEngine_run(JNIEnv* env, jclass, jlong handle,
                                         jintArray token_array) {
    if (handle == 0 || token_array == nullptr) {
        return env->NewStringUTF("Native engine is unavailable.");
    }
    auto* engine = reinterpret_cast<EngineHandle*>(handle);
    const jsize count = env->GetArrayLength(token_array);
    std::vector<jint> tokens(static_cast<std::size_t>(count));
    env->GetIntArrayRegion(token_array, 0, count, tokens.data());
    std::vector<std::uint32_t> input;
    input.reserve(tokens.size());
    for (jint token : tokens) {
        input.push_back(static_cast<std::uint32_t>(token));
    }
    const auto metrics = colibri::mobile::DualThreadInference(*engine->router)
                             .run(input, [](std::uint32_t) {},
                                  [](std::uint32_t) { return std::string{}; });
    std::ostringstream result;
    result << "Generated " << metrics.tokens << " tokens at "
           << metrics.tokens_per_second << " tokens/sec.";
    return env->NewStringUTF(result.str().c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_colibri_mobile_NativeEngine_destroy(JNIEnv*, jclass, jlong handle) {
    delete reinterpret_cast<EngineHandle*>(handle);
}
