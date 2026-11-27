// Principal class of the AUv3 extension: creates the audio unit and hosts the SwiftUI interface inside the DAW.
import CoreAudioKit
import SwiftUI

public final class AudioUnitViewController: AUViewController, AUAudioUnitFactory {
    private var audioUnit: VectaAudioUnit?
    private var hosting: UIHostingController<PluginRootView>?

    public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
        let au = try VectaAudioUnit(componentDescription: componentDescription, options: [])
        audioUnit = au
        DispatchQueue.main.async { [weak self] in self?.connectView() }
        return au
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        preferredContentSize = CGSize(width: 820, height: 520)
        connectView()
    }

    private func connectView() {
        guard hosting == nil, let au = audioUnit, isViewLoaded else { return }
        let host = UIHostingController(rootView: PluginRootView(store: ParameterStore(audioUnit: au)))
        addChild(host)
        host.view.frame = view.bounds
        host.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(host.view)
        host.didMove(toParent: self)
        hosting = host
    }
}
