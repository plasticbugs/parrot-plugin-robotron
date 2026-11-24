# Robotron: 2084 (ROBOTRON.CVP): Williams' 1982 arcade game on the
# ColecoVision. The cart runs the arcade board (a 6809, the blitters, the
# sound board's 6808 and DAC: src/rb.c, from MAME 0.288) from your own ROMs
# on the card (/ROBOTRON), and src/rbcv.c sends the picture and the sound
# to the console as the frame kernel's lists. RESET goes back to the menu.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                     -> build/ROBOTRON.CVP
#
# and copy the .CVP into the card's /PLUGINS folder (docs/getting-started.md).
# Started from tools/new-plugin.py.
cmake_minimum_required(VERSION 3.13)
include(cmake/parrotcart.cmake)
project(robotron_2084 C CXX ASM)
parrotcart_init()

# The same source makes the same file, whenever it's built: __DATE__ and
# __TIME__ come from SOURCE_DATE_EPOCH, fixed here (2026-11-23).
set(BUILD_EPOCH 1795392000)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

set(EMU src/rb.c src/rbsnd.c src/rbrun.c)
# (swzip.c is the SDK's, copied: test/host/build.sh and the simulator build
# src/ and only some of the SDK, not swzip.c)
set(CARD src/rbcard.c src/rbroms.c src/swzip.c)
add_executable(robotron_2084 src/main.c src/rbcv.c ${EMU} ${CARD} ${PARROTCART_SDK}/midihello/vdp.c)
# a whole picture every TV frame: the frame kernel, reading controller 2 too
# (the fire stick)
parrotcart_frames(robotron_2084 CONTROLLER2)
target_include_directories(robotron_2084 PRIVATE src ${PARROTCART_SDK}/sdplayer)
# The emulation -O3, its state not split into a section a variable (every
# use would need its own address loaded); the picture (rbcv.c) -O2; the
# card's code runs once, at the start: built for size. test/rb/armcount.sh
# builds each file the same way.
set_source_files_properties(src/main.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter")
set_source_files_properties(${EMU} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O3;-fno-data-sections")
set_source_files_properties(src/rbcv.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-O2")
set_source_files_properties(${CARD} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-unused-parameter;-Os")
# core 0's stack 4K, core 1's 2K
target_compile_definitions(robotron_2084 PRIVATE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0
    PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x800)

# The features (docs/features.md). Each one links its code, sets up the
# USB port with only what it needs, and puts its flag in the .CVP's USES:
#   PADS KEYBOARD MOUSE MIDI_IN   the USB port as a host, through a powered hub
#   MIDI_OUT                      the USB port as a MIDI device, on a computer
#   PSRAM                         the 8 MB PSRAM
#   CARD                          the card: files and saves (a plugin with
#                                 OPENS needs it to read its file)
# To add one later, add its word here and its code to src/main.c.
cvcart_features(robotron_2084 PADS CARD)

# The plugin (docs/plugin-rules.md): the ID is yours for good; the version
# is the release's tag without its v.
cvcart_plugin(robotron_2084 ID parrotcart.robotron NAME "Robotron: 2084" VERSION 0.1.2 BY "Parrot Cart"
              ICON src/icon.txt README CART-README.md FILE ROBOTRON.CVP
              HOME /ROBOTRON USES usb-host)
