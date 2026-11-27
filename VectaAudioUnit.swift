// The Audio Unit (AUv3 instrument). Owns the C++ engine, exposes the parameter tree and renders audio.
// NOTE: written on Linux without a Swift compiler - expect to fix small compile errors on the first build in Xcode.
import AudioToolbox
import AVFoundation
import CoreAudioKit

/// Everything the render block needs, kept in a final class so the block never captures `self` (the AU object).
private final class RenderState {
    var engine: OpaquePointer?
    var musicalContext: AUHostMusicalContextBlock?
    var transportState: AUHostTransportStateBlock?
    var scratchL: UnsafeMutablePointer<Float>?
    var scratchR: UnsafeMutablePointer<Float>?
    var scratchFrames = 0
    var lastTempo = 120.0
    let hostTempoActive = UnsafeMutablePointer<Int32>.allocate(capacity: 1)   // 1 while a host supplies tempo (UI reads racily)
    var lastPlaying = false
    // output tap for the scope / spectrogram (single writer = the render block, readers copy racily - harmless for a display)
    static let scopeSize = 4096
    let scopeL = UnsafeMutablePointer<Float>.allocate(capacity: RenderState.scopeSize)
    let scopeR = UnsafeMutablePointer<Float>.allocate(capacity: RenderState.scopeSize)
    let scopeWrite = UnsafeMutablePointer<Int>.allocate(capacity: 1)
    let scopeOn = UnsafeMutablePointer<Int32>.allocate(capacity: 1)   // Scope power: 0 (the default) = the render block does not copy the output tap at all
    let lastNote = UnsafeMutablePointer<Int32>.allocate(capacity: 1)   // most recent note-on (zone LEARN)
    init() { scopeOn.initialize(to: 0); hostTempoActive.initialize(to: 0); lastNote.initialize(to: -1); scopeL.initialize(repeating: 0, count: RenderState.scopeSize); scopeR.initialize(repeating: 0, count: RenderState.scopeSize); scopeWrite.initialize(to: 0) }
    deinit { scopeL.deallocate(); scopeR.deallocate(); scopeWrite.deallocate(); scopeOn.deallocate() }
}

/// Lets the app move AUParameter values to match a loaded preset without echoing them back into the engine as host edits.
private final class ObserverGate { private let lock = NSLock(); private var on = false
    var suppressed: Bool { get { lock.lock(); defer { lock.unlock() }; return on } set { lock.lock(); on = newValue; lock.unlock() } } }

public extension Notification.Name {
    /// Posted on the main thread after host automation changed the preset (the UI mirror should reload).
    static let vvAutomationApplied = Notification.Name("vv.automationApplied")
    /// Posted on the main thread after the host restored a session (fullState) that carried saved UI state (tab, view modes).
    static let vvUIStateRestored = Notification.Name("vv.uiStateRestored")
}

public final class VectaAudioUnit: AUAudioUnit {
    private let state = RenderState()
    private let gate = ObserverGate()
    private var pumpTimer: DispatchSourceTimer?
    private var outputBus: AUAudioUnitBus!
    private var outputBusArray: AUAudioUnitBusArray!
    private var observerTokens: [AUParameterObserverToken] = []
    // View state that must survive host project recall (selected tab, scope / view modes ...). Small integers keyed by name.
    private let uiLock = NSLock()
    private var uiValues: [String: Int] = [:]
    public func uiValue(_ key: String, default def: Int = 0) -> Int { uiLock.lock(); defer { uiLock.unlock() }; return uiValues[key] ?? def }
    public func setUIValue(_ key: String, _ value: Int) { uiLock.lock(); uiValues[key] = value; uiLock.unlock() }

    public static let componentDescription = AudioComponentDescription(
        componentType: kAudioUnitType_MusicDevice,
        componentSubType: fourCC("Vvtx"),
        componentManufacturer: fourCC("DMTm"),   // must match Extension/Info.plist (project.yml)
        componentFlags: 0, componentFlagsMask: 0)

