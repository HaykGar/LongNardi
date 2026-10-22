// usage: axlist <app> : role, label, frame (window-relative points) of every element in the app's window
import ApplicationServices
import AppKit
let app = NSWorkspace.shared.runningApplications.first { $0.localizedName == CommandLine.arguments[1] }!
func attr(_ e: AXUIElement, _ a: String) -> AnyObject? { var v: AnyObject?; AXUIElementCopyAttributeValue(e, a as CFString, &v); return v }
func frame(_ e: AXUIElement) -> CGRect? {
  guard let p = attr(e, kAXPositionAttribute), let s = attr(e, kAXSizeAttribute) else { return nil }
  var pt = CGPoint.zero, sz = CGSize.zero
  AXValueGetValue(p as! AXValue, .cgPoint, &pt); AXValueGetValue(s as! AXValue, .cgSize, &sz)
  return CGRect(origin: pt, size: sz)
}
let root = AXUIElementCreateApplication(app.processIdentifier)
let win = (attr(root, kAXWindowsAttribute) as? [AXUIElement])?.first
let origin = win.flatMap(frame)?.origin ?? .zero
func walk(_ e: AXUIElement) {
  let role = attr(e, kAXRoleAttribute) as? String ?? "?"
  let label = (attr(e, kAXDescriptionAttribute) as? String) ?? (attr(e, kAXTitleAttribute) as? String) ?? (attr(e, kAXValueAttribute) as? String) ?? ""
  if let f = frame(e), role != "AXGroup" || !label.isEmpty {
    print(role, "|", label, "|", Int(f.midX - origin.x), Int(f.midY - origin.y), Int(f.width), Int(f.height))
  }
  for c in (attr(e, kAXChildrenAttribute) as? [AXUIElement]) ?? [] { walk(c) }
}
if let w = win { walk(w) }
