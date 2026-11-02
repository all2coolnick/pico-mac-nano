// Pico Mac: shows and controls a pico-mac-nano from a Mac.
//
// The Pico must be running firmware built with -DUSB_PC=ON, and be plugged into
// this Mac with a plain USB cable.  The serial protocol is described in
// include/usb_pc.h.  Run with --port <path> to use a given serial port instead
// of looking for the Pico.

import Cocoa
import IOKit
import IOKit.serial

let usbVendorID = 0x2e8a
let usbProductID = 0x000a

let cmdMousePos: UInt8 = 0x01
let cmdMouseButton: UInt8 = 0x02
let cmdKey: UInt8 = 0x03
let cmdRefresh: UInt8 = 0x04
let cmdBootloader: UInt8 = 0x05

let pktSize: UInt8 = 0x5a
let pktLine: UInt8 = 0xa5

// Mac virtual keycodes to USB HID keycodes.  These are physical key positions,
// so the keyboard layout set on this Mac makes no difference.
let keyToHID: [UInt16: UInt8] = [
    0: 0x04, 1: 0x16, 2: 0x07, 3: 0x09, 4: 0x0b, 5: 0x0a, 6: 0x1d, 7: 0x1b, 8: 0x06, 9: 0x19,   // A S D F H G Z X C V
    11: 0x05, 12: 0x14, 13: 0x1a, 14: 0x08, 15: 0x15, 16: 0x1c, 17: 0x17,                       // B Q W E R Y T
    18: 0x1e, 19: 0x1f, 20: 0x20, 21: 0x21, 22: 0x23, 23: 0x22, 24: 0x2e, 25: 0x26, 26: 0x24,   // 1 2 3 4 6 5 = 9 7
    27: 0x2d, 28: 0x25, 29: 0x27, 30: 0x30, 31: 0x12, 32: 0x18, 33: 0x2f, 34: 0x0c, 35: 0x13,   // - 8 0 ] O U [ I P
    36: 0x28, 37: 0x0f, 38: 0x0d, 39: 0x34, 40: 0x0e, 41: 0x33, 42: 0x31, 43: 0x36, 44: 0x38,   // Return L J ' K ; \ , /
    45: 0x11, 46: 0x10, 47: 0x37, 48: 0x2b, 49: 0x2c, 50: 0x35, 51: 0x2a, 53: 0x29,             // N M . Tab Space ` Delete Esc
    122: 0x3a, 120: 0x3b, 99: 0x3c, 118: 0x3d, 96: 0x3e, 97: 0x3f,                              // F1-F6
    98: 0x40, 100: 0x41, 101: 0x42, 109: 0x43, 103: 0x44, 111: 0x45,                            // F7-F12
    123: 0x50, 124: 0x4f, 125: 0x51, 126: 0x52,                                                 // Left Right Down Up
    115: 0x4a, 119: 0x4d, 116: 0x4b, 121: 0x4e, 117: 0x4c,                                      // Home End PgUp PgDn ForwardDelete
    82: 0x62, 83: 0x59, 84: 0x5a, 85: 0x5b, 86: 0x5c, 87: 0x5d, 88: 0x5e, 89: 0x5f, 91: 0x60,   // Keypad 0-8
    92: 0x61, 65: 0x63, 67: 0x55, 69: 0x57, 75: 0x54, 78: 0x56, 76: 0x58, 81: 0x67,             // Keypad 9 . * + / - Enter =
]

let modifierToHID: [UInt16: UInt8] = [
    55: 0xe3, 54: 0xe7,     // Command
    56: 0xe1, 60: 0xe5,     // Shift
    58: 0xe2, 61: 0xe6,     // Option
    59: 0xe0, 62: 0xe4,     // Control
]
let capsLockKey: UInt16 = 57
let capsLockHID: UInt8 = 0x39

// MARK: - The Pico

