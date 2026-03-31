// aspect_lock - Native macOS tool to lock a window's aspect ratio to 4:3
// Uses Accessibility API with AXObserver for zero-latency resize enforcement

import Cocoa

let targetRatio: CGFloat = 4.0 / 3.0
var isAdjusting = false

func findWineWindow() -> (pid_t, AXUIElement)? {
    let options = CGWindowListOption(arrayLiteral: .optionOnScreenOnly)
    guard let windows = CGWindowListCopyWindowInfo(options, kCGNullWindowID) as? [[String: Any]] else { return nil }

    for w in windows {
        let title = w["kCGWindowName"] as? String ?? ""
        let pid = w["kCGWindowOwnerPID"] as? pid_t ?? 0
        if title.contains("Zoo Tycoon") {
            let app = AXUIElementCreateApplication(pid)
            var windowsRef: CFTypeRef?
            AXUIElementCopyAttributeValue(app, kAXWindowsAttribute as CFString, &windowsRef)
            if let axWindows = windowsRef as? [AXUIElement], let win = axWindows.first {
                return (pid, win)
            }
        }
    }
    return nil
}

func getWindowSize(_ window: AXUIElement) -> CGSize? {
    var sizeRef: CFTypeRef?
    AXUIElementCopyAttributeValue(window, kAXSizeAttribute as CFString, &sizeRef)
    guard let sizeVal = sizeRef else { return nil }
    var size = CGSize.zero
    AXValueGetValue(sizeVal as! AXValue, .cgSize, &size)
    return size
}

func setWindowSize(_ window: AXUIElement, _ size: CGSize) {
    var newSize = size
    guard let sizeVal = AXValueCreate(.cgSize, &newSize) else { return }
    AXUIElementSetAttributeValue(window, kAXSizeAttribute as CFString, sizeVal)
}

func enforceAspectRatio(_ window: AXUIElement) {
    guard !isAdjusting else { return }
    guard let size = getWindowSize(window) else { return }

    // Account for title bar (~28px)
    let titleBarHeight: CGFloat = 28
    let contentH = size.height - titleBarHeight
    let contentW = size.width

    guard contentW > 100 && contentH > 100 else { return }

    let targetH = contentW / targetRatio
    if abs(contentH - targetH) > 3 {
        isAdjusting = true
        let newHeight = targetH + titleBarHeight
        setWindowSize(window, CGSize(width: size.width, height: newHeight))
        isAdjusting = false
    }
}

func observerCallback(observer: AXObserver, element: AXUIElement, notification: CFString, refcon: UnsafeMutableRawPointer?) {
    enforceAspectRatio(element)
}

// Wait for the game window to appear
print("aspect_lock: waiting for Zoo Tycoon 2 window...")
var window: AXUIElement?
var pid: pid_t = 0

for _ in 0..<60 {
    if let (p, w) = findWineWindow() {
        pid = p
        window = w
        break
    }
    Thread.sleep(forTimeInterval: 1)
}

guard let win = window else {
    print("aspect_lock: could not find Zoo Tycoon 2 window")
    exit(1)
}

print("aspect_lock: found window, pid=\(pid), locking to 4:3")

// Set initial aspect ratio
enforceAspectRatio(win)

// Set up AXObserver to watch for resize
var observer: AXObserver?
let result = AXObserverCreate(pid, observerCallback, &observer)
if result == .success, let obs = observer {
    AXObserverAddNotification(obs, win, kAXResizedNotification as CFString, nil)
    AXObserverAddNotification(obs, win, kAXMovedNotification as CFString, nil)
    CFRunLoopAddSource(CFRunLoopGetCurrent(), AXObserverGetRunLoopSource(obs), .defaultMode)
    print("aspect_lock: observer active, enforcing 4:3")
    CFRunLoopRun()
} else {
    // Fallback: poll
    print("aspect_lock: observer failed, falling back to polling")
    while true {
        enforceAspectRatio(win)
        Thread.sleep(forTimeInterval: 0.1)
    }
}