    public override init(componentDescription: AudioComponentDescription, options: AudioComponentInstantiationOptions = []) throws {
        try super.init(componentDescription: componentDescription, options: options)
        state.engine = vv_engine_create(48000)
        _ = IRStore.shared   // scans the shared IRs folder so a preset's imported impulse responses resolve before the first render (app and AUv3 alike)
        BatterySaving.shared.attach { [weak self] in self?.state.engine }   // restores the saved Battery Saving Mode (a device setting, never read from presets)
        let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
        outputBus = try AUAudioUnitBus(format: format)
        outputBus.maximumChannelCount = 2
        outputBusArray = AUAudioUnitBusArray(audioUnit: self, busType: .output, busses: [outputBus])
        maximumFramesToRender = 1024
        buildParameterTree()
        if let e = state.engine {
            let l = InstrumentLoader(engine: e, bundle: Bundle(for: VectaAudioUnit.self))
            loader = l
            l.onPresetApplied = { [weak self] in self?.refreshParametersFromEngine() }
            l.loadEverything()
        }
        startPump()
        // The host owns the audio session inside a DAW, but an interruption (call, Siri, another app) still reaches us: release every
        // note so nothing hangs when the host stops pulling audio and resumes later.
        interruptionObserver = NotificationCenter.default.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { [weak self] n in
            guard let self, let raw = n.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt, AVAudioSession.InterruptionType(rawValue: raw) == .began else { return }
            vv_engine_all_notes_off(self.state.engine)
        }
    }
    private var interruptionObserver: NSObjectProtocol?

    /// Host automation -> preset. Parameter events may arrive on the render thread, so they are only flagged there; this timer
    /// (30 Hz, background queue) applies them to the preset. Cheap when nothing changed.
    private func startPump() {
        let t = DispatchSource.makeTimerSource(queue: DispatchQueue(label: "vv.params", qos: .userInitiated))
        t.schedule(deadline: .now() + .milliseconds(100), repeating: .milliseconds(33))
        t.setEventHandler { [weak self] in
            guard let self = self, let e = self.state.engine else { return }
            var changed = vv_engine_pump_params(e) > 0
            if vv_engine_poll_cc(e) > 0 { changed = true }   // MIDI CC map + learned mappings (never on the audio thread)
            if changed { DispatchQueue.main.async { NotificationCenter.default.post(name: .vvAutomationApplied, object: self) } }
        }
        t.resume(); pumpTimer = t
    }

    /// Preset -> host parameters (after a preset load): updates every AUParameter without echoing it back as an edit.
    public func refreshParametersFromEngine(notify: Bool = true) {
        guard let tree = parameterTree, let e = state.engine else { return }
        gate.suppressed = true
        for spec in ParameterTable.all {
            if let p = tree.parameter(withAddress: AUParameterAddress(spec.id)) { let v = vv_engine_get_param(e, UInt32(spec.id)); if abs(p.value - v) > 1e-6 { p.value = v } }
        }
        gate.suppressed = false
        if notify { NotificationCenter.default.post(name: .vvAutomationApplied, object: self) }
    }
    /// The C++ engine handle (for the preset store). Calls through the C ABI are safe from the main thread.
    public var enginePointer: OpaquePointer? { state.engine }
    /// Host tempo (BPM) while a host provides one (AUv3 / Ableton Link-style hosts); nil in standalone use. The host is then the only clock.
    public var hostTempo: Double? { state.hostTempoActive.pointee != 0 ? state.lastTempo : nil }
    /// Bundled assets + preset swapping. Nil only if the engine could not be created.
    public private(set) var loader: InstrumentLoader?

    deinit {
        if let o = interruptionObserver { NotificationCenter.default.removeObserver(o) }
        pumpTimer?.cancel()
        state.scratchL?.deallocate(); state.scratchR?.deallocate()
        vv_engine_destroy(state.engine)
    }

    // MARK: - Bus layout
    public override var outputBusses: AUAudioUnitBusArray { outputBusArray }
    public override var channelCapabilities: [NSNumber]? { [0, 2] }          // instrument, stereo out