/// Finds the serial port of a Pico running USB_PC firmware.
func findPicoPort() -> String? {
    var iterator: io_iterator_t = 0
    let matching = IOServiceMatching(kIOSerialBSDServiceValue)
    guard IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) == KERN_SUCCESS else { return nil }
    defer { IOObjectRelease(iterator) }

    func usbProperty(_ service: io_object_t, _ name: String) -> Int? {
        let options = IOOptionBits(kIORegistryIterateRecursively | kIORegistryIterateParents)
        return IORegistryEntrySearchCFProperty(service, kIOServicePlane, name as CFString, kCFAllocatorDefault, options) as? Int
    }

    var service = IOIteratorNext(iterator)
    while service != 0 {
        defer { IOObjectRelease(service); service = IOIteratorNext(iterator) }
        guard usbProperty(service, "idVendor") == usbVendorID, usbProperty(service, "idProduct") == usbProductID else { continue }
        let path = IORegistryEntryCreateCFProperty(service, kIOCalloutDeviceKey as CFString, kCFAllocatorDefault, 0)
        if let path = path?.takeRetainedValue() as? String {
            return path
        }
    }
    return nil
}

/// Talks to the Pico on a background thread, and keeps a copy of the Mac's screen.
final class Pico {
    private let portOverride: String?
    private let lock = NSLock()
    private var fd: Int32 = -1
    private var width = 0
    private var height = 0
    private var framebuffer = [UInt8]()
    private var changed = false

    var onStatus: ((String?) -> Void)?
    var onSize: ((Int, Int) -> Void)?

    init(port: String?) {
        portOverride = port
    }

    func start() {
        Thread.detachNewThread { [self] in run() }
    }

    func send(_ bytes: [UInt8]) {
        lock.lock()
        let fd = self.fd
        lock.unlock()
        if fd >= 0 {
            _ = bytes.withUnsafeBytes { write(fd, $0.baseAddress, $0.count) }
        }
    }

    /// Returns the screen as an image, if it has changed since the last call.
    func takeChangedImage() -> CGImage? {
        lock.lock()
        defer { lock.unlock() }
        guard changed, width > 0 else { return nil }
        changed = false
        guard let provider = CGDataProvider(data: Data(framebuffer) as CFData) else { return nil }
        // The Mac's 1 is black, so invert
        return CGImage(width: width, height: height, bitsPerComponent: 1, bitsPerPixel: 1, bytesPerRow: width / 8,
                       space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGBitmapInfo(rawValue: 0),
                       provider: provider, decode: [1, 0], shouldInterpolate: false, intent: .defaultIntent)
    }

    private func status(_ text: String?) {
        DispatchQueue.main.async { self.onStatus?(text) }
    }

    private func run() {
        while true {
            guard let port = portOverride ?? findPicoPort() else {
                status("Waiting for the Pico…\nPlug it in with a plain USB cable, running USB_PC firmware.")
                Thread.sleep(forTimeInterval: 1)
                continue
            }
            let opened = open(port, O_RDWR | O_NOCTTY)
            if opened < 0 {
                status("Can't open \(port):\n\(String(cString: strerror(errno)))")
                Thread.sleep(forTimeInterval: 1)
                continue
            }
            var tio = termios()
            if tcgetattr(opened, &tio) == 0 {
                cfmakeraw(&tio)
                tcsetattr(opened, TCSANOW, &tio)
            }
            let tiocsdtr: UInt = 0x2000_7479    // The Pico only sends to a port with DTR set
            _ = ioctl(opened, tiocsdtr)

            lock.lock()
            fd = opened
            lock.unlock()
            send([cmdRefresh])

            readPackets(opened)

            lock.lock()
            fd = -1
            lock.unlock()
            close(opened)
            status("Lost the Pico. Waiting for it to come back…")
            Thread.sleep(forTimeInterval: 1)
        }
    }

    private func readPackets(_ fd: Int32) {
        var buffer = [UInt8](repeating: 0, count: 4096)
        var pending = [UInt8]()
        while true {
            let n = read(fd, &buffer, buffer.count)
            if n <= 0 {
                return
            }
            pending.append(contentsOf: buffer[0..<n])
            var at = 0
            parse: while at < pending.count {
                switch pending[at] {
                case pktSize:
                    guard pending.count - at >= 5 else { break parse }
                    setSize(Int(pending[at + 1]) << 8 | Int(pending[at + 2]), Int(pending[at + 3]) << 8 | Int(pending[at + 4]))
                    at += 5
                case pktLine where width > 0:
                    let length = 2 + width / 8
                    guard pending.count - at >= length else { break parse }
                    setLine(Int(pending[at + 1]), pending[(at + 2)..<(at + length)])
                    at += length
                default:
                    at += 1
                }
            }
            pending.removeFirst(at)
        }
    }

