# pico-mac-nano - changes in this source tree

* USB: `src/hid.c` rewritten (HID report-descriptor parsing, report protocol),
  `include/tusb_config.h` (bigger enumeration buffer, 8 HID interfaces).
* Keyboard: `src/kbd.c` - caps lock latches like a real Mac. (Arrow keys: still TODO.)
* Sound: `src/snd.c`, `include/snd.h` - 1-bit full-swing square wave on
  GPIO13 (+) / GPIO12 (-), 3-level volume (Mac volume 0 off, 1-4 half, 5-7 full;
  `SND_VOL_FULL_MIN` in snd.h). Mac frames come from `umac_audio_get_frame()`
  (external/umac/include/umac.h). `src/main.c` calls snd_init()/snd_frame().
  Options: `-DSOUND=ON|OFF` (default ON), `-DBEEP=ON|OFF` (old hardware startup
  beep; default OFF as the Mac now plays its own chime),
  `-DGPIO_AUDIO=13 -DGPIO_AUDIO_INV=12` (must be the A/B pair of one PWM slice).
* umac ROM patching (`external/umac/src/rom.c`), for MEMSIZE > 128: skips the
  ROM RAM test (fast boot, no flicker) and points the ROM beep at the real sound buffer.
* SD card images (`src/main.c`, `src/hid.c`):
  * Boot image: `umac0*.img` by default. Hold a number key (0-9, top row or keypad)
    while powering on to boot `umacN*.img` instead (falls back to umac0, then to
    the in-flash disc). Enabled by default; build with `-DSD_BOOT_SELECT=OFF` to remove it
    (no keyboard wait at boot). The firmware waits up to `SD_SELECT_WAIT_MS` (default
    3000 ms, 0 disables) for a USB keyboard to appear.
  * Extra volumes: `vol1*.img`, `vol2*.img`, `vol3*.img` are mounted as drives
    2-4 and appear on the desktop shortly after boot (`SD_EXTRA_PREFIX` changes
    the "vol" prefix). `...ro.img` (e.g. `vol1ro.img`) means read-only.
  * umac (`external/umac`): `DISC_NUM_DRIVES` is now 4; the `.Sony` driver stub
    (`macsrc/sonydrv.S`, rebuilt into `include/sonydrv.h` with
    `make sonydrv.bin`, needs m68k binutils) registers every disc and mounts the
    extra ones with `MountVol` + a disk-inserted event once the desktop is up.
    The ROM must be re-patched (`./main -W`) with the new driver.
* Keypad / arrow keys (`src/kbd.c`, umac `src/main.c`): sent like a Mac Plus keypad
  (M0110A): `0x79` then the key code (`0x71` Shift first for + * / =). umac now
  has a small keyboard byte FIFO and answers the Instant command (0x14), which the
  Mac uses to fetch the byte after the 0x79 prefix.
* Screen layout (umac `src/rom.c`, `src/video.c`): the Mac's framebuffer now keeps the classic
  Mac Plus layout (512 pixels, 64 bytes per row, standard address). Software such as the ALICE
  chess game hard-codes that layout and crashes if the screen is moved for 480 pixels. umac's new
  `DISP_VISIBLE_WIDTH=480` option patches only the *width* the ROM believes in (screen size,
  cursor clipping, Welcome box edge) so the desktop fits the 480-pixel panel. `video.c` outputs the
  left 480 pixels of each 512-pixel row (`VIDEO_FB_HSHIFT` is 0 in this mode); the disk light and
  PC control viewer use the visible width. The old relocated-screen 480 mode (`DISP_WIDTH=480`,
  no `DISP_VISIBLE_WIDTH`) is still supported but is not recommended.
* PC control ignores the viewer's mouse position for the first `USB_PC_MOUSE_HOLDOFF_MS` (6000)
  after power-on. A 128K Mac tests all its RAM at boot, and a write into it then caused an
  occasional Sad Mac.