    // MARK: - Parameter tree
    private func unit(for s: String) -> AudioUnitParameterUnit {
        switch s {
        case "Hz", "Hertz": return .hertz
        case "Seconds": return .seconds
        case "Semitones": return .relativeSemiTones
        case "Cents": return .cents
        case "Percent": return .percent
        case "Ratio": return .ratio
        case "Boolean": return .boolean
        case "Indexed": return .indexed
        case "BPM": return .beatsPerMinute
        default: return .generic
        }
    }

    private func buildParameterTree() {
        var groups: [String: [AUParameter]] = [:]
        var order: [String] = []
        for spec in ParameterTable.all {
            let p = AUParameterTree.createParameter(
                withIdentifier: spec.address, name: spec.label, address: AUParameterAddress(spec.id),
                min: spec.minValue, max: spec.maxValue, unit: unit(for: spec.unit), unitName: nil,
                flags: [.flag_IsReadable, .flag_IsWritable], valueStrings: nil, dependentParameters: nil)
            p.value = spec.defaultValue
            if groups[spec.group] == nil { groups[spec.group] = []; order.append(spec.group) }
            groups[spec.group]!.append(p)
        }
        let children = order.map { AUParameterTree.createGroup(withIdentifier: $0.lowercased(), name: $0, children: groups[$0]!) }
        let tree = AUParameterTree.createTree(withChildren: children)

        let engine = state.engine
        let gate = self.gate
        tree.implementorValueObserver = { param, value in
            if gate.suppressed { return }
            vv_engine_set_param(engine, UInt32(param.address), value)      // real-time safe: stores + flags; the pump applies it
        }
        tree.implementorValueProvider = { param in vv_engine_get_param(engine, UInt32(param.address)) }
        tree.implementorStringFromValueCallback = { param, valuePtr in
            let v = valuePtr?.pointee ?? param.value
            return String(format: "%.3f", v)
        }
        parameterTree = tree
    }

    // MARK: - Render resources
    public override func allocateRenderResources() throws {
        try super.allocateRenderResources()
        let sr = outputBus.format.sampleRate
        vv_engine_prepare(state.engine, sr)        // not realtime-safe, fine here
        // playheads (roll, step and drum sequencers) are drawn one output-latency in the past, so the picture matches what is heard
        let outLat = max(self.outputLatency, AVAudioSession.sharedInstance().outputLatency) + AVAudioSession.sharedInstance().ioBufferDuration
        vv_engine_set_display_latency(state.engine, Int32(max(0, min(0.5, outLat)) * sr))
        state.musicalContext = musicalContextBlock
        state.transportState = transportStateBlock
        let frames = Int(maximumFramesToRender)
        state.scratchL?.deallocate(); state.scratchR?.deallocate()
        state.scratchL = .allocate(capacity: frames); state.scratchR = .allocate(capacity: frames)
        state.scratchFrames = frames
    }

    public override func deallocateRenderResources() {
        vv_engine_all_notes_off(state.engine)
        super.deallocateRenderResources()
    }

