#!/bin/bash
# Builds "Pico Mac.app" next to this script.  Needs the Xcode command line tools.
set -e
cd "$(dirname "$0")"

APP="Pico Mac.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"

swiftc -O -swift-version 5 -framework Cocoa -framework IOKit main.swift -o "$APP/Contents/MacOS/PicoMac"

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
