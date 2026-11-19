import Foundation
import MetricKit

// iOS delivers these payloads asynchronously, including diagnostics for earlier
// launches. Keep the latest reports for on-device review or sharing.
final class Diagnostics: NSObject, ObservableObject, MXMetricManagerSubscriber {
    static let directory = Library.root.appendingPathComponent("Diagnostics", isDirectory: true)
    @Published private(set) var reports: [URL] = []
    private let queue = DispatchQueue(label: "SwitchCore.diagnostics")
    override init() {
        super.init()
        MXMetricManager.shared.add(self)
        queue.async { [weak self] in self?.refresh() }
    }
    deinit { MXMetricManager.shared.remove(self) }
    func didReceive(_ payloads: [MXDiagnosticPayload]) {
        store(payloads.map { $0.jsonRepresentation() }, kind: "diagnostica")
    }
    func didReceive(_ payloads: [MXMetricPayload]) {
        store(payloads.map { $0.jsonRepresentation() }, kind: "metriche")
    }
    private func store(_ payloads: [Data], kind: String) {
        queue.async {
            let fm = FileManager.default
            do {
                try fm.createDirectory(at: Self.directory, withIntermediateDirectories: true)
                for payload in payloads {
                    let name = "\(kind)-\(Int(Date().timeIntervalSince1970))-\(UUID().uuidString.prefix(8)).json"
                    try payload.write(to: Self.directory.appendingPathComponent(name), options: .atomic)
                }
                self.refresh()
            } catch { NSLog("SwitchCore MetricKit: %@", error.localizedDescription) }
        }
    }
    private func refresh() {
        let files = (try? FileManager.default.contentsOfDirectory(at: Self.directory, includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        let latest = files.filter { $0.pathExtension == "json" }.sorted {
            ((try? $0.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast) >
            ((try? $1.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast)
        }
        for url in latest.dropFirst(12) { try? FileManager.default.removeItem(at: url) }
        DispatchQueue.main.async { self.reports = Array(latest.prefix(12)) }
    }
}