    private func setSize(_ w: Int, _ h: Int) {
        lock.lock()
        let isNew = (w != width || h != height)
        if isNew {
            width = w
            height = h
            framebuffer = [UInt8](repeating: 0, count: w / 8 * h)
        }
        changed = true
        lock.unlock()
        status(nil)
        if isNew {
            DispatchQueue.main.async { self.onSize?(w, h) }
        }
    }

    private func setLine(_ y: Int, _ line: ArraySlice<UInt8>) {
        lock.lock()
        defer { lock.unlock() }
        guard y < height else { return }
        framebuffer.replaceSubrange((y * (width / 8))..<((y + 1) * (width / 8)), with: line)
        changed = true
    }
}

// MARK: - The screen

final class ScreenView: NSView {
    let pico: Pico
    var screenSize = NSSize(width: 240, height: 180)
    private let message = NSTextField(wrappingLabelWithString: "")
    private let hiddenCursor = NSCursor(image: NSImage(size: NSSize(width: 1, height: 1)), hotSpot: .zero)
    private var keysDown = Set<UInt16>()
    private var modifiersDown = Set<UInt16>()
    private var capsLockOn = false
    private var buttonDown = false
    private var lastPos: (Int, Int)?
    private var connected = false

    init(pico: Pico) {
        self.pico = pico
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        layer?.contentsGravity = .resizeAspect
        layer?.magnificationFilter = .nearest

        message.textColor = .white
        message.alignment = .center
        message.translatesAutoresizingMaskIntoConstraints = false
        addSubview(message)
        NSLayoutConstraint.activate([
            message.centerXAnchor.constraint(equalTo: centerXAnchor),
            message.centerYAnchor.constraint(equalTo: centerYAnchor),
            message.widthAnchor.constraint(equalTo: widthAnchor, constant: -40),
        ])
        addTrackingArea(NSTrackingArea(rect: .zero, options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
                                       owner: self, userInfo: nil))
    }

    required init?(coder: NSCoder) { fatalError() }

    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    func setStatus(_ text: String?) {
        message.stringValue = text ?? ""
        message.isHidden = (text == nil)
        connected = (text == nil)
        window?.invalidateCursorRects(for: self)
    }

    func refresh() {
        if let image = pico.takeChangedImage() {
            CATransaction.begin()
            CATransaction.setDisableActions(true)
            layer?.contents = image
            CATransaction.commit()
        }
    }

    // The Mac draws its own pointer, so hide this one
    override func resetCursorRects() {
        if connected {
            addCursorRect(bounds, cursor: hiddenCursor)
        }
    }

    // MARK: Mouse

    private func movePointer(_ event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        let scale = min(bounds.width / screenSize.width, bounds.height / screenSize.height)
        let left = (bounds.width - screenSize.width * scale) / 2
        let top = (bounds.height - screenSize.height * scale) / 2
        let x = Int((point.x - left) / scale)
        let y = Int((bounds.height - point.y - top) / scale)
        let pos = (max(0, min(x, Int(screenSize.width) - 1)), max(0, min(y, Int(screenSize.height) - 1)))
        if lastPos == nil || lastPos! != pos {
            lastPos = pos
            pico.send([cmdMousePos, UInt8(pos.0 >> 8), UInt8(pos.0 & 0xff), UInt8(pos.1 >> 8), UInt8(pos.1 & 0xff)])
        }
    }

    private func setButton(_ down: Bool) {
        if down != buttonDown {
            buttonDown = down
            pico.send([cmdMouseButton, down ? 1 : 0])
        }
    }

    override func mouseMoved(with event: NSEvent) { movePointer(event) }
    override func mouseDragged(with event: NSEvent) { movePointer(event) }
    override func rightMouseDragged(with event: NSEvent) { movePointer(event) }
    override func otherMouseDragged(with event: NSEvent) { movePointer(event) }
    override func mouseDown(with event: NSEvent) { movePointer(event); setButton(true) }
    override func mouseUp(with event: NSEvent) { movePointer(event); setButton(false) }
    override func rightMouseDown(with event: NSEvent) { movePointer(event); setButton(true) }
    override func rightMouseUp(with event: NSEvent) { movePointer(event); setButton(false) }

    // MARK: Keyboard

    private func sendKey(_ hid: UInt8, _ down: Bool) {
        pico.send([cmdKey, hid, down ? 1 : 0])
    }

