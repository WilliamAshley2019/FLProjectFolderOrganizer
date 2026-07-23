cmake_minimum_required(VERSION 3.22)

project(FLPToolkit VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Static MSVC runtime - required for FL Studio plugin compatibility (avoids
# DLL-mismatch blacklisting when FL Studio scans plugins built with a
# different CRT than the host).
# ---------------------------------------------------------------------------
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if (MSVC)
    foreach(flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if (${flag_var} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# JUCE location. Point JUCE_PATH at your local JUCE 8.0.12 checkout, e.g.:
#   cmake -B build -DJUCE_PATH=C:/Development/JUCE
# Defaults to a sibling "JUCE" folder next to this project if not specified.
# ---------------------------------------------------------------------------
if (NOT DEFINED JUCE_PATH)
    set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE")
endif()
add_subdirectory(${JUCE_PATH} JUCE)

juce_add_plugin(FLPToolkit
    COMPANY_NAME "AlphaAudio"
    PLUGIN_MANUFACTURER_CODE Alph
    PLUGIN_CODE Flpt
    FORMATS VST3 Standalone
    PRODUCT_NAME "FLP Toolkit"
    VST3_CATEGORIES Tools
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    # "Plugin Channel Configurations" field must stay empty in the JUCE/Projucer
    # sense - we set explicit bus layouts instead in PluginProcessor.
)

juce_generate_juce_header(FLPToolkit)

target_sources(FLPToolkit PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/flp.h
    Source/flp.cpp
    Source/flphelper.h
    Source/flphelper.cpp
    Source/BatchProcessWindow.h
    Source/BatchProcessWindow.cpp
    Source/AutomationToolsWindow.h
    Source/AutomationToolsWindow.cpp
    Source/AutomationCurveView.h
    Source/AutomationCurveView.cpp
)

target_compile_definitions(FLPToolkit PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(FLPToolkit PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
