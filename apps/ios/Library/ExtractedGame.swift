import CryptoKit
import Foundation

struct ExtractedGame {
    let id: String
    let name: String
    let staging: URL

    enum ImportError: LocalizedError {
        case missingMain, invalidFile(String)
        var errorDescription: String? {
            switch self {
            case .missingMain: return "Seleziona una cartella con main in formato NSO0, direttamente o in exefs."
            case .invalidFile(let name): return "Impossibile importare " + name
            }
        }
    }

    // The returned directory belongs to the caller until it is installed or discarded.
    static func prepare(_ source: URL, in root: URL) throws -> Self {
        let scoped = source.startAccessingSecurityScopedResource()
        defer { if scoped { source.stopAccessingSecurityScopedResource() } }
        var error: NSError?
        var result: Result<Self, Error>?
        NSFileCoordinator().coordinate(readingItemAt: source, options: .withoutChanges, error: &error) { url in
            result = Result { try stage(url, in: root) }
        }
        if let error { throw error }
        guard let result else { throw ImportError.missingMain }
        return try result.get()
    }

    private static func stage(_ source: URL, in root: URL) throws -> Self {
        let fm = FileManager.default
        let staged = root.appendingPathComponent(".import-" + UUID().uuidString, isDirectory: true)
        let application = staged.appendingPathComponent("application", isDirectory: true)
        let output = application.appendingPathComponent("exefs", isDirectory: true)
        var complete = false
        defer { if !complete { try? fm.removeItem(at: staged) } }
        var hash = SHA256()
        // Hash paths and lengths along with the file bytes to distinguish editions with different assets.
        func copy(_ input: URL, _ relative: String, maxSize: Int? = nil) throws {
            let attrs = try input.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
            guard attrs.isRegularFile == true, attrs.isSymbolicLink != true, let size = attrs.fileSize,
                  maxSize.map({ size <= $0 }) ?? true else { throw ImportError.invalidFile(relative) }
            let target = application.appendingPathComponent(relative)
            guard fm.createFile(atPath: target.path, contents: nil) else { throw ImportError.invalidFile(relative) }
            let reader = try FileHandle(forReadingFrom: input)
            defer { try? reader.close() }
            let writer = try FileHandle(forWritingTo: target)
            hash.update(data: Data((relative + "\0" + String(size) + "\0").utf8))
            var total = 0
            while let bytes = try reader.read(upToCount: 1024 * 1024), !bytes.isEmpty {
                total += bytes.count
                guard total <= size else { throw ImportError.invalidFile(relative) }
                try writer.write(contentsOf: bytes)
                hash.update(data: bytes)
            }
            guard total == size else { throw ImportError.invalidFile(relative) }
            try writer.close()
        }
        let candidates = ["exefs", "ExeFS"].map { source.appendingPathComponent($0) } + [source]
        guard let exefs = candidates.first(where: { fm.fileExists(atPath: $0.appendingPathComponent("main").path) }),
              try exefs.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else {
            throw ImportError.missingMain
        }
        let main = exefs.appendingPathComponent("main")
        let mainType = try main.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey])
        guard mainType.isRegularFile == true, mainType.isSymbolicLink != true else { throw ImportError.missingMain }
        let header = try FileHandle(forReadingFrom: main)
        defer { try? header.close() }
        guard try header.read(upToCount: 4) == Data("NSO0".utf8) else { throw ImportError.missingMain }
        try fm.createDirectory(at: output, withIntermediateDirectories: true)
        let modules = ["main", "main.npdm", "rtld", "sdk"] + (0..<10).map { "subsdk" + String($0) }
        for name in modules {
            let file = exefs.appendingPathComponent(name)
            if fm.fileExists(atPath: file.path) {
                try copy(file, "exefs/" + name, maxSize: name == "main.npdm" ? 4 << 20 : 512 << 20)
            }
        }
        let nacp = source.appendingPathComponent("control.nacp")
        if fm.fileExists(atPath: nacp.path) { try copy(nacp, "control.nacp", maxSize: 4 << 20) }
        let rom = source.appendingPathComponent("romfs.bin")
        if fm.fileExists(atPath: rom.path) {
            try copy(rom, "romfs.bin")
        } else if let tree = ["romfs", "RomFS"].map({ source.appendingPathComponent($0) }).first(where: { fm.fileExists(atPath: $0.path) }) {
            func copyDirectory(_ dir: URL, _ relative: String) throws {
                let attrs = try dir.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
                guard attrs.isDirectory == true, attrs.isSymbolicLink != true else { throw ImportError.invalidFile(relative) }
                try fm.createDirectory(at: application.appendingPathComponent(relative), withIntermediateDirectories: false)
                hash.update(data: Data((relative + "/\0").utf8))
                for file in try fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: [.isDirectoryKey]).sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
                    let path = relative + "/" + file.lastPathComponent
                    if try file.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true { try copyDirectory(file, path) }
                    else { try copy(file, path) }
                }
            }
            try copyDirectory(tree, "romfs")
        }
        let id = hash.finalize().map { String(format: "%02x", $0) }.joined()
        complete = true
        return Self(id: id, name: source.lastPathComponent, staging: staged)
    }
}
