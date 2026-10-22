# Native HTDemucs. Fetch only pinned source trees; upstream's top-level build
# imposes -march=native, CLIs, OpenMP and external BLAS assumptions that are not
# suitable for a universal, self-contained macOS app.
include(FetchContent)
FetchContent_Declare(samplify_demucs
    URL https://codeload.github.com/sevagh/demucs.cpp/tar.gz/f1206e9adeea103aef4a636b9e62297cf1f8e34e
    URL_HASH SHA256=f08bcacb49f99fdedac8affe13d671ec82cf3639c096b92c9c3da08c4b952934
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR samplify-do-not-build-upstream)
FetchContent_Declare(samplify_eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/dd8c71e62852b2fe429edb6682ac91fd1c578a26/eigen-dd8c71e62852b2fe429edb6682ac91fd1c578a26.tar.gz
    URL_HASH SHA256=e8cbc003b1fa5e0d0d142c58155aaf341f87783d213d31a605a19b2f13d27acc
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR samplify-do-not-build-upstream)
FetchContent_MakeAvailable(samplify_demucs samplify_eigen)

# The pinned upstream loader reads uninitialized dimensions once it reaches
# normal EOF. Generate a build-local copy with checked record headers. Integrity
# checking before model parsing remains mandatory in StemSeparator itself.
file(READ "${samplify_demucs_SOURCE_DIR}/src/model_load.cpp" _stem_loader)
set(_stem_old_header [=[        int32_t n_dims;
        int32_t length;

        fread(&n_dims, sizeof(int32_t), 1, f);
        fread(&length, sizeof(int32_t), 1, f);]=])
set(_stem_new_header [=[        int32_t n_dims = 0;
        int32_t length = 0;
        if (fread(&n_dims, sizeof(int32_t), 1, f) != 1) {
            if (feof(f)) break;
            fclose(f); return false;
        }
        if (fread(&length, sizeof(int32_t), 1, f) != 1
            || n_dims < 1 || n_dims > 4 || length < 1 || length > 4096) {
            fclose(f); return false;
        }]=])
string(FIND "${_stem_loader}" "${_stem_old_header}" _stem_header_position)
if(_stem_header_position LESS 0)
    message(FATAL_ERROR "Pinned Demucs loader changed; re-audit the checked-header patch")
endif()
string(REPLACE "${_stem_old_header}" "${_stem_new_header}" _stem_loader "${_stem_loader}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/samplify-demucs-patched")
set(_stem_loader_file "${CMAKE_CURRENT_BINARY_DIR}/samplify-demucs-patched/model_load.cpp")
file(CONFIGURE OUTPUT "${_stem_loader_file}" CONTENT "#include <chrono>\n${_stem_loader}" @ONLY)

# Near-silent/anti-phase input can produce zero mono variance in upstream
# normalization. Fall back to channel RMS rather than creating NaN samples.
file(READ "${samplify_demucs_SOURCE_DIR}/src/model_apply.cpp" _stem_apply)
set(_stem_old_normalize "    // Normalize the audio\n")
set(_stem_new_normalize [=[    if (!std::isfinite(ref_std) || ref_std < 1e-8f) {
        ref_std = std::sqrt((full_audio.array() - ref_mean).square().mean());
        ref_std = std::max(ref_std, 1e-8f);
    }
    // Normalize the audio
]=])
string(FIND "${_stem_apply}" "${_stem_old_normalize}" _stem_normalize_position)
if(_stem_normalize_position LESS 0)
    message(FATAL_ERROR "Pinned Demucs normalization changed; re-audit the stability patch")
endif()
string(REPLACE "${_stem_old_normalize}" "${_stem_new_normalize}" _stem_apply "${_stem_apply}")
set(_stem_apply_file "${CMAKE_CURRENT_BINARY_DIR}/samplify-demucs-patched/model_apply.cpp")
file(CONFIGURE OUTPUT "${_stem_apply_file}" CONTENT "${_stem_apply}" @ONLY)

