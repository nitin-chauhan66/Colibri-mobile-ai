cmake_minimum_required(VERSION 3.22.1)
project(colibri_mobile_jni LANGUAGES CXX)

add_library(colibri_mobile_jni SHARED
    native_engine.cpp
    ../../../../../src/engine.cpp
    ../../../../../src/simd.cpp
)
target_include_directories(colibri_mobile_jni PRIVATE ../../../../../include)
target_compile_features(colibri_mobile_jni PRIVATE cxx_std_20)
target_compile_options(colibri_mobile_jni PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(colibri_mobile_jni log)
