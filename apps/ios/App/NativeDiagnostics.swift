import SwiftUI
import UIKit

// Milestone M0 of docs/nativo.md measured on the iPhone: x18 and TPIDR_EL0 across context switches,
// fixed-address reservations, memory, code-signing state and the StikDebug JIT protocol.
// The report is plain text so it can be copied or shared from the phone.
final class NativeProbe: ObservableObject {
    static let shared = NativeProbe()
    static let journal = Diagnostics.directory.appendingPathComponent("jit-journal.txt")
    private static let debugged: UInt32 = 0x1000_0000
    @Published private(set) var report = ""
    @Published private(set) var status = "Sonde non ancora eseguite."
    @Published private(set) var busy = false
    @Published private(set) var jitFinished = false
    private var probes = ""
    private var jit = ""
    private var started = false
    private var timer: Timer?
    private let queue = DispatchQueue(label: "SwitchCore.native-probes", qos: .userInitiated)
    private let signature = NativeProbe.describeSignature()

    private init() {
        // A code-signing kill cannot be caught: the journal of the previous launch shows the last step.
        if let previous = try? String(contentsOf: Self.journal, encoding: .utf8), !previous.isEmpty {
            jit = "[e] JIT StikDebug: passi registrati nell'ultimo test (avvio precedente)\n" +
                previous.split(separator: "\n").map { "    \($0)" }.joined(separator: "\n")
        }
        refresh()
    }

    func start() {
        guard !started else { return }
        started = true
        runProbes()
    }

    func runProbes() {
        guard !busy else { return }
        busy = true
        status = "Sonde in corso: registri e indirizzi…"
        let threads = UInt32(max(2, ProcessInfo.processInfo.activeProcessorCount * 2))
        queue.async {
            let begin = Date()
            let registers = sc_native_registers(threads, 100_000)
            let seconds = Date().timeIntervalSince(begin)
            var map = [CChar](repeating: 0, count: 512)
            var base: UInt64 = 0
            var size: UInt64 = 0
            sc_native_addresses(&map, map.count, &base, &size)
            var high = [CChar](repeating: 0, count: 256)
            sc_native_high_addresses(&high, high.count)
            let text = Self.describe(registers, seconds: seconds, map: String(cString: map), base: base,
                                     size: size, high: String(cString: high))
            DispatchQueue.main.async {
                self.probes = text
                self.busy = false
                self.status = "Sonde completate."
                self.refresh()
            }
        }
    }

    // StikDebug attaches to this pid, runs universal.js and brings the app back. The brk calls wait
    // for CS_DEBUGGED and an attached debugger: without the script a brk ends the process.
    @MainActor func requestJIT() {
        guard !busy, !jitFinished else { return }
        if Self.debuggerReady() { return runJIT() }
        var parts = URLComponents()
        parts.scheme = "stikdebug"
        parts.host = "enable-jit"
        parts.queryItems = [URLQueryItem(name: "bundle-id", value: Bundle.main.bundleIdentifier ?? ""),
                            URLQueryItem(name: "pid", value: String(getpid())),
                            URLQueryItem(name: "script-name", value: "universal.js")]
        guard let url = parts.url else { return }
        busy = true
        status = "Apertura di StikDebug…"
        UIApplication.shared.open(url) { opened in
            guard opened else {
                self.busy = false
                self.status = "StikDebug non è installato o non ha accettato la richiesta."
                return
            }
            self.status = "In attesa del debugger di StikDebug (fino a 3 minuti)…"
            let begin = Date()
            self.timer?.invalidate()
            self.timer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { timer in
                if Self.debuggerReady() {
                    timer.invalidate()
                    self.runJIT()
                } else if Date().timeIntervalSince(begin) > 180 {
                    timer.invalidate()
                    self.busy = false
                    self.status = "Nessun debugger collegato entro 3 minuti: nessun brk eseguito."
                }
            }
        }
    }

    private static func debuggerReady() -> Bool {
        sc_native_cs_flags() & debugged != 0 && sc_native_debugger_attached()
    }

