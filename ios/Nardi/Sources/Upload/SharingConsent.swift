import Foundation

/// The user's choice about sharing anonymous game data, plus the one-time notice
/// page that tells them about it. Both live in UserDefaults. Local game history
/// never consults this — opting out only stops cloud uploads.
struct SharingConsent {
    /// UserDefaults key for "Share anonymous game data" (default on). The Settings
    /// toggles bind to it via @AppStorage; the uploader reads it at queue time.
    static let sharingKey = "nardi.shareGameData"
    /// Stored value keeps its old "Banner" name so testers who already
    /// acknowledged the notice aren't shown it again.
    static let noticeAcknowledgedKey = "nardi.shareBannerDismissed"

    static let noticeCopy = "Your finished games are shared anonymously to help improve Nardi. If you'd rather not share them, you can turn this off anytime in Settings."

    let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) { self.defaults = defaults }

    var sharingEnabled: Bool {
        defaults.object(forKey: SharingConsent.sharingKey) as? Bool ?? true
    }

    /// The notice shows on launch until it is acknowledged once.
    var noticePending: Bool { !defaults.bool(forKey: Self.noticeAcknowledgedKey) }

    func acknowledgeNotice() { defaults.set(true, forKey: Self.noticeAcknowledgedKey) }
}
