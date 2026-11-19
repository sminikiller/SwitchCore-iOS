import Metal
import XCTest

final class MetalTests: XCTestCase {
    override class func setUp() {
        super.setUp()
        // Accelerated draws stay in Metal images until presentation. The triangle homebrew reads its
        // render target with the CPU after the fence, so every GPU submission is written back to guest
        // memory. The engine reads this flag once, at the first submission of the process.
        setenv("NEXO2_VK_SYNC", "1", 1)
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
    func testIndependentRacingGameFirstFrame() throws {
        let file = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "nfm_switch", withExtension: "nro"))
        let session = try XCTUnwrap(sc_create())
        defer { sc_destroy(session) }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        XCTAssertTrue(sc_load(session, SC_SWITCH_1, file.path, root.path), String(cString: sc_message(session)))
        XCTAssertEqual(String(cString: sc_title(session)), "Need for Madness")
        XCTAssertTrue(sc_set_gpu(session, false))
        var stats = sc_step(session, 250000)
        let deadline = Date().addingTimeInterval(70)
        while stats.frames == 0 && stats.state == SC_RUNNING && Date() < deadline { stats = sc_step(session, 250000) }
        var serial: UInt64 = 0
        XCTAssertTrue(sc_frame(session, 0, { _, pixels, w, h in
            XCTAssertEqual(w, 1280)
            XCTAssertEqual(h, 720)
            guard let pixels else { return XCTFail("Framebuffer assente") }
            let colors = Set(stride(from: 0, to: Int(w) * Int(h), by: 17).map { pixels[$0] })
            XCTAssertGreaterThan(colors.count, 12)
        }, nil, &serial), String(cString: sc_message(session)))
    }
    // Vertices shaded on the CPU, Maxwell fragment shader translated to Metal through MoltenVK.
    func testGuestMaxwellTriangle() throws {
        try runTriangle(gpuVertexShader: false)
    }
    // Maxwell vertex shader translated as well; it reads its inputs from storage buffers.
    func testGuestMaxwellVertexShader() throws {
        try runTriangle(gpuVertexShader: true)
    }
    private func runTriangle(gpuVertexShader: Bool) throws {
        guard MTLCreateSystemDefaultDevice() != nil else { throw XCTSkip("Metal assente sul runner.") }
        if !gpuVertexShader { setenv("NEXO2_VK_CPU_VS", "1", 1) }
        defer { unsetenv("NEXO2_VK_CPU_VS") }
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
        let log = String(cString: sc_log(session))
        XCTAssertTrue(log.contains("triangulo completo, fallos: 0"), log)
    }
}

