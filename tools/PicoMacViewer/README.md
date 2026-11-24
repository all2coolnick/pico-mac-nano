# pico-mac viewer

<img  align="right" height="320px" alt="pico-mac-viewer" src="https://github.com/user-attachments/assets/04353c25-b79f-4569-8485-591a79a56ef7" />

24 Nov 2026


This is a version of @andshort's native Mac application for remotely viewing and controlling pico-mac-nano, modified to work with the pico-mac-nano standard screen resolution of 480x342, and to add auto-mode selection from a universal firmware version.

As well as using a USB keyboard and mouse, pico-mac-nano can be shown and controlled from a Mac. This is excellent news if you want that 1984 Macintosh experience but your eyesight is not up to that tiny 2-inch screen. 
The Pico only has one USB port which can only be in one of two modes; host or device. When using pico-mac-nano with a mouse or keyboard, it is in host mode. 

This app requires pico-mac-nano to be in device mode so that it can show up to your Mac as a serial USB device. Starting with firmware version 21 (commit 3dff507), if a pico-mac-nano is powered on or reset while connected directly to a Mac with a standard USB data cable (power & data, and not via the OTG Splitter cable), it will boot as normal but the USB port will be in device mode. Launching the pico-mac viewer app on your Mac will open a window with a mirror of the pico-mac-nano's screen and you control it with your Mac's keyboard and mouse/trackpad. While its window is in front, all keys go to the pico-mac-nano, including Command-Q, so quit from the menu bar or by closing the window.

Powering up your pico-mac-nano with a keyboard, mouse or even just the splitter cable, will result in normal operation where the USB port is in host mode.

As of pico-mac-nano firmware 23 (commit 02993da) and pico-mac viewer 1.1, the boot image select feature of pico-mac-nano now works in PC Control mode, i.e. when connected to a computer and using pico-mac viewer to control. The viewer app must be running and the foremost window. Holding down a single number key (e.g. 3) any time before the pico-mac-nano boot beep will cause the pico-mac-nano to try to boot from a correspondingly numbered disk image file (e.g.umac3.img) on the micro-SD card. If that cannot be found, it will revert to the normal boot sequence.

## Installing the viewer

1. Download this repository's zip from GitHub (or just the file `pico-mac viewer 1.1.zip` in this folder) and unzip it. Inside is **pico-mac viewer 1.1.app**. Copy that app to your Applications folder or Desktop.
2. Double-click the app itself. Don't open anything inside it (the `Contents/MacOS/PicoMac` file in particular, which opens a Terminal window instead of the app).
3. If macOS refuses to open it, or Terminal shows `Killed: 9`, follow "If macOS won't open the viewer" below. This is common with downloaded copies.

The viewer needs macOS 12 or later and runs on both Apple silicon and Intel Macs.

## If macOS won't open the viewer

The viewer is not signed with an Apple Developer ID, so macOS treats a downloaded copy with suspicion. It may show "cannot be opened because the developer cannot be verified" or "is damaged and can't be opened", open a Terminal window showing `Killed: 9`, or simply do nothing. Unzipping can also leave extra hidden file information attached to the app, which makes its signature invalid so that Apple silicon Macs kill it immediately. Both problems are fixed in a minute:

1. Open **Terminal** (in Applications > Utilities).
2. Type `xattr -cr ` (with a space after it), then drag **pico-mac viewer 1.1.app** from Finder into the Terminal window so its path is filled in, and press Return. This clears the download flags and the extra file information.
3. Type `codesign --force --sign - ` (again with a space at the end), drag the same app into the Terminal window and press Return. This signs the app on your own Mac. If macOS offers to install the "command line developer tools", accept (a few minutes) and then repeat this step.
4. Double-click the app. If macOS still says it can't verify the developer, open **System Settings > Privacy & Security**, scroll down to the message about "pico-mac viewer", click **Open Anyway** and confirm. You only need to do this once. (On recent versions of macOS, right-click > Open no longer offers a way past this warning.)

Still stuck? Run `codesign --verify --verbose "<drag the app here>"` in Terminal and include its output when you ask for help.

### Building it yourself

If you'd rather not run the downloaded copy, you can build it on your Mac. Install the Xcode command line tools (`xcode-select --install`), then run `source/build.sh` in this folder. It builds a universal app, clears any extra file information and signs it for you, so none of the steps above are needed.
