import AVFoundation
import Foundation
import GameController

struct Profile {
    var fps = 60
    var audio = true
    var accelerated = true
    var cache = true
    var swapAB = false
    var scale = 1.0
    var haptics = true
    init(id: String) {
        let d = UserDefaults.standard
        func flag(_ key: String, _ fallback: Bool) -> Bool { d.object(forKey: id + key) as? Bool ?? fallback }
        fps = d.object(forKey: id + ".fps") as? Int ?? 60
        audio = flag(".audio", true)
        accelerated = flag(".metal", true)
        cache = flag(".cache", true)
        swapAB = flag(".swap", false)
        haptics = flag(".haptics", true)
        scale = d.object(forKey: id + ".scale") as? Double ?? 1
    }
}

private final class AudioOutput {
    let engine = AVAudioEngine()
    let player = AVAudioPlayerNode()
    let format = AVAudioFormat(standardFormatWithSampleRate: 48000, channels: 2)!
    private let lock = NSLock()
    private var queued = 0
    init() {
        engine.attach(player)
        engine.connect(player, to: engine.mainMixerNode, format: format)
    }
    func play(_ samples: UnsafePointer<Int16>, _ frames: Int) {
        guard frames > 0, frames <= 48000 else { return }
        if !engine.isRunning {
            do {
                try AVAudioSession.sharedInstance().setCategory(.ambient)
                try engine.start()
                player.play()
            } catch { return }
        }
        lock.lock()
        guard queued < 6 else { lock.unlock(); return }
        queued += 1
        lock.unlock()
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(frames)) else {
            complete(); return
        }
        buffer.frameLength = AVAudioFrameCount(frames)
        let data = buffer.floatChannelData!
        for i in 0..<frames {
            data[0][i] = Float(samples[2*i]) / 32768
            data[1][i] = Float(samples[2*i+1]) / 32768
        }
        player.scheduleBuffer(buffer) { [weak self] in self?.complete() }
    }
    private func complete() { lock.lock(); queued -= 1; lock.unlock() }
    func stop() { player.stop(); engine.stop() }
}

private final class Worker {
    let session = sc_create()
    let queue = DispatchQueue(label: "SwitchCore.emulation", qos: .userInitiated)
    var timer: DispatchSourceTimer?
    var audio: AudioOutput?
    var buttons: UInt64 = 0
    var lastFrame: UInt64 = 0
    var previousTime = 0.0
    var previousCount: UInt64 = 0
    var lastPresent = 0.0
    var lastUI = 0.0
    var fps = 0.0
    var renderer = "Maxwell software"
    var profile: Profile
    var paused = false
    var consumer: ((GuestFrame?, String, String, Bool) -> Void)?
    var pendingFrame: GuestFrame?
    init(_ profile: Profile) { self.profile = profile }
    deinit { sc_destroy(session) }

