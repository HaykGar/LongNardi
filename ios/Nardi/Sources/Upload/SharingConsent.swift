import Foundation

extension GameUploader {
    /// UserDefaults key for "Share anonymous game data" (default on). The Settings
    /// toggle binds to it via @AppStorage; the uploader reads it at queue time.
    static let sharingDefaultsKey = "nardi.shareGameData"
}

/// The user's choice about sharing anonymous game data, plus the one-time Home
/// banner that tells them about it. Both live in UserDefaults. Local match
/// history never consults this — opting out only stops cloud uploads.
struct SharingConsent {
    static let bannerDismissedKey = "nardi.shareBannerDismissed"

    // Copy pending developer approval (#5)
    static let bannerCopy = "Games are shared anonymously to help improve Nardi. You can turn this off in Settings during a game."

    let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) { self.defaults = defaults }

    var sharingEnabled: Bool {
        defaults.object(forKey: GameUploader.sharingDefaultsKey) as? Bool ?? true
    }

    /// The banner appears only on Home, and only until it is dismissed once.
    func showsBanner(onHome: Bool) -> Bool {
        onHome && !defaults.bool(forKey: Self.bannerDismissedKey)
    }

    func dismissBanner() { defaults.set(true, forKey: Self.bannerDismissedKey) }
}
