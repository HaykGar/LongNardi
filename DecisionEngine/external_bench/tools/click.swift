// usage: click <app name> <x> <y> [toX toY] — coords in points, relative to the app's window.
// The app only accepts real (HID) mouse input while frontmost, so this briefly brings it forward,
// clicks or drags, then puts the cursor and the previously frontmost app back.
import CoreGraphics
import AppKit
let a = CommandLine.arguments.dropFirst(2).compactMap { Double($0) }
let app = NSWorkspace.shared.runningApplications.first { $0.localizedName == CommandLine.arguments[1] }!
let previous = NSWorkspace.shared.frontmostApplication
let home = CGEvent(source: nil)!.location                  // user's cursor, global CG coords
let l = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as! [[String: Any]]
let w = l.first { ($0[kCGWindowOwnerPID as String] as? Int32) == app.processIdentifier && ($0[kCGWindowLayer as String] as? Int) == 0 }!
let b = w[kCGWindowBounds as String] as! [String: Double]
func pt(_ x: Double, _ y: Double) -> CGPoint { CGPoint(x: b["X"]! + x, y: b["Y"]! + y) }
func post(_ t: CGEventType, _ p: CGPoint) { CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: p, mouseButton: .left)!.post(tap: .cghidEventTap); usleep(30_000) }
if previous?.processIdentifier != app.processIdentifier { app.activate(); usleep(150_000) }
let p0 = pt(a[0], a[1])
post(.mouseMoved, p0); post(.leftMouseDown, p0)
if a.count >= 4 {
  // Route via the middle of the board so even a one-point move is a long, unmistakable drag
  // (the app treats a short drag as a tap and then picks the die itself).
  let p1 = pt(a[2], a[3]), mid = pt((a[0] + a[2]) / 2, 405)
  for (from, to) in [(p0, mid), (mid, p1)] {
    for i in 1...10 { let t = Double(i) / 10; post(.leftMouseDragged, CGPoint(x: from.x + (to.x - from.x) * t, y: from.y + (to.y - from.y) * t)) }
  }
  post(.leftMouseUp, p1)
} else { post(.leftMouseUp, p0) }
CGWarpMouseCursorPosition(home)
if let prev = previous, prev.processIdentifier != app.processIdentifier { prev.activate() }
