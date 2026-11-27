// Standalone host app: registers the Audio Unit in-process and plays it through AVAudioEngine, so the synth works
// without a DAW (and so TestFlight/App Store review has something to open).
import AVFoundation
import SwiftUI
import UIKit

@main
struct VectaVortexApp: App {
    @StateObject private var host = AudioHost()
    var body: some Scene {
        WindowGroup {
            Group {
                if let store = host.store { PluginRootView(store: store) }
                else { ProgressView("Starting audio engine…").task { await host.start() } }
            }
            .alert("Audio error", isPresented: .constant(host.error != nil)) { Button("OK") { host.error = nil } } message: { Text(host.error ?? "") }
        }
    }
}

@MainActor
final class AudioHost: ObservableObject {
    @Published var store: ParameterStore?
    @Published var error: String?
    private var engine = AVAudioEngine()
    private var observers: [NSObjectProtocol] = []
    private var registered = false
    private var shouldBeRunning = false          // user-visible intent: the engine should be producing audio

    func start() async {
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try session.setPreferredIOBufferDuration(0.005)
            try session.setActive(true)

            let desc = VectaAudioUnit.componentDescription
            if !registered { AUAudioUnit.registerSubclass(VectaAudioUnit.self, as: desc, name: "Dream Music Temple: Vektur Vortex", version: 1); registered = true }
            let avUnit: AVAudioUnit = try await withCheckedThrowingContinuation { cont in
                AVAudioUnit.instantiate(with: desc, options: []) { unit, err in
                    if let unit { cont.resume(returning: unit) } else { cont.resume(throwing: err ?? NSError(domain: "VectaVortex", code: 1)) }
                }
            }
            guard let au = avUnit.auAudioUnit as? VectaAudioUnit else { throw NSError(domain: "VectaVortex", code: 2, userInfo: [NSLocalizedDescriptionKey: "Unexpected audio unit type"]) }
            engine.attach(avUnit)
            let format = AVAudioFormat(standardFormatWithSampleRate: engine.outputNode.outputFormat(forBus: 0).sampleRate, channels: 2)!
            engine.connect(avUnit, to: engine.mainMixerNode, format: format)
            try engine.start()
            shouldBeRunning = true
            store = ParameterStore(audioUnit: au)
            installObservers()
        } catch { self.error = error.localizedDescription }
    }

    // MARK: - Interruptions (phone call, Siri, another app taking the session), route changes, media-services reset, backgrounding
    private func installObservers() {
        guard observers.isEmpty else { return }
        let nc = NotificationCenter.default, session = AVAudioSession.sharedInstance()
        func add(_ name: Notification.Name, _ object: Any?, _ handler: @escaping @MainActor (AudioHost, Notification) -> Void) {
            observers.append(nc.addObserver(forName: name, object: object, queue: .main) { [weak self] n in
                Task { @MainActor in if let self { handler(self, n) } }
            })
        }
        add(AVAudioSession.interruptionNotification, session) { $0.handleInterruption($1) }
        add(AVAudioSession.routeChangeNotification, session) { $0.handleRouteChange($1) }
        add(AVAudioSession.mediaServicesWereResetNotification, session) { host, _ in host.rebuildAfterMediaServicesReset() }
        add(.AVAudioEngineConfigurationChange, nil) { host, _ in host.restartEngineIfNeeded() }          // sample-rate / device change stops the engine
        add(UIApplication.didEnterBackgroundNotification, nil) { host, _ in   // Settings > Background audio (default on): off = pause when minimised
            if UserDefaults.standard.object(forKey: "vv.bgAudio") as? Bool ?? true { host.restartEngineIfNeeded() } else if host.engine.isRunning { host.engine.pause() }
        } // UIBackgroundModes=audio keeps it alive; verify
        add(UIApplication.willEnterForegroundNotification, nil) { host, _ in host.reactivateSession(); host.restartEngineIfNeeded() }
    }

    private func handleInterruption(_ n: Notification) {
        guard let raw = n.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt, let type = AVAudioSession.InterruptionType(rawValue: raw) else { return }
        switch type {
        case .began:
            store?.audioUnit.panic()                       // no hanging notes: the engine stops being pulled until the interruption ends
        case .ended:
            let opts = AVAudioSession.InterruptionOptions(rawValue: (n.userInfo?[AVAudioSessionInterruptionOptionKey] as? UInt) ?? 0)
            // resume even without .shouldResume when we are in the foreground: a synth that stays silent after a call is a bug
            if opts.contains(.shouldResume) || UIApplication.shared.applicationState != .background { reactivateSession(); restartEngineIfNeeded() }
        @unknown default: break
        }
    }

    private func handleRouteChange(_ n: Notification) {
        guard let raw = n.userInfo?[AVAudioSessionRouteChangeReasonKey] as? UInt, let reason = AVAudioSession.RouteChangeReason(rawValue: raw) else { return }
        switch reason {
        case .oldDeviceUnavailable: store?.audioUnit.panic(); restartEngineIfNeeded()   // headphones / interface unplugged
        case .newDeviceAvailable, .categoryChange, .override, .routeConfigurationChange: restartEngineIfNeeded()
        default: break
        }
    }

    /// The media server died: every AVAudio object is invalid. Rebuild the engine + audio unit from scratch.
    private func rebuildAfterMediaServicesReset() {
        engine.stop(); engine = AVAudioEngine(); store = nil
        Task { await start() }
    }

    private func reactivateSession() {
        let s = AVAudioSession.sharedInstance()
        for attempt in 0..<3 {
            do { try s.setCategory(.playback, mode: .default, options: [.mixWithOthers]); try s.setActive(true); return }
            catch { if attempt == 2 { self.error = "Audio session could not be reactivated: \(error.localizedDescription)" } else { Thread.sleep(forTimeInterval: 0.05) } }
        }
    }

    private func restartEngineIfNeeded() {
        guard shouldBeRunning, !engine.isRunning else { return }
        do { try engine.start() } catch { self.error = "Audio engine could not restart: \(error.localizedDescription)" }
    }
}
