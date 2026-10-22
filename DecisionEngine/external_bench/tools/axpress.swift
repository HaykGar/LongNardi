// usage: axpress <app> <label> : AXPress the first element whose description/title equals label
import ApplicationServices
import AppKit
let appName = CommandLine.arguments[1], target = CommandLine.arguments[2]
let app = NSWorkspace.shared.runningApplications.first { $0.localizedName == appName }!
func attr(_ e: AXUIElement, _ a: String) -> AnyObject? { var v: AnyObject?; AXUIElementCopyAttributeValue(e, a as CFString, &v); return v }
func find(_ e: AXUIElement) -> AXUIElement? {
  let label = (attr(e, kAXDescriptionAttribute) as? String) ?? (attr(e, kAXTitleAttribute) as? String) ?? ""
  if label == target { return e }
  for c in (attr(e, kAXChildrenAttribute) as? [AXUIElement]) ?? [] { if let f = find(c) { return f } }
  return nil
}
guard let el = find(AXUIElementCreateApplication(app.processIdentifier)) else { print("not found"); exit(1) }
print("press:", AXUIElementPerformAction(el, kAXPressAction as CFString).rawValue)
