cmake_minimum_required(VERSION 3.28)
project(Samplify VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum supported macOS")
endif()

option(SAMPLIFY_BUILD_APP "Build the standalone desktop application" ON)
option(SAMPLIFY_HIGH_QUALITY_STRETCH "Use native Rubber Band processing" ${SAMPLIFY_BUILD_APP})
include(CTest)

add_library(SamplifyWorkstationModel src/workstation/WorkstationModel.cpp)
target_include_directories(SamplifyWorkstationModel PUBLIC src)
add_library(SamplifyEffects src/dsp/Effects.cpp)
target_link_libraries(SamplifyEffects PUBLIC SamplifyWorkstationModel)

add_library(SamplifyCore src/core/Model.cpp src/core/DSP.cpp src/core/CueEditing.cpp)
target_include_directories(SamplifyCore PUBLIC src)
if(SAMPLIFY_HIGH_QUALITY_STRETCH)
    include(cmake/AudioBackends.cmake)
    target_link_libraries(SamplifyCore PUBLIC SamplifyStretch)
endif()
if(MSVC)
    target_compile_options(SamplifyCore PRIVATE /W4)
else()
    target_compile_options(SamplifyCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
    add_executable(SamplifyWorkstationModelTests tests/workstation_model_tests.cpp)
    target_link_libraries(SamplifyWorkstationModelTests PRIVATE SamplifyWorkstationModel)
    add_test(NAME SamplifyWorkstationModelTests COMMAND SamplifyWorkstationModelTests)
    add_executable(SamplifyEffectsTests tests/effects_tests.cpp)
    target_link_libraries(SamplifyEffectsTests PRIVATE SamplifyEffects)
    add_test(NAME SamplifyEffectsTests COMMAND SamplifyEffectsTests)
    add_executable(SamplifyCoreTests tests/core_tests.cpp)
    target_link_libraries(SamplifyCoreTests PRIVATE SamplifyCore)
    add_test(NAME SamplifyCoreTests COMMAND SamplifyCoreTests)
    add_executable(SamplifyCueEditingTests tests/cue_editing_tests.cpp)
    target_link_libraries(SamplifyCueEditingTests PRIVATE SamplifyCore)
    add_test(NAME SamplifyCueEditingTests COMMAND SamplifyCueEditingTests)
    if(SAMPLIFY_HIGH_QUALITY_STRETCH)
        add_executable(SamplifyStretchTests tests/stretch_tests.cpp src/core/Model.cpp)
        target_link_libraries(SamplifyStretchTests PRIVATE SamplifyStretch)
        add_test(NAME SamplifyStretchTests COMMAND SamplifyStretchTests)
    endif()
endif()

if(SAMPLIFY_BUILD_APP)
    if(NOT SAMPLIFY_HIGH_QUALITY_STRETCH)
        message(FATAL_ERROR "The native app requires the Rubber Band backend")
    endif()
    include(cmake/StemBackend.cmake)
    include(FetchContent)
    FetchContent_Declare(JUCE
        URL https://codeload.github.com/juce-framework/JUCE/tar.gz/51a8a6d7aeae7326956d747737ccf1575e61e209
        URL_HASH SHA256=20b740351cc030086951d87614bc08b42d072f1e4db68db1ef1d988e66dbbdbe
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(JUCE)

    juce_add_gui_app(Samplify
        PRODUCT_NAME "Samplify"
        COMPANY_NAME "MP Audio"
        BUNDLE_ID "audio.mpaudio.samplify"
        ICON_BIG assets/SamplifyIcon.png
        ICON_SMALL assets/SamplifyIcon.png
        VERSION "${PROJECT_VERSION}"
        MICROPHONE_PERMISSION_ENABLED FALSE
        DOCUMENT_EXTENSIONS "samplify")
    target_sources(Samplify PRIVATE
        src/Main.cpp
        src/audio/AudioEngine.cpp
        src/workstation/WorkstationRuntime.cpp
        src/plugins/PluginHost.cpp
        src/audio/SessionIO.cpp
        src/audio/SeratoMetadata.cpp
        src/ui/MainComponent.cpp
        src/ui/StudioComponent.cpp
        src/ui/HardwareLookAndFeel.cpp
        src/ui/WorkstationComponent.cpp
        src/ui/SourceWaveform.cpp)
    target_include_directories(Samplify PRIVATE src)
    target_compile_definitions(Samplify PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Samplify,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(Samplify PRIVATE SamplifyCore SamplifyStems SamplifyEffects
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    samplify_add_stem_resources(Samplify)
    configure_file("${juce_SOURCE_DIR}/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt"
        "${CMAKE_BINARY_DIR}/VST3-SDK-LICENSE.txt" COPYONLY)
    target_sources(Samplify PRIVATE "${CMAKE_BINARY_DIR}/VST3-SDK-LICENSE.txt")
    set_source_files_properties("${CMAKE_BINARY_DIR}/VST3-SDK-LICENSE.txt"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Licenses)
    configure_file("${samplifyrubberband_SOURCE_DIR}/COPYING" "${CMAKE_BINARY_DIR}/RubberBand-COPYING.txt" COPYONLY)
    target_sources(Samplify PRIVATE resources/THIRD_PARTY_NOTICES.md LICENSE "${CMAKE_BINARY_DIR}/RubberBand-COPYING.txt")
    set_source_files_properties(resources/THIRD_PARTY_NOTICES.md LICENSE "${CMAKE_BINARY_DIR}/RubberBand-COPYING.txt"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    if(NOT APPLE)
        add_custom_command(TARGET Samplify POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Samplify>/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SAMPLIFY_STEM_MODEL_FILE}"
                "$<TARGET_FILE_DIR:Samplify>/Resources/ggml-model-htdemucs-4s-f16.bin")
    endif()
    if(BUILD_TESTING)
        juce_add_console_app(SamplifyIntegrationTests PRODUCT_NAME "SamplifyIntegrationTests")
        target_sources(SamplifyIntegrationTests PRIVATE tests/integration_tests.cpp
            src/audio/AudioEngine.cpp src/audio/SessionIO.cpp
            src/workstation/WorkstationRuntime.cpp src/plugins/PluginHost.cpp)
        target_include_directories(SamplifyIntegrationTests PRIVATE src)
        target_compile_definitions(SamplifyIntegrationTests PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>)
        target_link_libraries(SamplifyIntegrationTests PRIVATE SamplifyCore SamplifyEffects
            juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(SamplifyIntegrationTests PRIVATE SAMPLIFY_WRAP_C_ALLOCATIONS=1)
            target_link_options(SamplifyIntegrationTests PRIVATE
                -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=posix_memalign,--wrap=aligned_alloc)
        endif()
        add_test(NAME SamplifyIntegrationTests COMMAND SamplifyIntegrationTests)
        juce_add_console_app(SamplifyMetadataTests PRODUCT_NAME "SamplifyMetadataTests")
        target_sources(SamplifyMetadataTests PRIVATE tests/serato_metadata_tests.cpp src/audio/SeratoMetadata.cpp)
        target_include_directories(SamplifyMetadataTests PRIVATE src)
        target_compile_definitions(SamplifyMetadataTests PRIVATE JUCE_USE_CURL=0)
        target_link_libraries(SamplifyMetadataTests PRIVATE SamplifyCore juce::juce_core juce::juce_recommended_config_flags)
        add_test(NAME SamplifyMetadataTests COMMAND SamplifyMetadataTests)
        foreach(testTarget SamplifyWorkstationAudioTests SamplifyPluginHostTests SamplifyWorkstationSessionTests)
            juce_add_console_app(${testTarget} PRODUCT_NAME "${testTarget}")
            target_include_directories(${testTarget} PRIVATE src)
            target_compile_definitions(${testTarget} PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>)
            target_link_libraries(${testTarget} PRIVATE SamplifyCore SamplifyEffects
                juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
            add_test(NAME ${testTarget} COMMAND ${testTarget})
            set_tests_properties(${testTarget} PROPERTIES TIMEOUT 900)
        endforeach()
        target_sources(SamplifyWorkstationAudioTests PRIVATE tests/workstation_audio_tests.cpp
            src/audio/AudioEngine.cpp src/audio/SessionIO.cpp
            src/workstation/WorkstationRuntime.cpp src/plugins/PluginHost.cpp)
        target_sources(SamplifyWorkstationSessionTests PRIVATE tests/workstation_session_tests.cpp src/audio/SessionIO.cpp)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(SamplifyWorkstationAudioTests PRIVATE SAMPLIFY_WRAP_C_ALLOCATIONS=1)
            target_link_options(SamplifyWorkstationAudioTests PRIVATE
                -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=posix_memalign,--wrap=aligned_alloc)
        endif()
        target_compile_definitions(SamplifyPluginHostTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
        target_sources(SamplifyPluginHostTests PRIVATE tests/plugin_host_tests.cpp src/plugins/PluginHost.cpp)
        set_tests_properties(SamplifyIntegrationTests PROPERTIES TIMEOUT 900)
    endif()
endif()
