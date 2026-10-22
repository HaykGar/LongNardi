import CoreGraphics
let opts: CGWindowListOption = [.optionOnScreenOnly, .excludeDesktopElements]
let list = CGWindowListCopyWindowInfo(opts, kCGNullWindowID) as! [[String: Any]]
for w in list where (w[kCGWindowLayer as String] as? Int) == 0 {
  let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
  print(w[kCGWindowNumber as String]!, w[kCGWindowOwnerName as String] ?? "", "|", w[kCGWindowName as String] ?? "", b["Width"] ?? "", "x", b["Height"] ?? "")
}