    // MARK: - Render
    public override var internalRenderBlock: AUInternalRenderBlock {
        let st = state
        return { actionFlags, timestamp, frameCount, outputBusNumber, outputData, realtimeEventListHead, pullInputBlock in
            guard let engine = st.engine else { return kAudioUnitErr_Uninitialized }
            let total = Int(frameCount)
            if total > st.scratchFrames { return kAudioUnitErr_TooManyFramesToProcess }

            // host tempo / play state
            // The host is transport, tempo and sync: play state, tempo and playhead (quarter notes) drive the piano roll, automation lanes, arp and sequencers.
            var tempo = 0.0, beat = 0.0
            let haveContext = st.musicalContext?(&tempo, nil, nil, &beat, nil, nil) ?? false
            if let ts = st.transportState {
                var flags = AUHostTransportStateFlags(rawValue: 0)
                if ts(&flags, nil, nil, nil) { st.lastPlaying = flags.contains(.moving) }
            }
            st.hostTempoActive.pointee = (haveContext && tempo > 0) ? 1 : 0
            if haveContext && tempo > 0 { st.lastTempo = tempo; vv_engine_set_host_transport(engine, 1, st.lastPlaying ? 1 : 0, beat, tempo) }

            // output buffers (the host may pass nil data pointers; then we supply our own)
            let abl = UnsafeMutableAudioBufferListPointer(outputData)
            var left: UnsafeMutablePointer<Float>
            var right: UnsafeMutablePointer<Float>
            if let l = abl[0].mData?.assumingMemoryBound(to: Float.self) { left = l } else { left = st.scratchL!; abl[0].mData = UnsafeMutableRawPointer(left) }
            if abl.count > 1 {
                if let r = abl[1].mData?.assumingMemoryBound(to: Float.self) { right = r } else { right = st.scratchR!; abl[1].mData = UnsafeMutableRawPointer(right) }
            } else { right = st.scratchR! }

            // sample-accurate events: render up to each event, apply it, continue
            var event = realtimeEventListHead
            var offset = 0
            let blockStart = AUEventSampleTime(timestamp.pointee.mSampleTime)
            while offset < total {
                var next = total
                while let e = event {
                    let t = Int(e.pointee.head.eventSampleTime - blockStart)
                    if t > offset { next = min(next, t); break }
                    switch e.pointee.head.eventType {
                    case .midi:   // if Xcode reports no member "midi", the Swift spelling in your SDK is .MIDI
                        var data = e.pointee.MIDI.data
                        let len = Int(min(e.pointee.MIDI.length, 3))
                        withUnsafeBytes(of: &data) { raw in
                            let b = raw.bindMemory(to: UInt8.self)
                            if len >= 3 && (b[0] & 0xF0) == 0x90 && b[2] > 0 { st.lastNote.pointee = Int32(b[1]) }
                            vv_engine_midi(engine, b.baseAddress, Int32(len)) }
                    case .parameter, .parameterRamp:
                        vv_engine_set_param(engine, UInt32(e.pointee.parameter.parameterAddress), e.pointee.parameter.value)
                    default: break
                    }
                    event = UnsafePointer(e.pointee.head.next)
                }
                vv_engine_render(engine, left.advanced(by: offset), right.advanced(by: offset), Int32(next - offset))
                offset = next
            }
            if abl.count == 1 { // mono host buffer: fold to mono
                for i in 0..<total { left[i] = 0.5 * (left[i] + right[i]) }
            }
            if st.scopeOn.pointee != 0 {   // tap the finished output (only while the Scope is switched ON)
                var w = st.scopeWrite.pointee
                for i in 0..<total { let k = (w + i) & (RenderState.scopeSize - 1); st.scopeL[k] = left[i]; st.scopeR[k] = abl.count > 1 ? right[i] : left[i] }
                w = (w + total) & (RenderState.scopeSize - 1); st.scopeWrite.pointee = w
            }
            return noErr
        }
    }

