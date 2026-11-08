package com.colibri.mobile;

final class NativeEngine {
    private static final boolean NATIVE_AVAILABLE;

    static {
        boolean available;
        try {
            System.loadLibrary("colibri_mobile_jni");
            available = true;
        } catch (UnsatisfiedLinkError ignored) {
            available = false;
        }
        NATIVE_AVAILABLE = available;
    }

    private NativeEngine() {}

    static long create(String appFilesDir) {
        return NATIVE_AVAILABLE ? nativeCreate(appFilesDir) : 1L;
    }

    static String run(long handle, int[] tokens) {
        if (NATIVE_AVAILABLE) {
            return nativeRun(handle, tokens);
        }
        return "Java fallback generated " + tokens.length
                + " tokens (native NDK engine not bundled in this APK).";
    }

    static void destroy(long handle) {
        if (NATIVE_AVAILABLE) {
            nativeDestroy(handle);
        }
    }

    private static native long nativeCreate(String appFilesDir);
    private static native String nativeRun(long handle, int[] tokens);
    private static native void nativeDestroy(long handle);
}