    private func runJIT() {
        busy = true
        status = "Debugger collegato: test JIT in corso…"
        try? FileManager.default.createDirectory(at: Diagnostics.directory, withIntermediateDirectories: true)
        let path = Self.journal.path
        queue.async {
            var error = [CChar](repeating: 0, count: 256)
            let result = sc_native_jit(path, &error, error.count)
            let text = Self.describe(result, error: String(cString: error))
            DispatchQueue.main.async {
                self.jit = text
                self.jitFinished = result.attempted
                self.busy = false
                self.status = result.rewritten == 43 ? "JIT funzionante: il rapporto è completo." :
                    "Test JIT concluso: dettagli nella sezione [e]."
                self.refresh()
            }
        }
    }

    func refresh() {
        report = [header(), probes, jit.isEmpty ? "[e] JIT StikDebug: non ancora eseguito." : jit]
            .filter { !$0.isEmpty }.joined(separator: "\n\n")
    }

    private func header() -> String {
        var system = utsname()
        uname(&system)
        let machine = withUnsafeBytes(of: &system.machine) { bytes in
            String(decoding: bytes.prefix { $0 != 0 }, as: UTF8.self)
        }
        let info = Bundle.main.infoDictionary ?? [:]
        let short = info["CFBundleShortVersionString"] as? String ?? "?"
        let build = info["CFBundleVersion"] as? String ?? "?"
        let identifier = Bundle.main.bundleIdentifier ?? "?"
        let process = ProcessInfo.processInfo
        let memory = String(format: "%.2f GiB", Double(process.physicalMemory) / 1_073_741_824)
        let available = Double(sc_available_memory()) / 1_048_576
        let footprint = Double(sc_process_footprint()) / 1_048_576
        let flags = sc_native_cs_flags()
        let debugged = Self.yes(flags & Self.debugged != 0)
        let taskAllow = Self.yes(flags & 0x4 != 0)
        let attached = Self.yes(sc_native_debugger_attached())
        var lines: [String] = ["SwitchCore M0 · diagnostica nativa"]
        lines.append("Data: \(ISO8601DateFormatter().string(from: Date()))")
        lines.append("App: \(short) (\(build)) · \(identifier) · pid \(getpid())")
        lines.append("Dispositivo: \(machine) · \(process.operatingSystemVersionString) · \(process.activeProcessorCount) core · RAM \(memory)")
        lines.append(signature)
        lines.append(String(format: "[c] Memoria: disponibile %.1f MiB · footprint %.1f MiB · limite stimato %.1f MiB",
                            available, footprint, available + footprint))
        lines.append("[d] Firma in esecuzione: flag 0x\(String(flags, radix: 16)) (\(Self.flagNames(flags)))")
        lines.append("    CS_DEBUGGED \(debugged) · get-task-allow \(taskAllow) · debugger collegato \(attached)")
        return lines.joined(separator: "\n")
    }

    private static func yes(_ value: Bool) -> String { value ? "sì" : "no" }

    private static func flagNames(_ flags: UInt32) -> String {
        let names: [(UInt32, String)] = [
            (0x1, "VALID"), (0x2, "ADHOC"), (0x4, "GET_TASK_ALLOW"), (0x100, "HARD"), (0x200, "KILL"),
            (0x800, "RESTRICT"), (0x1000, "ENFORCEMENT"), (0x4000, "ENTITLEMENTS_VALIDATED"),
            (0x1_0000, "RUNTIME"), (0x400_0000, "PLATFORM_BINARY"), (0x1000_0000, "DEBUGGED"),
            (0x2000_0000, "SIGNED"), (0x4000_0000, "DEV_CODE"),
        ]
        let set = names.filter { flags & $0.0 != 0 }.map { $0.1 }
        return set.isEmpty ? "nessuno" : set.joined(separator: " ")
    }

    private static func describeSignature() -> String {
        let signed = CodeSignature.read(Bundle.main.executableURL)
        let present = signed.present ? "presente" : "assente"
        var lines = ["Firma dell'eseguibile: \(present) · entitlement \(summary(signed.entitlements)) · DER \(yes(signed.der))"]
        if let profile = CodeSignature.profile() {
            let name = profile["Name"] as? String ?? "?"
            let expiry = (profile["ExpirationDate"] as? Date).map { ISO8601DateFormatter().string(from: $0) } ?? "?"
            let devices = (profile["ProvisionedDevices"] as? [Any])?.count ?? 0
            let granted = summary(profile["Entitlements"] as? [String: Any] ?? [:])
            lines.append("Profilo: \"\(name)\" · scade \(expiry) · dispositivi \(devices)")
            lines.append("    entitlement del profilo: \(granted)")
        } else {
            lines.append("Profilo: embedded.mobileprovision assente")
        }
        return lines.joined(separator: "\n")
    }

