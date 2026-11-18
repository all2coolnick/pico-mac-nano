#PicoMacViewer

This is a version of @andshort's native Mac application for viewing and controlling pico-mac-nano, modified to work with the pico-mac-nano standard screen resolution of 480x342, and to add auto-mode selection from a universal firmware version.

As well as using a USB keyboard and mouse, pico-mac-nano can be shown and controlled from a Mac. The Pico only has one USB port which can only be in one of two modes; host or device. When using pico-mac-nano with a mouse or keyboard, it is in host mode. This app requires pico-mac-nano to be in device mode so that it can show up to your Mac as a serial USB device. Starting with firmware version 21 (commit 3dff507), if a pico-mac-nano is powered on or reset while connected directly to a Mac with a standard USB data cable (power & data, and not via the OTG Splitter cable), it will boot as normal but the USB port will be in device mode. Launching the pico-mac viewer app on your Mac will open a window with a mirror of the pico-mac-nano's screen and you control it with your Mac's keyboard and mouse/trackpad. While its window is in front, all keys go to the pico-mac-nano, including Command-Q, so quit from the menu bar or by closing the window.

Powering up your pico-mac-nano with a keyboard, mouse or even just the splitter cable, will result in normal operation where the USB port is in host mode.
