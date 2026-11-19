import Metal
import XCTest

final class MetalTests: XCTestCase {
    override class func setUp() {
        super.setUp()
        // Accelerated draws stay in Metal images until presentation. The triangle homebrew reads its
        // render target with the CPU after the fence, so every GPU submission is written back to guest
        // memory. The engine reads this flag once, at the first submission of the process.
        setenv("NEXO2_VK_SYNC", "1", 1)
        setenv("NEXO2_VK_DRAW_DEBUG", "1", 1) // temporary diagnostics
    }
    func testIndependentHomebrewMenu() throws {
        let file = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "hbmenu", withExtension: "nro"),
                                 "Preparare la fixture con tools/verify-independent.py --fetch-only build-fixtures")
        let session = try XCTUnwrap(sc_create())
        defer { sc_destroy(session) }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        XCTAssertTrue(sc_load(session, SC_SWITCH_1, file.path, root.path))
        var stats = sc_step(session, 250000)
        let deadline = Date().addingTimeInterval(45)
        while stats.frames == 0 && stats.state == SC_RUNNING && Date() < deadline {
            sc_pad(session, 0, 0, 0, 0, 0)
            stats = sc_step(session, 250000)
        }
        XCTAssertGreaterThan(stats.frames, 0, String(cString: sc_message(session)))
        var number: UInt64 = 0
        XCTAssertTrue(sc_frame(session, 0, { _, pixels, width, height in
            XCTAssertEqual(width, 1280)
            XCTAssertEqual(height, 720)
            guard let pixels else { XCTFail("Framebuffer assente"); return }
            var colors = Set<UInt32>()
            for i in stride(from: 0, to: Int(width) * Int(height), by: 13) { colors.insert(pixels[i]) }
            XCTAssertGreaterThan(colors.count, 16)
        }, nil, &number))
    }
    func testGuestMaxwellTriangle() throws {
        guard MTLCreateSystemDefaultDevice() != nil else { throw XCTSkip("Metal assente sul runner.") }
        let session = try XCTUnwrap(sc_create())
        defer { sc_destroy(session) }
        let file = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "triangle", withExtension: "nro"))
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        XCTAssertTrue(sc_load(session, SC_SWITCH_1, file.path, root.path))
        XCTAssertTrue(sc_set_gpu(session, true), String(cString: sc_message(session)))
        var stats = sc_step(session, 100000)
        let deadline = Date().addingTimeInterval(25)
        while stats.state == SC_RUNNING && Date() < deadline { stats = sc_step(session, 100000) }
        XCTAssertEqual(stats.state, SC_EXITED)
        XCTAssertGreaterThan(stats.gpu_draws, 0)
        XCTAssertTrue(String(cString: sc_log(session)).contains("triangulo completo, fallos: 0"))
    }
    // Temporary diagnostics: which renderer path loses the triangle in the Simulator.
    func testTriangleDiagnostics() throws {
        guard MTLCreateSystemDefaultDevice() != nil else { throw XCTSkip("Metal assente sul runner.") }
        let file = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "triangle", withExtension: "nro"))
        let variants: [(String, [String])] = [("default", []), ("nocull", ["NEXO2_NOCULL"]),
                                              ("cpu-vs", ["NEXO2_VK_CPU_VS"]),
                                              ("cpu-vs+nocull", ["NEXO2_VK_CPU_VS", "NEXO2_NOCULL"])]
        for (name, flags) in variants {
            flags.forEach { setenv($0, "1", 1) }
            defer { flags.forEach { unsetenv($0) } }
            let session = try XCTUnwrap(sc_create())
            defer { sc_destroy(session) }
            let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
            defer { try? FileManager.default.removeItem(at: root) }
            guard sc_load(session, SC_SWITCH_1, file.path, root.path), sc_set_gpu(session, true) else {
                print("diag \(name): setup failed \(String(cString: sc_message(session)))")
                continue
            }
            var stats = sc_step(session, 100000)
            let deadline = Date().addingTimeInterval(25)
            while stats.state == SC_RUNNING && Date() < deadline { stats = sc_step(session, 100000) }
            let lines = String(cString: sc_log(session)).split(separator: "\n")
                .filter { $0.contains("FALLO") || $0.contains("pixeles") || $0.contains("fallos") }
            print("diag \(name): draws=\(stats.gpu_draws) state=\(stats.state.rawValue) \(lines.joined(separator: " | "))")
        }
    }
}

