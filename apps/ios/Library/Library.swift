import CryptoKit
import Foundation
import UIKit

struct Title: Identifiable, Codable, Hashable {
    let id: String
    let name: String
    let author: String
    let file: URL
    var favorite = false
    var lastPlayed: Date?
    var extracted: Bool?
}

enum LibraryError: LocalizedError {
    case invalid, tooLarge
    var errorDescription: String? {
        self == .invalid ? "Il file non contiene un eseguibile NRO valido." : "Questo profilo accetta NRO fino a 128 MiB."
    }
}

final class Library: ObservableObject {
    static let root = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("SwitchCore", isDirectory: true)
    @Published private(set) var titles: [Title] = []
    @Published var error: String?
    private let fm = FileManager.default
    private var catalog: URL { Self.root.appendingPathComponent("library.json") }
    init() {
        do {
            try fm.createDirectory(at: Self.root, withIntermediateDirectories: true)
            if fm.fileExists(atPath: catalog.path) {
                titles = try JSONDecoder().decode([Title].self, from: Data(contentsOf: catalog)).map { item in
                    // iOS may relocate the Application Support container between launches.
                    Title(id: item.id, name: item.name, author: item.author,
                          file: item.extracted == true ? Self.dir(item.id).appendingPathComponent("application") : Self.file(item.id),
                          favorite: item.favorite, lastPlayed: item.lastPlayed, extracted: item.extracted)
                }
            }
            if let demo = Bundle.main.url(forResource: "rally", withExtension: "nro"), !titles.contains(where: { $0.name == "Rally" }) {
                try importFile(demo)
            }
        } catch { self.error = error.localizedDescription }
    }
    static func dir(_ id: String) -> URL { root.appendingPathComponent(id, isDirectory: true) }
    static func file(_ id: String) -> URL { dir(id).appendingPathComponent("program.nro") }
    @MainActor func importFolder(_ url: URL) async throws {
        let staged = try await Task.detached(priority: .userInitiated) {
            try ExtractedGame.prepare(url, in: Self.root)
        }.value
        defer { try? fm.removeItem(at: staged.staging) }
        if titles.contains(where: { $0.id == staged.id }) { return }
        let dest = Self.dir(staged.id)
        if !fm.fileExists(atPath: dest.path) { try fm.moveItem(at: staged.staging, to: dest) }
        let title = Title(id: staged.id, name: staged.name, author: "Applicazione Switch · ExeFS",
                          file: dest.appendingPathComponent("application"), extracted: true)
        try persist(titles + [title])
    }
    func importFile(_ url: URL) throws {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let length = try url.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
        guard length <= 128 << 20 else { throw LibraryError.tooLarge }
        let data = try Data(contentsOf: url, options: .mappedIfSafe)
        func word(_ i: Int) -> Int { (0..<4).reduce(0) { $0 | Int(data[i + $1]) << (8 * $1) } }
        guard data.count <= 128 << 20, data.count >= 128, data[16..<20].elementsEqual("NRO0".utf8),
              word(20) == 0, word(32) == 0, word(36) >= 128, word(40) >= word(36),
              word(40) % 4096 == 0, word(48) % 4096 == 0,
              word(48) >= word(40) + word(44), word(48) + word(52) <= word(24),
              word(24) <= data.count, word(24) + word(56) <= 192 << 20 else { throw LibraryError.invalid }
        let id = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        if titles.contains(where: { $0.id == id }) { return }
        var name = url.deletingPathExtension().lastPathComponent
        var author = ""
        let assets = word(24)
        func section(_ i: Int) -> Range<Int>? {
            guard word(i + 4) == 0, word(i + 12) == 0 else { return nil }
            let start = assets + word(i), end = start + word(i + 8)
            return start >= assets + 56 && end <= data.count && end > start ? start..<end : nil
        }
        try fm.createDirectory(at: Self.dir(id), withIntermediateDirectories: true)
        if assets + 56 <= data.count && data[assets..<assets+4].elementsEqual("ASET".utf8) {
            if let meta = section(assets + 24), meta.count >= 0x3000 {
                for i in 0..<16 {
                    let p = meta.lowerBound + 0x300 * i
                    let text = String(decoding: data[p..<p+512].prefix { $0 != 0 }, as: UTF8.self)
                    if !text.isEmpty {
                        name = text
                        author = String(decoding: data[p+512..<p+768].prefix { $0 != 0 }, as: UTF8.self)
                        break
                    }
                }
            }
            if let icon = section(assets + 8), icon.count <= 2 << 20 {
                try data.subdata(in: icon).write(to: Self.dir(id).appendingPathComponent("cover.jpg"), options: .atomic)
            }
        }
        let target = Self.file(id)
        try data.write(to: target, options: .atomic)
        try persist(titles + [Title(id: id, name: name, author: author, file: target)])
    }
    func mark(_ id: String, favorite: Bool) {
        var next = titles
        guard let i = next.firstIndex(where: { $0.id == id }) else { return }
        if favorite { next[i].favorite.toggle() } else { next[i].lastPlayed = Date() }
        do { try persist(next) } catch { self.error = error.localizedDescription }
    }
    private func persist(_ next: [Title]) throws {
        try JSONEncoder().encode(next).write(to: catalog, options: .atomic)
        titles = next
    }
}

