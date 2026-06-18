cmake_minimum_required(VERSION 3.22)
project(PlinkoDelayPlugin VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Reuse the deterministic physics + sound engine (this also fetches Box2D v3.1.1).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_BINARY_DIR}/core_build)

juce_add_plugin(PlinkoDelay
    PRODUCT_NAME "Plinko Delay"
    COMPANY_NAME "SusSounds"
    PLUGIN_MANUFACTURER_CODE Suss
    PLUGIN_CODE Plnk
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PlinkoDelay PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/BoardComponent.cpp)

target_include_directories(PlinkoDelay PRIVATE Source)

target_compile_definitions(PlinkoDelay PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PlinkoDelay PRIVATE
    plinko_core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
