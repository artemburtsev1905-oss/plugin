cmake_minimum_required(VERSION 3.22)
project(VocalAutoAssistant VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.17-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
juce_add_plugin(VocalAutoAssistant
    COMPANY_NAME "Vocal Tools"
    BUNDLE_ID "com.vocaltools.vocalautoassistant"
    PLUGIN_MANUFACTURER_CODE Vctl
    PLUGIN_CODE Va02
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3
    PRODUCT_NAME "Vocal Auto Assistant"
)
juce_generate_juce_header(VocalAutoAssistant)
target_sources(VocalAutoAssistant PRIVATE Source/Plugin.cpp)
target_link_libraries(VocalAutoAssistant PRIVATE juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(VocalAutoAssistant PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
