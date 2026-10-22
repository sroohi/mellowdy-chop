include_guard(GLOBAL)
include(FetchContent)

# Rubber Band 4.0.0, tag commit 1d95888bec3ae0a17c0c4af791810d5a63f6bc35.
# The archive hash pins the exact source even if an upstream tag changes.
FetchContent_Declare(SamplifyRubberBand
    URL https://codeload.github.com/breakfastquay/rubberband/tar.gz/refs/tags/v4.0.0
    URL_HASH SHA256=24300f48a8014b7c863b573a9647e61b1b19b37875e2cdd92005e64c6424d266
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(SamplifyRubberBand)

# Retain the R2 onset detector's already allocated storage across FFT-size
# changes. The patch validates complete upstream/patched file hashes and is
# idempotent, so a different upstream version fails at configure time.
set(SAMPLIFY_RUBBERBAND_PATCH_ROOT "${samplifyrubberband_SOURCE_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/PatchRubberBand.cmake")
unset(SAMPLIFY_RUBBERBAND_PATCH_ROOT)

# The supported single-file build uses built-in FFT/resampling on Linux and
# system vDSP on Apple. It has no Meson or separately installed runtime dependency.
get_filename_component(SAMPLIFY_AUDIO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(SamplifyStretch STATIC
    "${SAMPLIFY_AUDIO_ROOT}/src/dsp/RubberBandProcessor.cpp"
    "${samplifyrubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
target_compile_features(SamplifyStretch PUBLIC cxx_std_20)
target_include_directories(SamplifyStretch PUBLIC "${SAMPLIFY_AUDIO_ROOT}/src")
target_include_directories(SamplifyStretch SYSTEM PUBLIC "${samplifyrubberband_SOURCE_DIR}")
target_compile_definitions(SamplifyStretch PUBLIC SAMPLIFY_USE_RUBBERBAND=1)
set_target_properties(SamplifyStretch PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(SamplifyStretch PRIVATE /O2)
else()
    # The native audio callback uses this DSP even in a debug application.
    target_compile_options(SamplifyStretch PRIVATE -O2)
endif()
if(APPLE)
    find_library(SAMPLIFY_ACCELERATE_FRAMEWORK Accelerate REQUIRED)
    target_link_libraries(SamplifyStretch PRIVATE "${SAMPLIFY_ACCELERATE_FRAMEWORK}")
endif()
