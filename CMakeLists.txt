# Vektur Vortex - JUCE plugin (AUv3 / Standalone / VST3 / AU) around the verified C++ engine in ../Core.
# Configure:  cmake -S Juce -B build-juce -G Xcode -DCMAKE_SYSTEM_NAME=iOS   (iOS AUv3 + standalone)
#             cmake -S Juce -B build-juce -G Xcode                            (macOS AU / AUv3 / VST3 / Standalone)
# JUCE is fetched automatically (8.0.6) unless -DJUCE_DIR=/path/to/JUCE is given. See ../docs/JUCE.md.
cmake_minimum_required(VERSION 3.22)
project(VectaVortexJuce VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(JUCE_DIR)
  add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.6 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

# ---- the DSP engine (plain C++, identical to the one the web-reference tests verify)
add_library(vvengine STATIC ../Bridge/vv_engine.cpp)
target_include_directories(vvengine PUBLIC ../Bridge ../Core/include)
target_compile_features(vvengine PUBLIC cxx_std_17)
target_compile_options(vvengine PRIVATE -ffp-contract=off -O3)

set(VV_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Assets)

set(VV_SOURCES
  Source/Store.cpp Source/Widgets.cpp Source/Screens.cpp Source/EqGraph.cpp Source/ArpSteps.cpp
  Source/AuthoringTools.cpp Source/DawRecorderUi.cpp Source/SampleEditor.cpp Source/ScreensMod.cpp Source/ScreensMixer.cpp Source/ScreensAuto.cpp Source/ScreensRoll.cpp Source/ScreensSeq.cpp Source/ScreensFx.cpp Source/XYPads.cpp Source/VoidView.cpp Source/WarpView.cpp Source/GrainBridgeView.cpp
  Source/PluginProcessor.cpp Source/PluginEditor.cpp)

if(APPLE)
  set(VV_FORMATS AUv3 Standalone VST3)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(APPEND VV_FORMATS AU)
  endif()
else()
  set(VV_FORMATS Standalone VST3)
endif()

juce_add_plugin(VectaVortex
  COMPANY_NAME "Dream Music Temple"
  BUNDLE_ID com.dreammusictemple.vectavortex
  PLUGIN_MANUFACTURER_CODE DMTm
  PLUGIN_CODE Vvtx
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  FORMATS ${VV_FORMATS}
  PRODUCT_NAME "Vektur Vortex"
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  FILE_SHARING_ENABLED TRUE
  DOCUMENT_BROWSER_ENABLED TRUE
  IPHONE_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
  IPAD_SCREEN_ORIENTATIONS UIInterfaceOrientationPortrait UIInterfaceOrientationLandscapeLeft UIInterfaceOrientationLandscapeRight
  HARDENED_RUNTIME_ENABLED TRUE)

target_sources(VectaVortex PRIVATE ${VV_SOURCES})
target_compile_definitions(VectaVortex PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_ALSA=0 JUCE_JACK=0
  VV_ASSETS_DIR="${VV_ASSETS_DIR}")
target_link_libraries(VectaVortex PRIVATE vvengine
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# Assets travel inside the bundle (Resources/Assets/...). Plugin code finds them next to its binary (see PluginProcessor.cpp).
if(APPLE)
  file(GLOB_RECURSE VV_ASSET_FILES CONFIGURE_DEPENDS ${VV_ASSETS_DIR}/*)
  foreach(f ${VV_ASSET_FILES})
    file(RELATIVE_PATH rel ${VV_ASSETS_DIR} ${f})
    get_filename_component(d ${rel} DIRECTORY)
    set_source_files_properties(${f} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/Assets/${d}")
    target_sources(VectaVortex PRIVATE ${f})
  endforeach()
endif()

# ---- headless UI test (Linux/macOS): builds every screen offscreen and writes PNG snapshots
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  juce_add_console_app(vv_ui_snapshot PRODUCT_NAME vv_ui_snapshot)
  target_sources(vv_ui_snapshot PRIVATE Tests/ui_snapshot.cpp ${VV_SOURCES})
  target_compile_definitions(vv_ui_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_ALSA=0 JUCE_JACK=0
    JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_STANDALONE_APPLICATION=1 VV_ASSETS_DIR="${VV_ASSETS_DIR}" VV_TESTING=1)
  target_link_libraries(vv_ui_snapshot PRIVATE vvengine juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra juce::juce_recommended_config_flags)
endif()