    // MARK: - Instrument controls for the UI (all thread safe in the engine; call from the main thread)
    public func setMacro(_ index: Int, _ value: Float) { vv_engine_set_macro(state.engine, Int32(index), value) }
    public func setModWheel(_ value: Float) { vv_engine_set_modwheel(state.engine, value) }
    public func setPitchBend(_ value: Float) { vv_engine_set_pitchbend(state.engine, value) }
    public func setArp(on: Bool, latch: Bool) { vv_engine_set_arp(state.engine, on ? 1 : 0, latch ? 1 : 0) }
    public func setSequencer(running: Bool) { vv_engine_start_sequencer(state.engine, running ? 1 : 0) }
    public func setPolyphony(voices: Int, legato: Bool) { vv_engine_set_polyphony(state.engine, Int32(voices), legato ? 1 : 0) }
    public func panic() { vv_engine_all_notes_off(state.engine) }
    /// The most recent MIDI note-on (UI keyboard or host), -1 when none yet. Used by the zone LEARN buttons.
    public var lastNoteOn: Int { Int(state.lastNote.pointee) }
    /// MPC pads: slot >= 0 plays that sampler slot once at its original pitch (layer 2); slot < 0 plays the layer at `pitchNote`.
    public func padOn(layer: Int, slot: Int, pad: Int, pitchNote: Int, velocity: Float = 0.9) { vv_engine_pad_on(state.engine, Int32(layer), Int32(slot), Int32(pad), Int32(pitchNote), velocity) }
    /// MPC pad plays transient `slice` (1-based) of sampler slot `slot`.
    public func padSliceOn(slot: Int, slice: Int, pad: Int, velocity: Float = 0.9) { vv_engine_pad_slice_on(state.engine, Int32(slot), Int32(slice), Int32(pad), velocity) }
    public func padOff(layer: Int, pad: Int) { vv_engine_pad_off(state.engine, Int32(layer), Int32(pad)) }
    /// The last `count` output samples (oldest first) for the scope views. Safe to call from the main thread.
    /// Scope power: ON starts copying the output tap, OFF stops it completely.
    public func setScopeTap(_ on: Bool) { state.scopeOn.pointee = on ? 1 : 0 }
    public func scopeSnapshot(_ count: Int) -> (l: [Float], r: [Float]) {
        let n = RenderState.scopeSize, c = min(count, n), w = state.scopeWrite.pointee
        var l = [Float](repeating: 0, count: c), r = [Float](repeating: 0, count: c)
        for i in 0..<c { let k = (w - c + i + n) & (n - 1); l[i] = state.scopeL[k]; r[i] = state.scopeR[k] }
        return (l, r)
    }

    // MARK: - MIDI from our own UI
    /// Sends a MIDI message through the host-provided scheduler (works inside DAWs and standalone).
    public func sendMIDI(_ status: UInt8, _ d1: UInt8, _ d2: UInt8) {
        guard let block = scheduleMIDIEventBlock else { return }
        var bytes: [UInt8] = [status, d1, d2]
        block(AUEventSampleTimeImmediate, 0, 3, &bytes)
    }

    // MARK: - State (host sessions / presets)
    public override var fullState: [String: Any]? {
        get {
            var s = super.fullState ?? [:]
            var values: [String: Float] = [:]
            for spec in ParameterTable.all { values[spec.address] = vv_engine_get_param(state.engine, UInt32(spec.id)) }
            s["vv.version"] = 3   // 3: EQ / IR reverb slots, Round Robin, clips. Older sessions (1, 2) restore unchanged: the core migrates the preset JSON (state_migrate.hpp) and missing parameters keep their defaults.
            s["vv.params"] = values
            uiLock.lock(); s["vv.ui"] = uiValues; uiLock.unlock()
            if let j = loader?.exportJSON(), !j.isEmpty { s["vv.preset"] = j }
            return s
        }
        set {
            super.fullState = newValue
            if let ui = newValue?["vv.ui"] as? [String: Int] {
                uiLock.lock(); uiValues = ui; uiLock.unlock()
                DispatchQueue.main.async { NotificationCenter.default.post(name: .vvUIStateRestored, object: self) }
            }
            if let j = newValue?["vv.preset"] as? String { loader?.loadPreset(j) }   // refused (nothing changes) when damaged; notices come back through vv_engine_load_warnings
            guard let values = newValue?["vv.params"] as? [String: Float] else { return }
            for (address, v) in values {   // unknown addresses (a newer build) and non-finite / out-of-range values are skipped, never applied
                guard let spec = ParameterTable.byAddress[address], v.isFinite else { continue }
                parameterTree?.parameter(withAddress: AUParameterAddress(spec.id))?.setValue(min(spec.maxValue, max(spec.minValue, v)), originator: nil)
            }
        }
    }
}

func fourCC(_ s: String) -> FourCharCode {
    var r: FourCharCode = 0
    for u in s.utf8.prefix(4) { r = (r << 8) | FourCharCode(u) }
    return r
}
