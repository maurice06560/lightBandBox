cmake_minimum_required(VERSION 3.22)

project(MBoxCore VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MBOX_BUILD_TESTS "Build the MBoxCoreTests unit test runner" ON)
set(MBOX_ASIO_SDK_DIR "" CACHE PATH "Windows only: the Steinberg ASIO SDK folder (the one containing common/iasiodrv.h)")

add_subdirectory(JUCE)

#===============================================================================
# MBoxCoreSources: the library's sources, JUCE modules and settings, as an INTERFACE
# target. Whatever links it compiles all of it, so a binary only ever contains one
# copy of JUCE:
#   - MBoxCore (shared) compiles it for the headless rack app, with JUCE hidden inside.
#   - MBoxCoreEmbedded compiles it into a JUCE UI app or the tests, next to the app's
#     own JUCE modules.
#===============================================================================
add_library(MBoxCoreSources INTERFACE)

target_sources(MBoxCoreSources
    INTERFACE
        Sources/MBoxCoreAudio.cpp
        Sources/Audio/MBoxAudioClipCache.cpp
        Sources/Audio/MBoxAudioEngine.cpp
        Sources/Audio/MBoxClickSynth.cpp
        Sources/Audio/MBoxRenderer.cpp
        Sources/Audio/MBoxSongPlayer.cpp
        Sources/Model/MBoxBank.cpp
        Sources/Model/MBoxPlaylist.cpp
        Sources/Model/MBoxRig.cpp
        Sources/Model/MBoxSong.cpp
        Sources/Model/MBoxTimeline.cpp
        Sources/Model/MBoxTrack.cpp
        Sources/Model/MBoxTypes.cpp
        Sources/Persistence/MBoxStorage.cpp
        Sources/Plugins/MBoxEffectChain.cpp
        Sources/Plugins/MBoxLiveInput.cpp
        Sources/Plugins/MBoxPluginHost.cpp
        Sources/TCPServer/MBoxServer.cpp)

target_include_directories(MBoxCoreSources
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/Sources"
        "${CMAKE_CURRENT_BINARY_DIR}")

target_compile_definitions(MBoxCoreSources
    INTERFACE
        MBOX_VERSION_STRING="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1     # bank-slot chains, inserts and master chains host VST3s
        $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)

target_link_libraries(MBoxCoreSources
    INTERFACE
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        # AU hosting needs CoreAudioKit, which JUCE only adds for its own app/plugin targets
        "$<$<PLATFORM_ID:Darwin>:-framework CoreAudioKit>")

# Low-latency audio on Windows needs ASIO. The SDK can't be redistributed, so it isn't in the repository.
if(WIN32 AND MBOX_ASIO_SDK_DIR)
    target_compile_definitions(MBoxCoreSources INTERFACE JUCE_ASIO=1)
    target_include_directories(MBoxCoreSources INTERFACE "${MBOX_ASIO_SDK_DIR}/common")
elseif(WIN32)
    message(WARNING "MBOX_ASIO_SDK_DIR is not set: building without ASIO, so only WASAPI/DirectSound latencies are available")
endif()

#===============================================================================
# MBoxCore: the shared library for the headless rack app
#===============================================================================
add_library(MBoxCore SHARED)

target_link_libraries(MBoxCore
    PRIVATE
        MBoxCoreSources
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(MBoxCore PRIVATE JUCE_STANDALONE_APPLICATION=0)

target_include_directories(MBoxCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Sources"
        "${CMAKE_CURRENT_BINARY_DIR}")

# Only symbols marked MBOX_EXPORT (the public API) are exported, not the whole of JUCE
include(GenerateExportHeader)
generate_export_header(MBoxCore BASE_NAME MBOX EXPORT_FILE_NAME mbox_export.h)

set_target_properties(MBoxCore PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    OBJC_VISIBILITY_PRESET hidden
    OBJCXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

#===============================================================================
# MBoxCoreEmbedded: for a JUCE app that compiles the library into itself (the UI app)
# and for the tests. MBOX_STATIC_DEFINE turns MBOX_EXPORT into nothing.
#===============================================================================
add_library(MBoxCoreEmbedded INTERFACE)
target_link_libraries(MBoxCoreEmbedded INTERFACE MBoxCoreSources)
target_compile_definitions(MBoxCoreEmbedded INTERFACE MBOX_STATIC_DEFINE)

#===============================================================================
# Tests
#===============================================================================
if(MBOX_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(MBoxCoreTests PRODUCT_NAME "MBoxCoreTests")

    target_sources(MBoxCoreTests
        PRIVATE
            Tests/Main.cpp
            Tests/MBoxTestUtilities.cpp
            Tests/MBoxModelTests.cpp
            Tests/MBoxRendererTests.cpp
            Tests/MBoxServerTests.cpp
            Tests/MBoxStorageTests.cpp
            Tests/MBoxTimelineTests.cpp)

    target_compile_definitions(MBoxCoreTests
        PRIVATE
            JUCE_UNIT_TESTS=1
            JUCE_MODAL_LOOPS_PERMITTED=1)  # the tests run the message loop with runDispatchLoopUntil

    target_link_libraries(MBoxCoreTests
        PRIVATE
            MBoxCoreEmbedded
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME MBoxCoreTests COMMAND MBoxCoreTests)
endif()
