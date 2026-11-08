# Colibrì Mobile AI prototype

This module is a small C++20 reference implementation of a mobile-friendly
streaming layer for Colibrì. It keeps the base model under an application
memory budget and maps expert files on demand; weight tensors are never copied
into `malloc`/`new`-owned buffers.

## Design

- `MemoryManager` enforces a hard application cap before every dynamic mapping.
  RSS is sampled from `/proc/self/smaps_rollup` on Linux and `getrusage` as a
  portable fallback. The base model is registered once and remains owned by the
  caller for the engine lifetime. Do not use `mlock` on mobile: Jetsam/LMK can
  still terminate an app with locked pages.
- `TensorRouter` validates file ranges, aligns offsets to the OS page size,
  opens read-only, maps with `mmap(MAP_PRIVATE)`, and releases the descriptor,
  mapping, and memory reservation through RAII. `madvise(MADV_WILLNEED)` gives
  the kernel a prefetch hint; it is not treated as a residency guarantee.
- A bounded recycling ring holds only a few mapping handles. The background
  worker consumes routing predictions while the caller executes base-layer
  work. The queue is bounded and drops the oldest prediction instead of
  growing without limit.
- `simd.cpp` contains the scalar/ARM NEON kernel seam. FlashAttention should be
  supplied by the platform backend (Metal on iOS, NNAPI/LiteRT or a Vulkan
  backend on Android) and should consume the mapped span synchronously.

The cap is a safety ceiling, not a performance promise. A production engine
should also account for KV cache, tokenizer buffers, UI allocations, GPU
memory, and the platform's process pressure callbacks. Use a cap below the
4 GB application allowance (for example 3.0-3.5 GiB) after measuring those
other allocations.

## Build and smoke test

```sh
cmake -S mobile_ai -B /tmp/colibri-mobile-build -DCOLIBRI_MOBILE_BUILD_TESTS=ON
cmake --build /tmp/colibri-mobile-build
ctest --test-dir /tmp/colibri-mobile-build --output-on-failure
```

## Android JNI integration

Build the static library with the Android NDK toolchain and expose an opaque
`jlong` handle. Kotlin should create the engine on a background dispatcher,
send token IDs to a JNI `run` method, and marshal only generated token IDs back
to the UI thread. Register the base model and expert paths from app-private
storage; never accept arbitrary paths from the UI.

```cpp
extern "C" JNIEXPORT jlong JNICALL
Java_com_example_ai_Colibri_nativeCreate(JNIEnv* env, jclass,
                                         jstring base_dir);
extern "C" JNIEXPORT void JNICALL
Java_com_example_ai_Colibri_nativeDestroy(JNIEnv*, jclass, jlong handle);
```

Use `android.os.Debug.getPss()` or a native RSS sampler for telemetry, and
pause/trim the prefetch queue from `ComponentCallbacks2::onTrimMemory`.
NNAPI/LiteRT and GPU buffers must be included in the same process budget.

## iOS Objective-C++ / Swift integration

Compile the C++ sources as Objective-C++ in an XCFramework target and wrap
the engine in an `NSObject` that owns a `std::unique_ptr`. Swift should call
it from an actor or a dedicated serial `DispatchQueue`; token callbacks must
be delivered to `MainActor`. Map files from the app's sandbox and respond to
`UIApplication.didReceiveMemoryWarningNotification` by stopping new
prefetches and clearing the ring.

```objc
@interface COLIEngine : NSObject
- (instancetype)initWithBasePath:(NSString *)basePath
                       expertPath:(NSString *)expertPath;
- (void)generate:(NSArray<NSNumber *> *)tokenIDs
       completion:(void (^)(NSArray<NSNumber *> *tokens, NSError *error))completion;
@end
```

Metal/Accelerate kernels should receive a short-lived view of the mapped
expert and complete the command buffer before the mapping is released.
