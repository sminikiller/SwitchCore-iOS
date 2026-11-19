import AVFoundation
import SwiftUI

struct PadKey: View {
    var name: String
    var bit: Int
    var engine: Engine
    @State private var held = false
    var body: some View {
        Text(name).font(.headline.monospaced()).frame(width: 44, height: 40)
            .background(held ? Color.teal : .white.opacity(0.12), in: Capsule())
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { _ in
                if !held {
                    held = true; engine.hold(bit, true)
                    if engine.profile.haptics { UIImpactFeedbackGenerator(style: .light).impactOccurred() }
                }
            }.onEnded { _ in held = false; engine.hold(bit, false) })
            .accessibilityLabel(name).accessibilityAddTraits(.isButton)
    }
}
struct GameView: View {
    let title: Title
    @StateObject private var engine: Engine
    @State private var details = false
    @State private var pressure = false
    @Environment(\.dismiss) private var dismiss
    @Environment(\.scenePhase) private var phase
    init(title: Title) {
        self.title = title
        _engine = StateObject(wrappedValue: Engine(title))
    }
    var body: some View {
        VStack(spacing: 6) {
            HStack {
                Button("Libreria", systemImage: "chevron.left") { engine.pause(true); dismiss() }
                Spacer()
                Text(title.name).font(.headline).lineLimit(1)
                Spacer()
                Button(engine.paused ? "Riprendi" : "Pausa", systemImage: engine.paused ? "play.fill" : "pause.fill") {
                    pressure = false; engine.pause(!engine.paused)
                }.disabled(engine.done)
                Button("Diagnostica", systemImage: "waveform.path.ecg") { details.toggle() }
            }.labelStyle(.iconOnly).padding(.horizontal)
            ZStack {
                Color.black
                if let f = engine.frame {
                    Screen(frame: f, scale: pressure || ProcessInfo.processInfo.isLowPowerModeEnabled ? 0.5 : engine.profile.scale)
                        .aspectRatio(CGFloat(f.width)/CGFloat(f.height), contentMode: .fit)
                        .overlay { GeometryReader { geo in
                            Color.clear.contentShape(Rectangle()).gesture(DragGesture(minimumDistance: 0).onChanged { e in
                                let x = max(0, min(1279, e.location.x * 1280 / max(1, geo.size.width)))
                                let y = max(0, min(719, e.location.y * 720 / max(1, geo.size.height)))
                                engine.touch(true, UInt32(x), UInt32(y))
                            }.onEnded { _ in engine.touch(false, 0, 0) })
                        }}
                } else if !engine.done { ProgressView("Avvio homebrew") }
                if engine.done || engine.paused {
                    Text(engine.done ? String(engine.log.prefix(240)) : "In pausa").padding().background(.regularMaterial, in: RoundedRectangle(cornerRadius: 12))
                }
            }
            if details {
                ScrollView {
                    Text(engine.metrics + "\n" + engine.log.suffix(3000)).font(.caption.monospaced()).textSelection(.enabled)
                }.frame(maxHeight: 80)
            }
            ViewThatFits {
                HStack { controls([("←",12),("↑",13),("↓",15),("→",14)]); Spacer(); controls([("−",11),("+",10),("X",2),("Y",3),("B",1),("A",0)]) }
                VStack { controls([("←",12),("↑",13),("↓",15),("→",14),("−",11),("+",10)]); controls([("X",2),("Y",3),("B",1),("A",0)]) }
            }.padding(.horizontal).padding(.bottom, 6)
        }.background(Color.black).foregroundStyle(.white).preferredColorScheme(.dark)
        .onChange(of: phase) { _, new in if new != .active { engine.pause(true) } }
        .onReceive(NotificationCenter.default.publisher(for: UIApplication.didReceiveMemoryWarningNotification)) { _ in pressure = true; engine.pause(true) }
        .onReceive(NotificationCenter.default.publisher(for: ProcessInfo.thermalStateDidChangeNotification)) { _ in
            pressure = ProcessInfo.processInfo.thermalState.rawValue >= ProcessInfo.ThermalState.serious.rawValue
            if ProcessInfo.processInfo.thermalState == .critical { engine.pause(true) }
        }
        .onReceive(NotificationCenter.default.publisher(for: AVAudioSession.interruptionNotification)) { note in
            if (note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt) == AVAudioSession.InterruptionType.began.rawValue { engine.pause(true) }
        }
    }
    private func controls(_ keys: [(String, Int)]) -> some View {
        HStack(spacing: 5) { ForEach(keys, id: \.1) { key in PadKey(name: key.0, bit: key.1, engine: engine) } }
    }
}

