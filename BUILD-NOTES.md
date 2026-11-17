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
* Screen position (`src/video.c`): `VIDEO_FB_HSHIFT` (-16) moves the 480-wide image left so
  it sits where the older 512-wide-FB builds put it (v9-v10 were shifted 16 px right, cutting
  off the right edge). Total line length is unchanged; tune the value if the edge is off.
* `src/video.c`: VIDEO_FB_HRES is 480 (matches a ROM patched by umac to 480x342).
* `CMakeLists.txt`: SOUND option, hardware_pwm, audio pin defaults 13/12.

## Building (208K, 480x342, stock Mac Plus v3 ROM)

    # 1. umac host build, patches the ROM:
    cd external/umac
    make DISP_WIDTH=480 DISP_HEIGHT=342 MEMSIZE=208
    ./main -r /path/to/macplus_v3.ROM -W /tmp/rom.bin      # close the window once it opens
    xxd -i < /tmp/rom.bin  > ../../incbin/umac-rom.h
    xxd -i < /path/to/disk.dsk > ../../incbin/umac-disc.h  # 1.25MB disc image
    cd ../..
    # 2. firmware (Pico SDK 2.1+ needed: 2.0.0 overflows flash with the embedded disc):
    mkdir build && cd build
    export PICO_SDK_PATH=/path/to/pico-sdk
    CFLAGS="-DDISP_WIDTH=480 -DDISP_HEIGHT=342" cmake .. -DPICO_BOARD=pico \
        -DPICO_ZERO=ON -DTFT_2P=ON -DMEMSIZE=208 -DSOUND=ON
    make -j8                                               # -> firmware.uf2

For the 128K build use MEMSIZE=128 in both places (video.c / DISP settings unchanged).
The ROM and disc images are not included.
