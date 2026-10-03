cmake_minimum_required(VERSION 3.22)

# Must precede project(): these drive the compiler checks.
#
# Build a universal binary. Left unset, CMake targets its OWN architecture — and
# an x86_64 CMake (Intel Homebrew under Rosetta) then silently produces an
# Intel-only plug-in, which native arm64 hosts skip without any error. arm64
# macOS starts at 11.0, so the deployment target has to clear that.
if(APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING
            "macOS build architectures" FORCE)
    endif()
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "Minimum macOS version" FORCE)
    endif()
endif()

project(Timaband VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: either an absolute checkout (-DJUCE_DIR=...) or the macOS default
# install location.
set(JUCE_DIR "/Applications/JUCE" CACHE PATH "Path to a JUCE source checkout")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR} — pass -DJUCE_DIR=/path/to/JUCE")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

set(TIMABAND_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp
    Source/dsp/Engine.cpp
    Source/ui/BandPanel.cpp
    Source/ui/Controls.cpp
    Source/ui/CrossoverDisplay.cpp
    Source/ui/LookAndFeel.cpp
    Source/ui/MeterPanel.cpp
    Source/ui/Spectrum.cpp)

# PRODUCT_NAME stays ASCII: it names the bundle and the target. PLUGIN_NAME is
# what the host lists.
juce_add_plugin(Timaband
    COMPANY_NAME            "Sungam"
    BUNDLE_ID               "com.sungam.timaband"
    PLUGIN_MANUFACTURER_CODE Sngm
    PLUGIN_CODE             Tmbd
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "Timaband"
    PLUGIN_NAME             "Tímaband"
    DESCRIPTION             "Multiband transient designer"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    # The standalone reads the audio input; macOS kills an app that opens the
    # microphone without a usage string.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Tímaband processes the audio input of your interface."
    VST3_CATEGORIES         Fx Dynamics
    AU_MAIN_TYPE            kAudioUnitType_Effect)

target_sources(Timaband PRIVATE ${TIMABAND_SOURCES})

target_compile_definitions(Timaband PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0)

target_include_directories(Timaband PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")

target_link_libraries(Timaband
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Dev tools, -DTIMABAND_TOOLS=ON:
#   dsp_check   runs the Engine offline against the checks in tools/dsp_check.cpp,
#               without a plug-in wrapper or a host.
#   panel_shot  renders the real editor, fed with synthetic drums, to docs/*.png.
#   ui_check    drives the editor's controls headlessly and checks what they do.
#   panel_shot_sungam, ui_check_sungam
#               the same two with the Sungam recolour (TIMABAND_THEME_SUNGAM=1);
#               panel_shot_sungam writes docs/sungam-mockup/plugin-sungam*.png.
option(TIMABAND_TOOLS "Build the offline DSP checker and the editor renderer" OFF)
if(TIMABAND_TOOLS)
    juce_add_console_app(dsp_check PRODUCT_NAME "dsp_check" NEEDS_CURL FALSE)
    target_sources(dsp_check PRIVATE tools/dsp_check.cpp Source/dsp/Engine.cpp)
    target_compile_definitions(dsp_check PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_include_directories(dsp_check PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_link_libraries(dsp_check
        PRIVATE
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # The editor tools build once per palette: the default dark theme, and the Sungam
    # recolour (TIMABAND_THEME_SUNGAM), which renders into docs/sungam-mockup.
    function(timaband_add_editor_tool target source docsDir)     # extra arguments are definitions
        juce_add_console_app(${target} PRODUCT_NAME "${target}" NEEDS_CURL FALSE)
        target_sources(${target} PRIVATE ${source} ${TIMABAND_SOURCES})
        target_compile_definitions(${target} PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
            JucePlugin_Name="Timaband"
            TIMABAND_DOCS_DIR="${docsDir}"
            ${ARGN})
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
        target_link_libraries(${target}
            PRIVATE
                juce::juce_audio_utils
                juce::juce_dsp
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)
    endfunction()

    set(timabandDocs "${CMAKE_CURRENT_SOURCE_DIR}/docs")
    timaband_add_editor_tool(panel_shot tools/panel_shot.cpp "${timabandDocs}")
    timaband_add_editor_tool(panel_shot_sungam tools/panel_shot.cpp "${timabandDocs}/sungam-mockup" TIMABAND_THEME_SUNGAM=1)
    timaband_add_editor_tool(ui_check tools/ui_check.cpp "${timabandDocs}")
    timaband_add_editor_tool(ui_check_sungam tools/ui_check.cpp "${timabandDocs}/sungam-mockup" TIMABAND_THEME_SUNGAM=1)
endif()