add_library(SamplifyDemucs STATIC
    "${_stem_loader_file}" "${_stem_apply_file}"
    "${samplify_demucs_SOURCE_DIR}/src/crosstransformer.cpp"
    "${samplify_demucs_SOURCE_DIR}/src/dsp.cpp"
    "${samplify_demucs_SOURCE_DIR}/src/encdec.cpp"
    "${samplify_demucs_SOURCE_DIR}/src/layers.cpp"
    "${samplify_demucs_SOURCE_DIR}/src/lstm.cpp"
    "${samplify_demucs_SOURCE_DIR}/src/model_inference.cpp")
target_include_directories(SamplifyDemucs PUBLIC "${samplify_demucs_SOURCE_DIR}/src")
target_include_directories(SamplifyDemucs SYSTEM PUBLIC "${samplify_eigen_SOURCE_DIR}")
target_compile_features(SamplifyDemucs PUBLIC cxx_std_17)
target_compile_definitions(SamplifyDemucs PUBLIC EIGEN_DONT_PARALLELIZE)
set_target_properties(SamplifyDemucs PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(SamplifyDemucs PRIVATE /O2)
else()
    # Inference is unusably slow at -O0. This is ordinary portable optimization,
    # not -march=native or -ffast-math; universal architecture flags are retained.
    target_compile_options(SamplifyDemucs PRIVATE -O2)
endif()
if(APPLE)
    find_library(_samplify_accelerate Accelerate REQUIRED)
    target_link_libraries(SamplifyDemucs PRIVATE "${_samplify_accelerate}")
    target_compile_definitions(SamplifyDemucs PUBLIC EIGEN_USE_BLAS)
endif()

add_library(SamplifyStems STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../src/stems/StemSeparator.cpp")
find_package(Threads REQUIRED)
target_include_directories(SamplifyStems PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
target_link_libraries(SamplifyStems PRIVATE SamplifyDemucs Threads::Threads PUBLIC SamplifyCore)
target_compile_features(SamplifyStems PUBLIC cxx_std_20)

set(SAMPLIFY_STEM_MODEL_FILE "${CMAKE_BINARY_DIR}/models/ggml-model-htdemucs-4s-f16.bin"
    CACHE FILEPATH "Verified native HTDemucs model to bundle with the app")
set(SAMPLIFY_STEM_MODEL_SHA256 "72b17c42d308982ddb5069bc3bf48b81a5aac4cb6516e4366c0fa7cef6df0064")

function(samplify_add_stem_resources app_target)
    if(NOT EXISTS "${SAMPLIFY_STEM_MODEL_FILE}")
        message(FATAL_ERROR "Prepare the native model before configuring: python3 script/prepare_stem_model.py --output '${SAMPLIFY_STEM_MODEL_FILE}'")
    endif()
    file(SHA256 "${SAMPLIFY_STEM_MODEL_FILE}" _actual_stem_hash)
    if(NOT _actual_stem_hash STREQUAL SAMPLIFY_STEM_MODEL_SHA256)
        message(FATAL_ERROR "Stem model SHA-256 mismatch; regenerate it from the pinned official checkpoint")
    endif()
    target_sources(${app_target} PRIVATE "${SAMPLIFY_STEM_MODEL_FILE}")
    set_source_files_properties("${SAMPLIFY_STEM_MODEL_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set(_stem_notice_files
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/stems/licenses/Demucs-MIT.txt"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/stems/licenses/DemucsCpp-MIT.txt"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/stems/licenses/NativeStems-Notice.txt"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/stems/licenses/Eigen-MPL-2.0.txt")
    target_sources(${app_target} PRIVATE ${_stem_notice_files})
    set_source_files_properties(${_stem_notice_files} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Licenses)
endfunction()

if(BUILD_TESTING)
    add_executable(SamplifyStemTests "${CMAKE_CURRENT_LIST_DIR}/../src/stems/StemTests.cpp")
    target_link_libraries(SamplifyStemTests PRIVATE SamplifyStems)
    add_test(NAME SamplifyStemTests COMMAND SamplifyStemTests "${SAMPLIFY_STEM_MODEL_FILE}")
    set_tests_properties(SamplifyStemTests PROPERTIES TIMEOUT 900)
endif()
