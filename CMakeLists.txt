cmake_minimum_required(VERSION 3.22)
project(VOX_DNA_PRO VERSION 3.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Runtime estático en Windows: no hace falta instalar Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()

# JUCE: usa ./JUCE si existe, si no lo descarga automáticamente
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(VD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VD_FORMATS AU)
endif()

juce_add_plugin(VoxDnaPro
    COMPANY_NAME "VOX DNA"
    BUNDLE_ID com.voxdna.voxdnapro
    PLUGIN_MANUFACTURER_CODE VxDn
    PLUGIN_CODE VDna
    FORMATS ${VD_FORMATS}
    PRODUCT_NAME "VOX DNA PRO"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics EQ Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect)

# fuentes OFL incrustadas (ver Resources/Fonts/README-FONTS.txt)
juce_add_binary_data(VoxDnaAssets SOURCES
    Resources/Fonts/Rajdhani-Medium.ttf
    Resources/Fonts/Rajdhani-SemiBold.ttf
    Resources/Fonts/Rajdhani-Bold.ttf
    Resources/Fonts/VoxDisplay-Medium.ttf
    Resources/Fonts/VoxDisplay-Bold.ttf)

set(VD_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Widgets.cpp
    Source/UI/CreatePage.cpp
    Source/UI/Pages.cpp)

target_sources(VoxDnaPro PRIVATE ${VD_SOURCES})

target_include_directories(VoxDnaPro PRIVATE Source)

target_compile_definitions(VoxDnaPro PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VoxDnaPro
    PRIVATE
        VoxDnaAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Banco de pruebas offline (opcional): cmake -DVD_BUILD_TESTS=ON
option(VD_BUILD_TESTS "Build offline DSP/UI test runner" OFF)
if(VD_BUILD_TESTS)
    juce_add_console_app(VoxDnaTests PRODUCT_NAME "VoxDnaTests")
    target_sources(VoxDnaTests PRIVATE Tests/TestMain.cpp ${VD_SOURCES})
    target_include_directories(VoxDnaTests PRIVATE Source)
    target_compile_definitions(VoxDnaTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="VOX DNA PRO" JucePlugin_VersionString="3.1.0")
    target_link_libraries(VoxDnaTests PRIVATE
        VoxDnaAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
