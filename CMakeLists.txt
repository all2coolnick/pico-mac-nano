# CMakeLists
#
# MIT License
#
# Copyright (c) 2021, 2024 Matt Evans
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
#
cmake_minimum_required(VERSION 3.13)

# Options that should be defined when initialising the build
# directory with cmake, e.g. "cmake .. -DOPTION=true":
#

option(PICO_ZERO "Running on WaveShare Pico Zero" true) 

option(USE_SD "Build in SD support" true)
set(SD_TX 3 CACHE STRING "SD SPI TX pin")
set(SD_RX 4 CACHE STRING "SD SPI RX pin")
set(SD_SCK 2 CACHE STRING "SD SPI SCK pin")
set(SD_CS 5 CACHE STRING "SD SPI CS pin")
set(SD_MHZ 5 CACHE STRING "SD SPI speed in MHz")

option(TFT_2P "Output to TFT" true) # Control pin settings for 2-inch Portrait TFT
set(TFT_SPI_MOSI 3 CACHE STRING "TFT SPI TX pin")
set(TFT_SPI_CLK 2 CACHE STRING "TFT SPI SCK pin")
set(TFT_SPI_CS 14 CACHE STRING "TFT SPI CS pin")
set(TFT_RESET 15 CACHE STRING "TFT hard reset pin")

option(USB_PC "Control from a PC over USB, instead of a USB keyboard and mouse" false)

option(BEEP "Generate startup Beep" true) 
set(GPIO_AUDIO 10 CACHE STRING "Audio signal out pin")
set(GPIO_AUDIO_INV 11 CACHE STRING "Inverse of audio signal out pin")

# Mac screen size.  Must match the ROM in incbin/umac-rom.h, which is patched
# for one size (umac: "make DISP_WIDTH=... DISP_HEIGHT=...", then "main -W").
set(DISP_WIDTH 240 CACHE STRING "Mac screen width, in pixels")
set(DISP_HEIGHT 180 CACHE STRING "Mac screen height, in pixels")
add_compile_definitions(DISP_WIDTH=${DISP_WIDTH} DISP_HEIGHT=${DISP_HEIGHT})

# See below, -DMEMSIZE=<size in KB> will configure umac's memory size,
# overriding defaults.

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(firmware)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# For TUSB host stuff:
set(FAMILY rp2040)
set(BOARD raspberry_pi_pico)

set(TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)

# umac subproject (and Musashi sub-subproject)
set(UMAC_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/umac)
set(UMAC_MUSASHI_PATH ${UMAC_PATH}/external/Musashi)
set(UMAC_INCLUDE_PATHS ${UMAC_PATH}/include ${UMAC_MUSASHI_PATH})

# This isn't very nice, but hey it's Sunday :p
set(UMAC_SOURCES
  ${UMAC_PATH}/src/disc.c
  ${UMAC_PATH}/src/main.c
  ${UMAC_PATH}/src/rom.c
  ${UMAC_PATH}/src/scc.c
  ${UMAC_PATH}/src/via.c
  ${UMAC_MUSASHI_PATH}/m68kcpu.c
  ${UMAC_MUSASHI_PATH}/m68kdasm.c
  ${UMAC_MUSASHI_PATH}/m68kops.c
  ${UMAC_MUSASHI_PATH}/softfloat/softfloat.c
  )

set(MEMSIZE 128 CACHE STRING "Memory size, in KB")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -DPICO -DMUSASHI_CNF=\\\"../include/m68kconf.h\\\" -DUMAC_MEMSIZE=${MEMSIZE}")

if (USE_SD)
   add_compile_definitions(USE_SD=1)
   set(FF_DISABLE_RTC ${PICO_RP2350})  # RP2350 doesn't have RTC, so disable it
   add_subdirectory(external/no-OS-FatFS-SD-SPI-RPi-Pico/FatFs_SPI build)
   set(EXTRA_SD_SRC src/sd_hw_config.c)
   set(EXTRA_SD_LIB FatFs_SPI)
   add_compile_definitions(SD_TX=${SD_TX} SD_RX=${SD_RX} SD_SCK=${SD_SCK} SD_CS=${SD_CS} SD_MHZ=${SD_MHZ})
endif()

if (TFT_2P)
   add_compile_definitions(TFT_2P=1)
   set(EXTRA_TFT2P_SRC src/tft_2p.c)
   add_compile_definitions(TFT_SPI_MOSI=${TFT_SPI_MOSI} TFT_SPI_CLK=${TFT_SPI_CLK} TFT_SPI_CS=${TFT_SPI_CS} TFT_RESET=${TFT_RESET})
endif()

if (PICO_ZERO)
   add_compile_definitions(PICO_ZERO=1)
   set(EXTRA_PICOZERO_SRC src/ws2812.c)
endif()

if (BEEP)
   add_compile_definitions(BEEP=1)
   set(EXTRA_AUDIO_SRC src/audio.c)
   add_compile_definitions(GPIO_AUDIO=${GPIO_AUDIO} GPIO_AUDIO_INV=${GPIO_AUDIO_INV})   
endif()

if (USB_PC)
   add_compile_definitions(USB_PC=1)
   set(EXTRA_USB_SRC src/usb_pc.c src/usb_descriptors.c)
   set(EXTRA_USB_LIB tinyusb_device pico_bootrom)
else()
   set(EXTRA_USB_SRC src/hid.c)
   set(EXTRA_USB_LIB tinyusb_host)
endif()

if (TARGET tinyusb_device)
  add_executable(firmware
    src/main.c
    src/video.c
    src/kbd.c
    ${EXTRA_USB_SRC}
    ${EXTRA_AUDIO_SRC}
    ${EXTRA_PICOZERO_SRC}
    ${EXTRA_TFT2P_SRC}
    ${EXTRA_SD_SRC}

    ${UMAC_SOURCES}
  )

  # The umac sources need to prepare Musashi (some sources are generated):
  add_custom_command(OUTPUT ${UMAC_MUSASHI_PATH}/m68kops.c
    COMMAND echo "*** Preparing umac source ***"
    COMMAND make -C ${UMAC_PATH} prepare
    )
  add_custom_target(prepare_umac
    DEPENDS ${UMAC_MUSASHI_PATH}/m68kops.c
    )
  add_dependencies(firmware prepare_umac)

  target_link_libraries(firmware
    pico_stdlib
    pico_multicore
    ${EXTRA_USB_LIB}
    tinyusb_board
    hardware_dma
    hardware_pio
    hardware_sync
    ${EXTRA_SD_LIB}
    )

  target_include_directories(firmware PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${TINYUSB_PATH}/hw
    ${TINYUSB_PATH}/src
    ${UMAC_INCLUDE_PATHS}
    incbin
    )

  pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/src/pio_video.pio)

  # WS2812 Mod START
  if (PICO_ZERO)
    pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)
  endif()
  # WS2812 Mod END

  if (BEEP)
    pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/src/audio.pio)
  endif()
  
  pico_enable_stdio_uart(firmware 1)

  # Needed for UF2:
  pico_add_extra_outputs(firmware)

elseif(PICO_ON_DEVICE)
   message(WARNING "not building firmware because TinyUSB submodule is not initialized in the SDK")
endif()
