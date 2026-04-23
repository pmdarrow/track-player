cmake_minimum_required(VERSION 3.22)

# Must be set BEFORE project() so downstream generators pick them up.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    # Default to host-arch so compile_commands.json is clang-tidy-compatible.
    # For distribution, override with: -DCMAKE_OSX_ARCHITECTURES=x86_64;arm64
endif()

project(SimpleAudioPlayer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit build/compile_commands.json for clang-tidy / IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SIMPLE_AUDIO_PLAYER_COPY_PLUGIN_AFTER_BUILD
    "Copy built plugin formats into the current user's plugin folders"
    ON
)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Treat all JUCE module headers as SYSTEM so tools (clang-tidy, -Wall) don't
# report findings inside third-party code. This is more reliable than
# --exclude-header-filter, which some clang-analyzer checks ignore.
get_property(_juce_targets DIRECTORY "${juce_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t ${_juce_targets})
    get_target_property(_incl ${_t} INTERFACE_INCLUDE_DIRECTORIES)
    if(_incl)
        set_target_properties(${_t} PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_incl}")
    endif()
endforeach()

juce_add_plugin(SimpleAudioPlayer
    COMPANY_NAME              "Peter Darrow"
    BUNDLE_ID                 "com.peterdarrow.simpleaudioplayer"
    PLUGIN_MANUFACTURER_CODE  Pdar
    PLUGIN_CODE               Sapl
    PRODUCT_NAME              "Simple Audio Player"
    FORMATS                   AU Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   ${SIMPLE_AUDIO_PLAYER_COPY_PLUGIN_AFTER_BUILD}
)

set(SIMPLE_AUDIO_PLAYER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PlayPauseButton.cpp
    Source/PlayPauseButton.h
    Source/PlaylistComponents.cpp
    Source/PlaylistComponents.h
    Source/SimpleAudioPlayerTheme.cpp
    Source/SimpleAudioPlayerTheme.h
)

target_sources(SimpleAudioPlayer PRIVATE ${SIMPLE_AUDIO_PLAYER_SOURCES})

target_compile_definitions(SimpleAudioPlayer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(SimpleAudioPlayer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_gui_basics
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ─── Developer tooling targets ──────────────────────────────────────────────
# `brew install llvm` provides both clang-format and clang-tidy on macOS. The
# keg-only bin dir is added as a HINT so the user doesn't have to tweak PATH.

find_program(CLANG_FORMAT_EXE
    NAMES clang-format
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
)
find_program(CLANG_TIDY_EXE
    NAMES clang-tidy
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin
)

if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${SIMPLE_AUDIO_PLAYER_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-format: reformatting sources in place"
        VERBATIM
    )

    add_custom_target(format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run -Werror ${SIMPLE_AUDIO_PLAYER_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-format: verifying sources are formatted"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found; 'format'/'format-check' targets disabled. Install with: brew install llvm")
endif()

if(CLANG_TIDY_EXE)
    # On macOS, CMake's Apple Clang compile commands don't include -isysroot
    # (Apple Clang finds the SDK implicitly), but brew's LLVM clang-tidy does
    # need it. Probe xcrun once at configure time and pass through via
    # --extra-arg so tidy can resolve system headers like TargetConditionals.h.
    set(CLANG_TIDY_EXTRA_ARGS "")
    if(APPLE)
        execute_process(
            COMMAND xcrun --show-sdk-path
            OUTPUT_VARIABLE SIMPLE_AUDIO_PLAYER_MACOS_SDK
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(SIMPLE_AUDIO_PLAYER_MACOS_SDK)
            list(APPEND CLANG_TIDY_EXTRA_ARGS
                --extra-arg=-isysroot
                --extra-arg=${SIMPLE_AUDIO_PLAYER_MACOS_SDK}
            )
        endif()
    endif()

    add_custom_target(tidy
        COMMAND ${CLANG_TIDY_EXE}
            -p ${CMAKE_BINARY_DIR}
            # clang-analyzer reports at the sink location (deep inside JUCE);
            # exclude _deps/ so those traces don't fail the run.
            --exclude-header-filter=.*/_deps/.*
            ${CLANG_TIDY_EXTRA_ARGS}
            ${SIMPLE_AUDIO_PLAYER_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "clang-tidy: analysing sources"
        VERBATIM
    )
else()
    message(STATUS "clang-tidy not found; 'tidy' target disabled. Install with: brew install llvm")
endif()