    func handleKeyEvent(_ event: NSEvent) {
        switch event.type {
        case .keyDown:
            guard !event.isARepeat, let hid = keyToHID[event.keyCode] else { return }  // The Mac does its own auto-repeat
            keysDown.insert(event.keyCode)
            sendKey(hid, true)
        case .keyUp:
            if keysDown.remove(event.keyCode) != nil, let hid = keyToHID[event.keyCode] {
                sendKey(hid, false)
            }
        case .flagsChanged:
            if event.keyCode == capsLockKey {
                capsLockOn = event.modifierFlags.contains(.capsLock)
                sendKey(capsLockHID, capsLockOn)
            } else if let hid = modifierToHID[event.keyCode] {
                let down = modifiersDown.insert(event.keyCode).inserted
                if !down {
                    modifiersDown.remove(event.keyCode)
                }
                sendKey(hid, down)
                if !down && hid == 0xe3 || !down && hid == 0xe7 {
                    releaseKeys()   // macOS may not report key releases while Command is held
                }
            }
        default:
            break
        }
    }

    private func releaseKeys() {
        for key in keysDown {
            if let hid = keyToHID[key] {
                sendKey(hid, false)
            }
        }
        keysDown.removeAll()
    }

    /// Nothing can stay held down on the Mac once this window can't see it being released.
    func releaseEverything() {
        releaseKeys()
        for key in modifiersDown {
            if let hid = modifierToHID[key] {
                sendKey(hid, false)
            }
        }
        modifiersDown.removeAll()
        setButton(false)
    }
}

// MARK: - The app

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let pico: Pico
    var window: NSWindow!
    var screen: ScreenView!

    init(pico: Pico) {
        self.pico = pico
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        buildMenu()

        screen = ScreenView(pico: pico)
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 720, height: 540),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable],
                          backing: .buffered, defer: false)
        window.title = "Pico Mac"
        window.contentView = screen
        window.contentAspectRatio = NSSize(width: 240, height: 180)
        window.contentMinSize = NSSize(width: 240, height: 180)
        window.delegate = self
        window.center()
        window.makeKeyAndOrderFront(nil)
        window.makeFirstResponder(screen)

        pico.onStatus = { [weak self] text in self?.screen.setStatus(text) }
        pico.onSize = { [weak self] w, h in self?.screenSizeChanged(w, h) }
        screen.setStatus("Looking for the Pico…")
        pico.start()

        Timer.scheduledTimer(withTimeInterval: 1.0 / 60, repeats: true) { [weak self] _ in self?.screen.refresh() }

        // Every key goes to the Mac while this window is in front, including
        // Command shortcuts such as Command-Q.  Use the menu bar to quit.
        NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .keyUp, .flagsChanged]) { [weak self] event in
            guard let self, event.window === self.window, self.window.isKeyWindow else { return event }
            self.screen.handleKeyEvent(event)
            return nil
        }

        NSApp.activate(ignoringOtherApps: true)
    }

    private func screenSizeChanged(_ w: Int, _ h: Int) {
        let size = NSSize(width: w, height: h)
        screen.screenSize = size
        window.contentAspectRatio = size
        window.contentMinSize = size
        window.setContentSize(NSSize(width: w * 3, height: h * 3))
    }

    private func buildMenu() {
        let main = NSMenu()
        let appItem = NSMenuItem()
        main.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Reboot Pico for Flashing…", action: #selector(rebootForFlashing), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Quit Pico Mac", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "")
        appItem.submenu = appMenu
        NSApp.mainMenu = main
    }

    @objc func rebootForFlashing() {
        let alert = NSAlert()
        alert.messageText = "Reboot the Pico for flashing?"
        alert.informativeText = "The Mac will stop, and the Pico will appear as a drive. Copy a .uf2 file to it to flash it."
        alert.addButton(withTitle: "Reboot")
        alert.addButton(withTitle: "Cancel")
        if alert.runModal() == .alertFirstButtonReturn {
            pico.send([cmdBootloader] + Array("BOOT".utf8))
        }
    }

    func windowDidResignKey(_ notification: Notification) {
        screen.releaseEverything()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    func applicationWillTerminate(_ notification: Notification) {
        screen.releaseEverything()
    }
}

var port: String?
let arguments = CommandLine.arguments
if let i = arguments.firstIndex(of: "--port"), i + 1 < arguments.count {
    port = arguments[i + 1]
}

signal(SIGPIPE, SIG_IGN)
let app = NSApplication.shared
app.setActivationPolicy(.regular)
let delegate = AppDelegate(pico: Pico(port: port))
app.delegate = delegate
app.run()
