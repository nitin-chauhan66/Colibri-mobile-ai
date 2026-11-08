package com.colibri.mobile;

final class NativeEngine {
    static {
        System.loadLibrary("colibri_mobile_jni");
    }

    private NativeEngine() {}

    static native long create(String appFilesDir);
    static native String run(long handle, int[] tokens);
    static native void destroy(long handle);
}