    private static func summary(_ entitlements: [String: Any]) -> String {
        let keys = ["get-task-allow", "com.apple.developer.kernel.extended-virtual-addressing",
                    "com.apple.developer.kernel.increased-memory-limit"]
        let known = keys.map { key -> String in
            let name = key.components(separatedBy: ".").last ?? key
            guard let value = entitlements[key] else { return "\(name) assente" }
            if let flag = value as? Bool { return "\(name) \(yes(flag))" }
            return "\(name) \(value)"
        }
        let others = entitlements.keys.filter { !keys.contains($0) }.count
        let rest = others > 0 ? ", altri \(others)" : ""
        return known.joined(separator: ", ") + rest
    }

    private static func describe(_ registers: SCRegisterProbe, seconds: Double, map: String, base: UInt64,
                                 size: UInt64, high: String) -> String {
        var lines: [String] = []
        if registers.checks == 0 {
            lines.append("[a] Registri: sonda disponibile solo su arm64.")
        } else {
            lines.append(String(format: "[a] Registri: %u thread · %llu controlli · %llu segnali · %.2f s",
                                registers.threads, registers.checks, registers.signals, seconds))
            lines.append("    x18 cambiato \(registers.x18_changed) volte · TPIDR_EL0 cambiato \(registers.tpidr_changed) volte")
        }
        lines.append("[b] Indirizzi fissi 4-64 GiB, blocchi da 256 MiB (+ riservabile, . no):")
        let cells = Array(map)
        for row in stride(from: 0, to: cells.count, by: 16) {
            lines.append(String(format: "    %2ld GiB ", 4 + row / 4) + String(cells[row..<min(row + 16, cells.count)]))
        }
        let total = Double(cells.filter { $0 == "+" }.count) / 4
        lines.append(String(format: "    massimo contiguo %llu MiB da 0x%llx · totale %.2f GiB", size >> 20, base, total))
        lines.append("    oltre 64 GiB: \(high)")
        return lines.joined(separator: "\n")
    }

    private static func describe(_ jit: SCJitProbe, error: String) -> String {
        func value(_ result: Int32) -> String { result < 0 ? "non eseguita" : String(result) }
        func kern(_ code: Int32) -> String { code == 0 ? "ok" : "errore \(code)" }
        var lines = ["[e] JIT StikDebug (universal.js):"]
        guard jit.attempted else {
            lines.append("    \(error)")
            return lines.joined(separator: "\n")
        }
        let region = jit.script_allocated ? "del debugger" : String(format: "0x%llx", jit.region)
        let mapping = String(format: "(%llu KiB) · eseguibile 0x%llx · alias RW 0x%llx", jit.size >> 10,
                             jit.executable, jit.alias)
        lines.append("    regione \(region) \(mapping)")
        lines.append("    vm_protect RX \(kern(jit.protect_error)) · alias vm_remap \(kern(jit.remap_error)) · protezioni alias \(jit.alias_protection)/\(jit.alias_max_protection)")
        lines.append("    42 prima del distacco: \(value(jit.before_detach)) · distacco \(yes(jit.detached)) · dopo il distacco: \(value(jit.after_detach)) · riscritta (43): \(value(jit.rewritten))")
        if jit.fault != 0 { lines.append("    segnale durante la chiamata: \(jit.fault)") }
        if !error.isEmpty { lines.append("    errore: \(error)") }
        let outcome: String
        if jit.rewritten == 43 {
            outcome = "JIT funzionante anche dopo il distacco"
        } else if jit.before_detach == 42 {
            outcome = "JIT funzionante solo prima del distacco"
        } else {
            outcome = "JIT non funzionante"
        }
        lines.append("    esito: \(outcome)")
        return lines.joined(separator: "\n")
    }
}

// What the signer left in the app: entitlements in the code signature of the executable and the
// embedded provisioning profile. Re-signing tools on Windows differ in what they keep.
enum CodeSignature {
    struct Contents {
        var present = false
        var der = false
        var entitlements: [String: Any] = [:]
    }

