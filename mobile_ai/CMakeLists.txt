cmake_minimum_required(VERSION 3.21)
project(colibri_mobile_ai LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(colibri_mobile_ai STATIC
    src/engine.cpp
    src/simd.cpp
)
target_include_directories(colibri_mobile_ai PUBLIC include)
target_compile_options(colibri_mobile_ai PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

if(ANDROID)
    target_compile_definitions(colibri_mobile_ai PUBLIC COLIBRI_MOBILE_ANDROID=1)
elseif(APPLE)
    target_compile_definitions(colibri_mobile_ai PUBLIC COLIBRI_MOBILE_IOS=1)
endif()

if(NOT WIN32)
    target_link_libraries(colibri_mobile_ai PRIVATE pthread)
endif()

option(COLIBRI_MOBILE_BUILD_TESTS "Build the host-side smoke test" ON)
if(COLIBRI_MOBILE_BUILD_TESTS)
    enable_testing()
    add_executable(colibri_mobile_ai_test tests/engine_test.cpp)
    target_link_libraries(colibri_mobile_ai_test PRIVATE colibri_mobile_ai)
    add_test(NAME colibri_mobile_ai_test COMMAND colibri_mobile_ai_test)
endif()
