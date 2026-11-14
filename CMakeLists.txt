cmake_minimum_required(VERSION 3.22)

project(OneBoxBand VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_gui_app(OneBoxBand
    PRODUCT_NAME "OneBoxBand"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "OneBoxBand needs audio input access to pass audio through.")

target_sources(OneBoxBand PRIVATE
    Sources/Main.cpp
    Sources/Audio/MBoxAudioEngine.cpp
    Sources/Model/MBoxApplication.cpp
    Sources/Model/MBoxPlaylist.cpp
    Sources/Model/MBoxSong.cpp
    Sources/Model/MBoxClipStreamer.cpp
    Sources/Persistence/MBoxStore.cpp
    Sources/Communication/MBoxComServer.cpp)

target_compile_definitions(OneBoxBand PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:OneBoxBand,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:OneBoxBand,JUCE_VERSION>")

target_link_libraries(OneBoxBand
    PRIVATE
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Console client for the MBoxComServer (port 5050)
juce_add_console_app(OneBoxConsole
    PRODUCT_NAME "OneBoxConsole")

target_sources(OneBoxConsole PRIVATE
    Sources/Console/Main.cpp)

target_compile_definitions(OneBoxConsole PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(OneBoxConsole
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
