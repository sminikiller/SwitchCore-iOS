import Metal
import XCTest

final class MetalTests: XCTestCase {
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
}

