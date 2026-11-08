cmake_minimum_required(VERSION 3.22)
project(PrioraBusBust05 VERSION 0.5.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.17-security GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PrioraBusBust05
    COMPANY_NAME "DJ Begemot"
    BUNDLE_ID "com.djbegemot.priorabusbust05"
    PLUGIN_MANUFACTURER_CODE Bgmt
    PLUGIN_CODE Pr05
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3
    PRODUCT_NAME "Priora Bus Bust05 by DJ Begemot"
)
juce_add_binary_data(PrioraGuiAssets SOURCES Resources/background.jpg)
juce_generate_juce_header(PrioraBusBust05)
target_sources(PrioraBusBust05 PRIVATE Source/Plugin.cpp)
target_link_libraries(PrioraBusBust05 PRIVATE
    PrioraGuiAssets juce::juce_audio_utils juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
target_compile_definitions(PrioraBusBust05 PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
