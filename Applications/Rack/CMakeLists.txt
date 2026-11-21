#===============================================================================
# Rack: the headless rack application. It links the MBoxCore shared library and
# talks to audio only through MBoxCoreAudio.
#
# The app has its own (small) copy of JUCE for juce::JUCEApplication and its message
# loop; the library's copy stays hidden inside libMBoxCore. Both post to the native
# main run loop (macOS) or the thread's window queue (Windows), so the app's loop
# serves the library too. On Linux JUCE's loop is internal to each copy, so the
# library's messages would not be dispatched: there, use MBoxCoreAudio::run() instead.
#===============================================================================
juce_add_console_app(Rack PRODUCT_NAME "Rack")

target_sources(Rack
    PRIVATE
        Main.cpp
        OneBoxBandApplication.cpp)

target_compile_definitions(Rack
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Rack,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(Rack
    PRIVATE
        MBoxCore
        juce::juce_gui_basics  # juce::JUCEApplication
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