* Sound effects from VIA Timer 1 (umac `src/via.c`, `src/main.c`): many games (Lode Runner,
  Destroyer, Mouse Eaters, CrystalRaider...) make tones by running VIA Timer 1 with its output on
  PB7 (ACR bits 7:6 = 11), which switches the sound output on and off at the timer rate, with
  nothing written to the sound buffer. umac now tracks the T1 latch and gates the sound buffer
  with that square wave, as Mini vMac does (`umac_audio_get_frame()`).
* `CMakeLists.txt`: SOUND option, hardware_pwm, audio pin defaults 13/12.

## Building (208K, 480-wide desktop in the classic 512-wide layout, stock Mac Plus v3 ROM)

    # 1. umac host build, patches the ROM:
    cd external/umac
    make DISP_WIDTH=512 DISP_HEIGHT=342 DISP_VISIBLE_WIDTH=480 MEMSIZE=208
    ./main -r /path/to/macplus_v3.ROM -W /tmp/rom.bin      # close the window once it opens
    xxd -i < /tmp/rom.bin  > ../../incbin/umac-rom.h
    xxd -i < /path/to/disk.dsk > ../../incbin/umac-disc.h  # 1.25MB disc image
    cd ../..
    # 2. firmware (Pico SDK 2.1+ needed: 2.0.0 overflows flash with the embedded disc):
    mkdir build && cd build
    export PICO_SDK_PATH=/path/to/pico-sdk
    CFLAGS="-DDISP_WIDTH=512 -DDISP_HEIGHT=342 -DDISP_VISIBLE_WIDTH=480" cmake .. -DPICO_BOARD=pico \
        -DPICO_ZERO=ON -DTFT_2P=ON -DMEMSIZE=208 -DSOUND=ON
    make -j8                                               # -> firmware.uf2

For the 128K build use MEMSIZE=128 in both places (the DISP settings are unchanged).
The ROM and disc images are not included.

## Disk activity light and PC control (from andshort's pull request #1)

* **Disk light** (`src/video.c`): a white bar is drawn below the picture for `VIDEO_DISK_FRAMES` (6)
  frames after each disk read or write (SD images, and the in-flash disc, which is now read through a
  function so it can be seen). Its position is set by `VIDEO_DISK_X/W/Y/H`: by default 32 px wide
  against the right edge of the panel, 80 lines up from the bottom. Adjust to match the floppy slot in the case.
* **PC control** (`-DUSB_PC=ON`): the Pico becomes a USB serial device instead of a USB host, so no
  keyboard or mouse can be plugged in; boot-key select and `picolog.txt` are not available in this mode.
  The screen is sent to the PC and the PC's mouse and keyboard are sent back. The protocol is in
  `include/usb_pc.h`; `tools/PicoMacViewer` is a macOS app that uses it (source in `tools/PicoMacViewer/source`; run its `build.sh` on a Mac to build "Pico Mac.app" into `tools/PicoMacViewer`). The app is only ad-hoc signed, so macOS warns about a downloaded copy: right-click it, choose Open, and confirm once.
  Unlike the original, the changed-line detection keeps a hash per line rather than a copy of the screen,
  so the RAM use is about the same as the normal build and it fits the 208K option.
* The 240x180 2x2 display mode from the same pull request is not included.
* **Automatic mode** (`-DUSB_AUTO=ON`, instead of USB_PC): one firmware for both. At power-on the Pico is a
  USB host and waits up to `USB_AUTO_WAIT_MS` (4000) for a keyboard or mouse; the Mac starts as soon as one
  appears. If none does, it restarts once as the PC-controlled serial device (the mode is passed across the
  restart in a watchdog scratch register and used for that boot only, so a reset looks for a keyboard again).
  Both USB stacks need RAM, so in this mode the diagnostic log (`HID_LOG_SIZE`), the key-state request buffer,
  the serial buffers and the changed-line hashes (16 bit) are smaller; it fits the 208K build with about
  0.6 KB to spare. Plugging a keyboard in after power-on in PC mode does nothing until the next power cycle.
