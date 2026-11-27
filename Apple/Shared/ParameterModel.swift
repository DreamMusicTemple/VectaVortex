import AudioToolbox
import Combine
import SwiftUI

/// Observable wrapper around one AUParameter so SwiftUI controls stay in sync with host automation.
final class ParameterModel: ObservableObject, Identifiable {
    let param: AUParameter
    let spec: ParamSpec
    @Published var value: Float
    private var token: AUParameterObserverToken?
    var id: UInt64 { param.address }

    init(param: AUParameter, spec: ParamSpec) {
        self.param = param; self.spec = spec; self.value = param.value
        token = param.token(byAddingParameterObserver: { [weak self] _, newValue in
            DispatchQueue.main.async { if self?.value != newValue { self?.value = newValue } }
        })
    }
    deinit { if let t = token { param.removeParameterObserver(t) } }

    func set(_ v: Float) { value = v; param.setValue(v, originator: token) }
}

/// All parameter models for one Audio Unit, grouped like the web app (Global, Wavetable, FM, Sampler, Procedural, FX).
final class ParameterStore: ObservableObject {
    let audioUnit: VectaAudioUnit
    let models: [ParameterModel]
    let groups: [(name: String, models: [ParameterModel])]

    init(audioUnit: VectaAudioUnit) {
        self.audioUnit = audioUnit
        var all: [ParameterModel] = []
        if let tree = audioUnit.parameterTree {
            for spec in ParameterTable.all { if let p = tree.parameter(withAddress: AUParameterAddress(spec.id)) { all.append(ParameterModel(param: p, spec: spec)) } }
        }
        models = all
        var order: [String] = []; var dict: [String: [ParameterModel]] = [:]
        for m in all { if dict[m.spec.group] == nil { order.append(m.spec.group); dict[m.spec.group] = [] }; dict[m.spec.group]!.append(m) }
        groups = order.map { ($0, dict[$0]!) }
    }

    func model(_ address: String) -> ParameterModel? { models.first { $0.spec.address == address } }
}
