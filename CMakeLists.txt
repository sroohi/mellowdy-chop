cmake_minimum_required(VERSION 3.28)
project(Samplify VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum supported macOS")
endif()

option(SAMPLIFY_BUILD_APP "Build the standalone desktop application" ON)
include(CTest)

add_library(SamplifyCore src/core/Model.cpp src/core/DSP.cpp)
target_include_directories(SamplifyCore PUBLIC src)
if(MSVC)
    target_compile_options(SamplifyCore PRIVATE /W4)
else()
    target_compile_options(SamplifyCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
    add_executable(SamplifyCoreTests tests/core_tests.cpp)
    target_link_libraries(SamplifyCoreTests PRIVATE SamplifyCore)
    add_test(NAME SamplifyCoreTests COMMAND SamplifyCoreTests)
endif()

if(SAMPLIFY_BUILD_APP)
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
        src/audio/SessionIO.cpp
        src/ui/MainComponent.cpp)
    target_include_directories(Samplify PRIVATE src)
    target_compile_definitions(Samplify PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Samplify,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(Samplify PRIVATE SamplifyCore
        juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    if(BUILD_TESTING)
        juce_add_console_app(SamplifyIntegrationTests PRODUCT_NAME "SamplifyIntegrationTests")
        target_sources(SamplifyIntegrationTests PRIVATE tests/integration_tests.cpp
            src/audio/AudioEngine.cpp src/audio/SessionIO.cpp)
        target_include_directories(SamplifyIntegrationTests PRIVATE src)
        target_compile_definitions(SamplifyIntegrationTests PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(SamplifyIntegrationTests PRIVATE SamplifyCore
            juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
        add_test(NAME SamplifyIntegrationTests COMMAND SamplifyIntegrationTests)
    endif()
endif()
