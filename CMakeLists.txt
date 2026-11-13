# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
cmake_minimum_required(VERSION 3.16)
project(Kreisel LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # (the plugins are shared libraries)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(K_FX_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
add_custom_command(
  OUTPUT "${K_FX_GEN_DIR}/pd_tables.h"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${K_FX_GEN_DIR}"
  COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pendel-dsp/gen_tables.py" "${K_FX_GEN_DIR}/pd_tables.h" --fs 44100
  DEPENDS third_party/pendel-dsp/gen_tables.py
  COMMENT "Generating fixed-point Pendel effect tables")
add_custom_target(kreisel_fx_tables DEPENDS "${K_FX_GEN_DIR}/pd_tables.h")
# the source groups, as every platform's build takes them (platforms/platforms.toml, tools/kbuild.py)
foreach(G core audio fx ui store_file plugin_load plugin_image)
  execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/kbuild.py" --group ${G} --cmake
                  OUTPUT_VARIABLE K_SRC_${G} OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE K_RC)
  if(NOT K_RC EQUAL 0)
    message(FATAL_ERROR "tools/kbuild.py --group ${G} failed (Python 3.11+ for tomllib)")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS platforms/platforms.toml)
add_library(kreisel_core STATIC ${K_SRC_core} ${K_SRC_audio} ${K_SRC_fx})
add_dependencies(kreisel_core kreisel_fx_tables)
target_include_directories(kreisel_core PRIVATE "${K_FX_GEN_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pendel-dsp")
target_include_directories(kreisel_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(UNIX AND NOT APPLE)
  target_link_libraries(kreisel_core PUBLIC m)
endif()
if(MSVC)
  target_compile_options(kreisel_core PRIVATE /W4)
else()
  target_compile_options(kreisel_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(kreisel_core_test tests/core_test.c)
target_link_libraries(kreisel_core_test PRIVATE kreisel_core)
add_test(NAME kreisel_core_test COMMAND kreisel_core_test)

add_executable(kreisel_math_test tests/math_test.c)
target_include_directories(kreisel_math_test PRIVATE src)
target_link_libraries(kreisel_math_test PRIVATE m)
add_test(NAME kreisel_math_test COMMAND kreisel_math_test)

# Outside plugins on a desktop: the loader (shared libraries from a folder), and FM2, a plugin built as one (build/plugins/)
add_library(kreisel_store_file STATIC ${K_SRC_store_file})
target_link_libraries(kreisel_store_file PUBLIC kreisel_core)
add_library(kreisel_plugin_load STATIC ${K_SRC_plugin_load})
target_link_libraries(kreisel_plugin_load PUBLIC kreisel_core ${CMAKE_DL_LIBS})
add_library(kreisel_plugin_fm2 MODULE plugins/fm2/fm2.c)
target_include_directories(kreisel_plugin_fm2 PRIVATE sdk src)
set_target_properties(kreisel_plugin_fm2 PROPERTIES PREFIX "" OUTPUT_NAME fm2 C_VISIBILITY_PRESET hidden
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")
add_library(kreisel_plugin_flat MODULE plugins/flat/flat.c)   # FLAT: a plugin that lays out its own screens
target_include_directories(kreisel_plugin_flat PRIVATE sdk src)
set_target_properties(kreisel_plugin_flat PROPERTIES PREFIX "" OUTPUT_NAME flat C_VISIBILITY_PRESET hidden
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")
add_library(kreisel_plugin_tilt MODULE plugins/tilt/tilt.c)   # TILT: an effect that lays out its own insert (the FX page)
target_include_directories(kreisel_plugin_tilt PRIVATE sdk src)
set_target_properties(kreisel_plugin_tilt PROPERTIES PREFIX "" OUTPUT_NAME tilt C_VISIBILITY_PRESET hidden
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins")
# BEEP: the SDK's example (docs/PLUGIN_SDK.md), in a folder of its own (not the one the desktop and plugin_test load)
add_library(kreisel_plugin_beep MODULE plugins/beep/beep.c)
target_include_directories(kreisel_plugin_beep PRIVATE sdk src)
set_target_properties(kreisel_plugin_beep PROPERTIES PREFIX "" OUTPUT_NAME beep SUFFIX ".so" C_VISIBILITY_PRESET hidden
                      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sdk_example")
add_executable(kreisel_sdk_example_test tests/sdk_example_test.c)
target_link_libraries(kreisel_sdk_example_test PRIVATE kreisel_plugin_load)
target_compile_definitions(kreisel_sdk_example_test PRIVATE KREISEL_SDK_EXAMPLE="${CMAKE_CURRENT_BINARY_DIR}/sdk_example/beep.so")
add_dependencies(kreisel_sdk_example_test kreisel_plugin_beep)
add_test(NAME kreisel_sdk_example_test COMMAND kreisel_sdk_example_test)
add_executable(kreisel_plugin_test tests/plugin_test.c)
target_link_libraries(kreisel_plugin_test PRIVATE kreisel_plugin_load)
target_compile_definitions(kreisel_plugin_test PRIVATE KREISEL_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/plugins")
add_dependencies(kreisel_plugin_test kreisel_plugin_fm2 kreisel_plugin_flat kreisel_plugin_tilt)
add_test(NAME kreisel_plugin_test COMMAND kreisel_plugin_test)
add_library(kreisel_plugin_image STATIC ${K_SRC_plugin_image})   # plugins as images in flash (the FM-1)
target_link_libraries(kreisel_plugin_image PUBLIC kreisel_core)
if(NOT MSVC)
  target_compile_options(kreisel_plugin_image PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(kreisel_plugin_image_test tests/plugin_image_test.c)
target_link_libraries(kreisel_plugin_image_test PRIVATE kreisel_plugin_image)
add_test(NAME kreisel_plugin_image_test COMMAND kreisel_plugin_image_test)

add_executable(kreisel_audio_test tests/audio_test.c)
target_link_libraries(kreisel_audio_test PRIVATE kreisel_core)
add_test(NAME kreisel_audio_test COMMAND kreisel_audio_test)

add_executable(kreisel_arp_test tests/arp_test.c)
target_link_libraries(kreisel_arp_test PRIVATE kreisel_core)
add_test(NAME kreisel_arp_test COMMAND kreisel_arp_test)

add_executable(kreisel_phase_env_test tests/phase_env_test.c)
target_link_libraries(kreisel_phase_env_test PRIVATE kreisel_core)
add_test(NAME kreisel_phase_env_test COMMAND kreisel_phase_env_test)
add_executable(kreisel_vector_test tests/vector_test.c)
target_link_libraries(kreisel_vector_test PRIVATE kreisel_core)
add_test(NAME kreisel_vector_test COMMAND kreisel_vector_test)

add_executable(kreisel_io_test tests/io_test.c)
target_link_libraries(kreisel_io_test PRIVATE kreisel_core)
add_test(NAME kreisel_io_test COMMAND kreisel_io_test)
add_executable(kreisel_ctl_test tests/ctl_test.c)
target_link_libraries(kreisel_ctl_test PRIVATE kreisel_store_file)
add_test(NAME kreisel_ctl_test COMMAND kreisel_ctl_test)
add_executable(kreisel_param_test tests/param_test.c)
target_link_libraries(kreisel_param_test PRIVATE kreisel_core)
add_test(NAME kreisel_param_test COMMAND kreisel_param_test)
add_executable(kreisel_fx_test tests/fx_test.c)
target_link_libraries(kreisel_fx_test PRIVATE kreisel_core)
add_test(NAME kreisel_fx_test COMMAND kreisel_fx_test)
add_executable(kreisel_fx_golden_test tests/fx_golden_test.c)
target_link_libraries(kreisel_fx_golden_test PRIVATE kreisel_core)
add_test(NAME kreisel_fx_golden_test COMMAND kreisel_fx_golden_test)
add_executable(kreisel_fx_third_test tests/fx_third_test.c)
target_link_libraries(kreisel_fx_third_test PRIVATE kreisel_core)
add_test(NAME kreisel_fx_third_test COMMAND kreisel_fx_third_test)

add_executable(kreisel_clock_test tests/clock_test.c)
target_link_libraries(kreisel_clock_test PRIVATE kreisel_core)
add_test(NAME kreisel_clock_test COMMAND kreisel_clock_test)

add_executable(kreisel_seqtool_test tests/seqtool_test.c)
target_link_libraries(kreisel_seqtool_test PRIVATE kreisel_core)
add_test(NAME kreisel_seqtool_test COMMAND kreisel_seqtool_test)
add_executable(kreisel_song_test tests/song_test.c)
target_link_libraries(kreisel_song_test PRIVATE kreisel_core)
add_test(NAME kreisel_song_test COMMAND kreisel_song_test)
add_executable(kreisel_drum_lane_test tests/drum_lane_test.c)   # an outside drum's values for each lane (a drum built on the SDK in it)
target_include_directories(kreisel_drum_lane_test PRIVATE sdk)
target_link_libraries(kreisel_drum_lane_test PRIVATE kreisel_core)
add_test(NAME kreisel_drum_lane_test COMMAND kreisel_drum_lane_test)
add_executable(kreisel_save_test tests/save_test.c)
target_link_libraries(kreisel_save_test PRIVATE kreisel_core)
add_test(NAME kreisel_save_test COMMAND kreisel_save_test)

# Samples (src/sample): WAV reading and the sample store, desktop builds only (Linux, macOS), never the FM-1
file(GLOB K_SRC_sample CONFIGURE_DEPENDS src/sample/*.c)
add_library(kreisel_sample STATIC ${K_SRC_sample})
target_include_directories(kreisel_sample PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(NOT MSVC)
  target_compile_options(kreisel_sample PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(kreisel_sample_test tests/sample_test.c)
target_link_libraries(kreisel_sample_test PRIVATE kreisel_sample)
add_test(NAME kreisel_sample_test COMMAND kreisel_sample_test)

# Headless core builds must not require the UI font/icon toolchain.
# Keep UI tests available without SDL by enabling KREISEL_BUILD_UI explicitly.
option(KREISEL_BUILD_SDL "Build the SDL2 prototype" ON)
option(KREISEL_BUILD_UI "Build portable renderer and UI tests" ${KREISEL_BUILD_SDL})
if(KREISEL_BUILD_SDL AND NOT KREISEL_BUILD_UI)
  message(FATAL_ERROR "KREISEL_BUILD_SDL requires KREISEL_BUILD_UI=ON")
endif()

if(KREISEL_BUILD_UI)
  # The UI canvas and screens: no SDL here. The fonts are baked at build time (Pillow and fontTools):
  # tools/gen_aa_font.py -> build/gen/ui_fonts.h
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(KREISEL_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
  add_custom_command(
    OUTPUT "${KREISEL_GEN_DIR}/ui_fonts.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KREISEL_GEN_DIR}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_aa_font.py" "${KREISEL_GEN_DIR}/ui_fonts.h" --preset inter-tight
    DEPENDS tools/gen_aa_font.py tools/aa_raster.py assets/fonts/InterTight-VF.ttf
    COMMENT "Baking the UI fonts")
  add_custom_command(
    OUTPUT "${KREISEL_GEN_DIR}/ui_vfonts.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KREISEL_GEN_DIR}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_vec_font.py" "${KREISEL_GEN_DIR}/ui_vfonts.h"
    DEPENDS tools/gen_vec_font.py assets/fonts/InterTight-VF.ttf
    COMMENT "Baking the UI font outlines")
  file(GLOB KREISEL_ICON_SVGS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
  add_custom_command(
    OUTPUT "${KREISEL_GEN_DIR}/ui_icons.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${KREISEL_GEN_DIR}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_icons.py" "${KREISEL_GEN_DIR}/ui_icons.h"
    DEPENDS tools/gen_icons.py ${KREISEL_ICON_SVGS}
    COMMENT "Baking the icons (Fukiai)")
  add_custom_target(kreisel_fonts DEPENDS "${KREISEL_GEN_DIR}/ui_fonts.h" "${KREISEL_GEN_DIR}/ui_vfonts.h" "${KREISEL_GEN_DIR}/ui_icons.h")
  add_library(kreisel_ui STATIC ${K_SRC_ui})
  add_dependencies(kreisel_ui kreisel_fonts)
  target_include_directories(kreisel_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/ui" PRIVATE "${KREISEL_GEN_DIR}")
  target_link_libraries(kreisel_ui PUBLIC kreisel_core)

  # the profile table is made from platforms/targets/*.toml: it must be current
  add_test(NAME kreisel_profiles_current COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/kbuild.py" --profiles --check)
  # design rule 2: the UI and the SDL platform write no value directly (the core's commands only; exceptions listed with a reason)
  add_test(NAME kreisel_ui_writes COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_ui_writes.py" "${CMAKE_CURRENT_SOURCE_DIR}")
  add_executable(kreisel_ui_test tests/ui_test.c)
  target_link_libraries(kreisel_ui_test PRIVATE kreisel_ui)
  add_test(NAME kreisel_ui_test COMMAND kreisel_ui_test)
  add_executable(kreisel_surface_test tests/surface_test.c)   # a touch screen's surface: the faces and the fingers
  target_link_libraries(kreisel_surface_test PRIVATE kreisel_ui)
  add_test(NAME kreisel_surface_test COMMAND kreisel_surface_test)
  add_executable(kreisel_plugin_ui_test tests/plugin_ui_test.c)   # a plugin's own layouts, drawn on each kind of screen
  target_link_libraries(kreisel_plugin_ui_test PRIVATE kreisel_ui kreisel_plugin_load)
  target_compile_definitions(kreisel_plugin_ui_test PRIVATE KREISEL_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/plugins")
  add_dependencies(kreisel_plugin_ui_test kreisel_plugin_fm2 kreisel_plugin_flat kreisel_plugin_tilt)
  add_test(NAME kreisel_plugin_ui_test COMMAND kreisel_plugin_ui_test)

  if(UNIX)   # MIDI over Wi-Fi (RTP-MIDI, platforms/net): the listener against an initiator played over the loopback
    add_executable(rtpmidi_test tests/rtpmidi_test.c platforms/net/kr_rtpmidi.c)
    target_include_directories(rtpmidi_test PRIVATE platforms/net)
    add_test(NAME rtpmidi_test COMMAND rtpmidi_test)
    add_executable(mdns_test tests/mdns_test.c platforms/net/kr_mdns.c)   # its Bonjour announcement's packets
    target_include_directories(mdns_test PRIVATE platforms/net)
    add_test(NAME mdns_test COMMAND mdns_test)
  endif()

  # Host tool: every page at the FM-1 and handheld sizes as PNGs (`cmake --build build --target shots` -> build/shots/)
  add_executable(kreisel_shots tools/ui_shots.c)
  target_link_libraries(kreisel_shots PRIVATE kreisel_ui)
  # the same screens as shapes for Sketch (tools/ui_sketch.py): the UI built again with G_RECORD (src/ui/gfx_rec.h)
  add_executable(kreisel_sketch tools/ui_shots.c ${K_SRC_ui})
  add_dependencies(kreisel_sketch kreisel_fonts)
  target_compile_definitions(kreisel_sketch PRIVATE G_RECORD)
  target_include_directories(kreisel_sketch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/ui" "${KREISEL_GEN_DIR}")
  target_link_libraries(kreisel_sketch PRIVATE kreisel_core)
  # the pills under the row in hand's labels, on every screen: on the screen, in their frame, on no knob, label or other pill
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME kreisel_ui_pill_check COMMAND sh -c "rm -rf pill_rec && mkdir pill_rec && $<TARGET_FILE:kreisel_sketch> pill_rec >/dev/null && ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/ui_pill_check.py pill_rec fm1_ && ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/ui_pill_check.py pill_rec zero_")
  endif()
  add_custom_target(shots COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shots"
                    COMMAND kreisel_shots "${CMAKE_CURRENT_BINARY_DIR}/shots" DEPENDS kreisel_shots)
endif()

if(KREISEL_BUILD_SDL)
  find_package(SDL2 CONFIG QUIET)
  if(TARGET SDL2::SDL2)
    set(KREISEL_SDL_TARGET SDL2::SDL2)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    set(KREISEL_SDL_TARGET PkgConfig::SDL2)
  endif()
  # The desktop app: eight tracks (KREISEL_APP_TRACKS). The libraries keep the build's own count (four unless -DK_TRACKS=N: the
  # tests and the screenshots, the FM-1's), so the app is built from the sources with its own when they differ.
  set(KREISEL_APP_TRACKS 8 CACHE STRING "tracks of the desktop app (1..8)")
  if(CMAKE_C_FLAGS MATCHES "K_TRACKS=([0-9]+)")
    set(K_LIB_TRACKS ${CMAKE_MATCH_1})
  else()
    set(K_LIB_TRACKS 4)
  endif()
  set(K_SDL_SRC platforms/sdl/main.c platforms/sdl/blit.c platforms/sdl/input.c platforms/sdl/panel.c platforms/sdl/desk.c platforms/sdl/midi.c platforms/sdl/ioconf.c platforms/sdl/controllers.c)
  if(UNIX AND NOT APPLE)   # MIDI in on Linux (platforms/sdl/midi.c): RTP-MIDI and its mDNS (platforms/net), the ALSA sequencer when found
    list(APPEND K_SDL_SRC platforms/net/kr_rtpmidi.c platforms/net/kr_mdns.c)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(ALSA QUIET alsa)
    endif()
  endif()
  if(K_LIB_TRACKS EQUAL KREISEL_APP_TRACKS)
    add_executable(kreisel ${K_SDL_SRC})
    target_link_libraries(kreisel PRIVATE kreisel_ui kreisel_plugin_load kreisel_store_file ${KREISEL_SDL_TARGET})
  else()
    add_executable(kreisel ${K_SDL_SRC} ${K_SRC_core} ${K_SRC_audio} ${K_SRC_fx} ${K_SRC_ui} ${K_SRC_store_file} ${K_SRC_plugin_load})
    add_dependencies(kreisel kreisel_fx_tables kreisel_fonts)
    target_compile_definitions(kreisel PRIVATE K_TRACKS=${KREISEL_APP_TRACKS})
    if(NOT MSVC)   # (optimised even in a Debug tree: a high-density screen's canvas is 16 times the pixels)
      target_compile_options(kreisel PRIVATE -O2)
    endif()
    target_include_directories(kreisel PRIVATE src src/ui "${KREISEL_GEN_DIR}" "${K_FX_GEN_DIR}" third_party/pendel-dsp)
    target_link_libraries(kreisel PRIVATE ${CMAKE_DL_LIBS} ${KREISEL_SDL_TARGET})
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(kreisel PRIVATE m)   # (Linux: libm is its own library, in either way of building it)
  endif()
  target_include_directories(kreisel PRIVATE platforms/net)   # (kr_rtpmidi.h, kr_mdns.h)
  if(APPLE)
    target_link_libraries(kreisel PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")   # MIDI in (platforms/sdl/midi.c)
  elseif(ALSA_FOUND)
    target_compile_definitions(kreisel PRIVATE KREISEL_ALSA=1)
    target_include_directories(kreisel PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(kreisel PRIVATE ${ALSA_LIBRARIES})
  endif()
  if(APPLE)   # a Mac app: build/Fune.app (its icon, its name: the name shown, 2026-11-09); build/kreisel stays, a link to the program inside (the dev tools' way)
    file(STRINGS src/kreisel_version.h K_VER_LINE REGEX "define KREISEL_VERSION_SHORT")
    string(REGEX MATCH "\"([^\"]+)\"" _ "${K_VER_LINE}")
    set(KREISEL_VERSION "${CMAKE_MATCH_1}")
    configure_file(platforms/sdl/Info.plist.in "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    set_source_files_properties(assets/app/Kreisel.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(kreisel PRIVATE assets/app/Kreisel.icns)
    set_target_properties(kreisel PROPERTIES MACOSX_BUNDLE TRUE OUTPUT_NAME Fune
                          MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
    add_custom_command(TARGET kreisel POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E create_symlink Fune.app/Contents/MacOS/Fune "${CMAKE_CURRENT_BINARY_DIR}/kreisel")
  endif()
  add_executable(kreisel_panel_test tests/panel_test.c platforms/sdl/panel.c)
  target_include_directories(kreisel_panel_test PRIVATE platforms/sdl)
  target_link_libraries(kreisel_panel_test PRIVATE kreisel_ui ${KREISEL_SDL_TARGET})
  add_test(NAME kreisel_panel_test COMMAND kreisel_panel_test)
endif()

# The tests check with assert(), some around a call that must run (assert(ui_draw_init(...))): kept on in every build type, a
# Release build's NDEBUG notwithstanding (there the call was dropped and the test crashed on what it had not set up)
foreach(t kreisel_core_test kreisel_math_test kreisel_plugin_test kreisel_audio_test kreisel_arp_test kreisel_phase_env_test
          kreisel_vector_test kreisel_fx_test kreisel_fx_golden_test kreisel_fx_third_test kreisel_clock_test kreisel_song_test kreisel_seqtool_test kreisel_save_test kreisel_sample_test kreisel_ui_test
          rtpmidi_test mdns_test kreisel_panel_test kreisel_plugin_ui_test
          kreisel_sdk_example_test kreisel_plugin_image_test)
  if(TARGET ${t})
    target_compile_options(${t} PRIVATE -UNDEBUG)
  endif()
endforeach()
