#!/bin/bash
# Builds "pico-mac viewer.app" in the folder above this one (tools/PicoMacViewer).
# Needs the Xcode command line tools.
set -e
cd "$(dirname "$0")"
SRC="$PWD"
cd ..

APP="pico-mac viewer.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"

# Universal binary, for Apple Silicon and Intel Macs on macOS 12 or newer.
# Current toolchains have no Intel copy of a Swift compatibility library, so
# Intel skips it.  The app doesn't use Swift concurrency, which it mainly serves.
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FLAGS="-O -swift-version 5 -framework Cocoa -framework IOKit"
swiftc $FLAGS -target arm64-apple-macos12.0 "$SRC/main.swift" -o "$TMP/PicoMac-arm64"
swiftc $FLAGS -target x86_64-apple-macos12.0 -runtime-compatibility-version none "$SRC/main.swift" -o "$TMP/PicoMac-x86_64"
lipo -create "$TMP/PicoMac-arm64" "$TMP/PicoMac-x86_64" -output "$APP/Contents/MacOS/PicoMac"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>pico-mac viewer</string>
    <key>CFBundleDisplayName</key><string>pico-mac viewer</string>
    <key>CFBundleIdentifier</key><string>local.pico-mac-nano.viewer</string>
    <key>CFBundleExecutable</key><string>PicoMac</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>1.1</string>
    <key>CFBundleVersion</key><string>2</string>
    <key>LSMinimumSystemVersion</key><string>12.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF

# Folders synced by iCloud or downloaded from the web can add extended attributes
# (Finder info, quarantine) that codesign refuses to sign over.
xattr -cr "$APP"
codesign --force --sign - "$APP"
echo "Built $APP"