    func start(_ title: Title) {
        guard sc_load(session, SC_SWITCH_1, title.file.path, Library.dir(title.id).path) else {
            consumer?(nil, "", String(cString: sc_message(session)), true); return
        }
        if profile.accelerated && sc_set_gpu(session, true) { renderer = "MoltenVK / Metal" }
        sc_cache(session, profile.cache)
        if profile.audio {
            audio = AudioOutput()
            sc_audio(session, { ctx, pcm, n in
                guard let ctx, let pcm else { return }
                Unmanaged<AudioOutput>.fromOpaque(ctx).takeUnretainedValue().play(pcm, n)
            }, Unmanaged.passUnretained(audio!).toOpaque())
        }
        previousTime = ProcessInfo.processInfo.systemUptime
        let source = DispatchSource.makeTimerSource(queue: queue)
        timer = source
        source.schedule(deadline: .now(), repeating: .milliseconds(8), leeway: .milliseconds(1))
        source.setEventHandler { [weak self] in self?.tick() }
        source.resume()
    }
    func setPaused(_ value: Bool) {
        paused = value
        buttons = 0
        sc_touch(session, false, 0, 0)
        audio?.stop()
        previousTime = ProcessInfo.processInfo.systemUptime
        previousCount = lastFrame
    }
    func stop() {
        timer?.cancel(); timer = nil
        sc_audio(session, nil, nil)
        audio?.stop(); audio = nil
        consumer = nil
    }
    func tick() {
        guard !paused else { return }
        let info = ProcessInfo.processInfo
        let start = info.systemUptime
        var bits = buttons
        var axes = [Int32](repeating: 0, count: 4)
        if let pad = (GCController.current ?? GCController.controllers().first)?.extendedGamepad {
            let inputs: [(GCControllerButtonInput, Int)] = [
                (pad.buttonA,0),(pad.buttonB,1),(pad.buttonX,2),(pad.buttonY,3),
                (pad.leftShoulder,6),(pad.rightShoulder,7),(pad.leftTrigger,8),(pad.rightTrigger,9),
                (pad.buttonMenu,10),(pad.dpad.left,12),(pad.dpad.up,13),(pad.dpad.right,14),(pad.dpad.down,15)]
            for (button, bit) in inputs where button.isPressed { bits |= 1 << bit }
            if pad.buttonOptions?.isPressed == true { bits |= 1 << 11 }
            axes = [pad.leftThumbstick.xAxis, pad.leftThumbstick.yAxis, pad.rightThumbstick.xAxis, pad.rightThumbstick.yAxis].map { Int32($0.value * 32767) }
        }
        if profile.swapAB { bits = (bits & ~3) | ((bits & 1) << 1) | ((bits & 2) >> 1) }
        sc_pad(session, bits, axes[0], axes[1], axes[2], axes[3])
        let limited = info.isLowPowerModeEnabled || info.thermalState.rawValue >= ProcessInfo.ThermalState.serious.rawValue
        let limit = Double(limited ? min(30, profile.fps) : profile.fps)
        if start < lastPresent + 1 / limit { return }
        var stats = sc_step(session, 100000)
        while stats.state == SC_RUNNING && stats.frames == lastFrame && info.systemUptime - start < (limited ? 0.003 : 0.006) {
            stats = sc_step(session, 100000)
        }
        var number = lastFrame
        if sc_frame(session, lastFrame, { ctx, pixels, w, h in
            guard let ctx, let pixels else { return }
            let worker = Unmanaged<Worker>.fromOpaque(ctx).takeUnretainedValue()
            worker.pendingFrame = GuestFrame(data: Data(bytes: pixels, count: Int(w)*Int(h)*4),
                                              width: Int(w), height: Int(h), serial: 0)
        }, Unmanaged.passUnretained(self).toOpaque(), &number) {
            lastPresent = start
            lastFrame = number
        }
        if start - previousTime >= 1 {
            fps = Double(lastFrame - previousCount) / (start - previousTime)
            previousCount = lastFrame; previousTime = start
        }
        let finished = stats.state != SC_RUNNING
        if pendingFrame != nil || finished || start - lastUI >= 0.5 {
            let frame = pendingFrame.map { GuestFrame(data: $0.data, width: $0.width, height: $0.height, serial: number) }
            pendingFrame = nil
            let text = String(format: "%.1f FPS guest  ·  %.1f ms/frame  ·  %.1f MiB guest", fps, fps > 0 ? 1000/fps : 0, Double(stats.guest_bytes)/1048576) + "\n" + renderer + " · draw GPU " + String(stats.gpu_draws)
            let log = finished ? String(cString: sc_message(session)) + "\n" + String(cString: sc_log(session)) : String(cString: sc_log(session))
            consumer?(frame, text, log, finished)
            lastUI = start
        }
        if finished { stop() }
    }
}

final class Engine: ObservableObject {
    @Published var frame: GuestFrame?
    @Published var metrics = ""
    @Published var log = ""
    @Published var paused = false
    @Published var done = false
    let profile: Profile
    private let worker: Worker
    init(_ title: Title) {
        profile = Profile(id: title.id)
        worker = Worker(profile)
        worker.consumer = { [weak self] f, text, log, finished in
            DispatchQueue.main.async {
                if let f { self?.frame = f }
                self?.metrics = text; self?.log = log; self?.done = finished
                if finished, !log.isEmpty {
                    try? log.write(to: Library.dir(title.id).appendingPathComponent("last-session.txt"), atomically: true, encoding: .utf8)
                }
            }
        }
        worker.queue.async { [worker] in worker.start(title) }
    }
    func hold(_ bit: Int, _ down: Bool) {
        worker.queue.async { [worker] in
            if down { worker.buttons |= 1 << bit } else { worker.buttons &= ~(1 << bit) }
        }
    }
    func touch(_ down: Bool, _ x: UInt32, _ y: UInt32) {
        worker.queue.async { [worker] in sc_touch(worker.session, down, x, y) }
    }
    func pause(_ value: Bool) {
        paused = value
        worker.queue.async { [worker] in worker.setPaused(value) }
    }
    deinit { worker.queue.async { [worker] in worker.stop() } }
}

