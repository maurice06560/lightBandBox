#===============================================================================
# OneBoxBand: the JUCE UI application. It compiles the library in (MBoxCoreEmbedded),
# so the process holds a single copy of JUCE and the UI can use the model classes and
# host plugin editors. See Docs/UIPortPlan.md.
#
# Not built (UIPortPlan step 5): LoudnessMeterView and TunerPanel still use the previous
# implementation's MBoxApplication.
#===============================================================================
juce_add_module("${PROJECT_SOURCE_DIR}/Modules/onebox_ui")

juce_add_gui_app(OneBoxBand
    PRODUCT_NAME "OneBoxBand"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "OneBox"
    BUNDLE_ID "com.onebox.onebandbox"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "OneBoxBand plays your instruments through their bank slots.")

target_sources(OneBoxBand
    PRIVATE
        Main.cpp
        OneBoxBandApplication.cpp
        Session.cpp
        LoadingOverlay.cpp
        StartupSplash.cpp
        MainComponent.cpp
        TransportBar.cpp
        BrowserPanel.cpp
        ArrangementView.cpp
        ListEditorPanel.cpp
        MixerPanel.cpp
        SubmixPanel.cpp
        AudioDevicePanel.cpp
        BanksEditor.cpp
        PluginMenu.cpp
        PluginWindows.cpp
        ControlSpaceMonitor.cpp
        $<$<PLATFORM_ID:Darwin>:ControlSpaceMonitor.mm>)

target_include_directories(OneBoxBand
    PRIVATE
        "${PROJECT_SOURCE_DIR}/Modules")  # <onebox_ui/onebox_ui.h>

target_compile_definitions(OneBoxBand
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:OneBoxBand,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:OneBoxBand,JUCE_VERSION>")

target_link_libraries(OneBoxBand
    PRIVATE
        MBoxCoreEmbedded
        onebox_ui
        juce::juce_gui_extra
        juce::juce_audio_utils  # waveforms (AudioThumbnail)
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
