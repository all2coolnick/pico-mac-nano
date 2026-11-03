#!/bin/bash
# Builds "Pico Mac.app" next to this script.  Needs the Xcode command line tools.
set -e
cd "$(dirname "$0")"

APP="Pico Mac.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"

# Universal binary, for Apple Silicon and Intel Macs on macOS 12 or newer.
# Current toolchains have no Intel copy of a Swift compatibility library, so
# Intel skips it.  The app doesn't use Swift concurrency, which it mainly serves.
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FLAGS="-O -swift-version 5 -framework Cocoa -framework IOKit"
swiftc $FLAGS -target arm64-apple-macos12.0 main.swift -o "$TMP/PicoMac-arm64"
swiftc $FLAGS -target x86_64-apple-macos12.0 -runtime-compatibility-version none main.swift -o "$TMP/PicoMac-x86_64"
lipo -create "$TMP/PicoMac-arm64" "$TMP/PicoMac-x86_64" -output "$APP/Contents/MacOS/PicoMac"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Pico Mac</string>
    <key>CFBundleDisplayName</key><string>Pico Mac</string>
    <key>CFBundleIdentifier</key><string>local.pico-mac-nano.viewer</string>
    <key>CFBundleExecutable</key><string>PicoMac</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.0</string>
    <key>CFBundleVersion</key><string>1</string>
    <key>LSMinimumSystemVersion</key><string>12.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

codesign --force --sign - "$APP"
echo "Built $APP"
