cmake_minimum_required (VERSION 3.22)
project (ARAFLBridge VERSION 0.2.0)
set (CMAKE_CXX_STANDARD 17)

include (FetchContent)
FetchContent_Declare (JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.12)
FetchContent_MakeAvailable (JUCE)

# ARA SDK: clone github.com/Celemony/ARA_SDK (WITH submodules) and point at the folder containing ARA_API/
set (ARA_SDK_DIR "" CACHE PATH "Path to ARA_SDK root")
# Your FLP parser (flp.h / flp.cpp) - only needed for the flp2sidecar exporter
set (FLP_SOURCE_DIR "" CACHE PATH "Folder containing flp.h and flp.cpp")
if (NOT EXISTS "${ARA_SDK_DIR}/ARA_API/ARAInterface.h")
    message (FATAL_ERROR "Set -DARA_SDK_DIR=<path to ARA_SDK>")
endif()

# ---------------- the bridge plug-in ----------------
juce_add_plugin (ARAFLBridge
    COMPANY_NAME "AlphaAudio" PLUGIN_MANUFACTURER_CODE Wama PLUGIN_CODE Arab
    FORMATS VST3 PRODUCT_NAME "ARA FL Bridge" IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx)

target_sources (ARAFLBridge PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/MiniAraHost.cpp)

# JUCE 8.0.12 keeps the bundled VST3 SDK under juce_audio_processors_headless (ARAVST3.h needs pluginterfaces/)
target_include_directories (ARAFLBridge PRIVATE "${ARA_SDK_DIR}" Source
    "${juce_SOURCE_DIR}/modules/juce_audio_processors_headless/format_types/VST3_SDK")
target_compile_definitions (ARAFLBridge PUBLIC
    JUCE_PLUGINHOST_VST3=1 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries (ARAFLBridge PRIVATE
    juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if (MSVC)
    target_compile_options (ARAFLBridge PRIVATE /Zc:__cplusplus /wd4996)
endif()

# ---------------- flp2sidecar console exporter (optional) ----------------
if (EXISTS "${FLP_SOURCE_DIR}/flp.h")
    juce_add_console_app (flp2sidecar PRODUCT_NAME "flp2sidecar")
    target_sources (flp2sidecar PRIVATE Tools/flp2sidecar.cpp Source/FLPSidecar/FLPSidecarExporter.cpp "${FLP_SOURCE_DIR}/flp.cpp")
    target_include_directories (flp2sidecar PRIVATE "${FLP_SOURCE_DIR}" Source/FLPSidecar)
    target_compile_definitions (flp2sidecar PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
    target_link_libraries (flp2sidecar PRIVATE juce::juce_audio_processors juce::juce_cryptography
        PUBLIC juce::juce_recommended_config_flags)
endif()
