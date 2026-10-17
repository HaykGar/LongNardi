import Foundation

extension GameUploader {
    /// UserDefaults key for "Share anonymous game data" (default on). The Settings
    /// toggle binds to it via @AppStorage; the uploader reads it at queue time.
    static let sharingDefaultsKey = "nardi.shareGameData"
}

/// The user's choice about sharing anonymous game data, plus the one-time notice
/// page that tells them about it. Both live in UserDefaults. Local game history
/// never consults this — opting out only stops cloud uploads.
struct SharingConsent {
    static let bannerDismissedKey = "nardi.shareBannerDismissed"

    static let bannerCopy = "Your finished games are shared anonymously to help improve Nardi. If you'd rather not share them, you can turn this off anytime in Settings."

    let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) { self.defaults = defaults }

    var sharingEnabled: Bool {
        defaults.object(forKey: GameUploader.sharingDefaultsKey) as? Bool ?? true
    }

    /// The notice shows on launch until it is acknowledged once.
    var noticePending: Bool { !defaults.bool(forKey: Self.bannerDismissedKey) }

    func acknowledgeNotice() { defaults.set(true, forKey: Self.bannerDismissedKey) }
}