    static func read(_ executable: URL?) -> Contents {
        var contents = Contents()
        guard let executable, let data = try? Data(contentsOf: executable, options: .alwaysMapped) else { return contents }
        func word(_ offset: Int, bigEndian: Bool = false) -> Int? {
            guard offset >= 0, offset + 4 <= data.count else { return nil }
            let raw = data.withUnsafeBytes { $0.loadUnaligned(fromByteOffset: offset, as: UInt32.self) }
            return Int(bigEndian ? UInt32(bigEndian: raw) : UInt32(littleEndian: raw))
        }
        var base = 0
        if word(0, bigEndian: true) == 0xcafe_babe, let slices = word(4, bigEndian: true) {
            for slice in 0..<min(slices, 8) where word(8 + 20 * slice, bigEndian: true) == 0x0100_000c {
                base = word(16 + 20 * slice, bigEndian: true) ?? 0
            }
        }
        guard word(base) == 0xfeed_facf, let commands = word(base + 16) else { return contents }
        var command = base + 32
        for _ in 0..<min(commands, 1024) {
            guard let kind = word(command), let size = word(command + 4), size >= 8 else { break }
            // LC_CODE_SIGNATURE points at a SuperBlob; slot 5 holds XML entitlements, slot 7 DER.
            if kind == 0x1d, let offset = word(command + 8) {
                let blob = base + offset
                if word(blob, bigEndian: true) == 0xfade_0cc0, let count = word(blob + 8, bigEndian: true) {
                    contents.present = true
                    for index in 0..<min(count, 64) {
                        guard let slot = word(blob + 12 + 8 * index, bigEndian: true),
                              let at = word(blob + 16 + 8 * index, bigEndian: true),
                              let magic = word(blob + at, bigEndian: true),
                              let length = word(blob + at + 4, bigEndian: true),
                              length >= 8, blob + at + length <= data.count else { continue }
                        if slot == 5 && magic == 0xfade_7171,
                           let plist = try? PropertyListSerialization.propertyList(
                               from: data.subdata(in: blob + at + 8 ..< blob + at + length), format: nil) as? [String: Any] {
                            contents.entitlements = plist
                        }
                        if slot == 7 && magic == 0xfade_7172 { contents.der = true }
                    }
                }
            }
            command += size
        }
        return contents
    }

    static func profile() -> [String: Any]? {
        guard let url = Bundle.main.url(forResource: "embedded", withExtension: "mobileprovision"),
              let data = try? Data(contentsOf: url),
              let start = data.range(of: Data("<?xml".utf8)),
              let end = data.range(of: Data("</plist>".utf8), in: start.lowerBound..<data.endIndex) else { return nil }
        return try? PropertyListSerialization.propertyList(from: data.subdata(in: start.lowerBound..<end.upperBound),
                                                           format: nil) as? [String: Any]
    }
}

struct NativeDiagnosticsView: View {
    @ObservedObject private var probe = NativeProbe.shared
    @Environment(\.dismiss) private var dismiss
    var body: some View {
        NavigationStack {
            List {
                Section {
                    Button("Ripeti sonde", systemImage: "arrow.clockwise") { probe.runProbes() }.disabled(probe.busy)
                    Button("Attiva JIT con StikDebug", systemImage: "bolt.horizontal") { probe.requestJIT() }
                        .disabled(probe.busy || probe.jitFinished)
                    Text(probe.status).font(.footnote).foregroundStyle(.secondary)
                } footer: {
                    Text("Il test JIT apre StikDebug con lo script universal.js. L'app esegue brk solo con CS_DEBUGGED attivo e debugger collegato.")
                }
                Section("Rapporto") {
                    Text(probe.report).font(.caption2.monospaced()).textSelection(.enabled)
                        .accessibilityIdentifier("native-report")
                    Button("Copia rapporto", systemImage: "doc.on.doc") { UIPasteboard.general.string = probe.report }
                    ShareLink(item: probe.report, subject: Text("Rapporto M0 SwitchCore")) {
                        Label("Condividi rapporto", systemImage: "square.and.arrow.up")
                    }
                }
            }
            .navigationTitle("Diagnostica nativa")
            .toolbar { Button("Fine") { dismiss() } }
            .onAppear { probe.start() }
        }
    }
}
