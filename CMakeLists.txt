cmake_minimum_required(VERSION 3.22)
project(AIMP_VST3_Bridge VERSION 2.0.0 LANGUAGES CXX C RC)

message(STATUS "Building against AIMP SDK v5.40 build 2650")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# Statically link MSVC runtime to prevent dependency issues on user machine
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Configure JUCE path relative to this project
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" CACHE PATH "Path to JUCE")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE directory not found at ${JUCE_DIR}. Please check the path.")
endif()

set(AIMP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/aimp-sdk" CACHE PATH "Path to AIMP SDK")
if(NOT EXISTS "${AIMP_SDK_DIR}/Sources/Cpp")
    message(FATAL_ERROR "AIMP SDK C++ headers not found at ${AIMP_SDK_DIR}/Sources/Cpp.")
endif()

# Add JUCE subdirectory
add_subdirectory("${JUCE_DIR}" JUCE)

function(configure_juce_target target enable_aimp_plugin enable_winamp_dsp)
    target_compile_definitions(${target}
        PRIVATE
            AIMP_VST3_BRIDGE_ENABLE_AIMP_PLUGIN=${enable_aimp_plugin}
            AIMP_VST3_BRIDGE_ENABLE_WINAMP_DSP=${enable_winamp_dsp}
            JUCE_PLUGINHOST_VST3=1
            JUCE_PLUGINHOST_VST=0
            JUCE_PLUGINHOST_AU=0
            JUCE_PLUGINHOST_LADSPA=0
            JUCE_PLUGINHOST_LV2=0
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
    )

    target_include_directories(${target}
        PRIVATE
            Source
            "${AIMP_SDK_DIR}/Sources/Cpp"
    )

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_gui_extra
            juce::juce_gui_basics
            juce::juce_data_structures
            juce::juce_events
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endfunction()

# Winamp-compatible DSP module: this is the audio processor AIMP should list
# in the "Plugins DSP" selector.
add_library(dsp_vst3_audio SHARED
    Source/dsp_vst3_main.cpp
    Source/OutProcClient.cpp
    Source/BridgeRuntime.cpp
    Source/dsp_vst3.rc
)
set_target_properties(dsp_vst3_audio PROPERTIES PREFIX "" SUFFIX ".dll" OUTPUT_NAME "dsp_vst3_bridge")
target_compile_definitions(dsp_vst3_audio PRIVATE
    AIMP_VST3_BRIDGE_ENABLE_AIMP_PLUGIN=0
    AIMP_VST3_BRIDGE_ENABLE_WINAMP_DSP=1
    AIMP_VST3_BRIDGE_OUT_OF_PROCESS=1
)
target_include_directories(dsp_vst3_audio PRIVATE Source)
target_link_libraries(dsp_vst3_audio PRIVATE juce::juce_core)

add_executable(VST3BridgeHost WIN32
    Source/OutProcHostMain.cpp
    Source/VST3HostEngine.cpp
    Source/HostWindow.cpp
    Source/BridgeRuntime.cpp
    Source/VST3Bridge.manifest
)
configure_juce_target(VST3BridgeHost 0 0)

add_executable(VST3BridgeScanner WIN32
    Source/ScannerMain.cpp
    Source/BridgeRuntime.cpp
    Source/VST3Bridge.manifest
)
configure_juce_target(VST3BridgeScanner 0 0)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BRIDGE_ARCH_SUFFIX 64)
else()
    set(BRIDGE_ARCH_SUFFIX 32)
endif()
set_target_properties(VST3BridgeHost PROPERTIES OUTPUT_NAME "VST3BridgeHost${BRIDGE_ARCH_SUFFIX}")
set_target_properties(VST3BridgeScanner PROPERTIES OUTPUT_NAME "VST3BridgeScanner${BRIDGE_ARCH_SUFFIX}")

enable_testing()
add_executable(BridgeRuntimeTests
    Source/BridgeRuntimeTests.cpp
    Source/BridgeRuntime.cpp
)
target_include_directories(BridgeRuntimeTests PRIVATE Source)
target_link_libraries(BridgeRuntimeTests PRIVATE juce::juce_core)
add_test(NAME BridgeRuntimeTests COMMAND BridgeRuntimeTests)
