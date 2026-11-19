import Metal
import UIKit
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
    func testIndependentRacingGameReachesTrack() throws {
        executionTimeAllowance = 600
        let file = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "nfm_switch", withExtension: "nro"))
        let session = try XCTUnwrap(sc_create())
        defer { sc_destroy(session) }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        XCTAssertTrue(sc_load(session, SC_SWITCH_1, file.path, root.path), String(cString: sc_message(session)))
        XCTAssertTrue(sc_set_gpu(session, false))
        let planURL = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "nfm-race", withExtension: "txt"))
        let plan = try String(contentsOf: planURL, encoding: .utf8)
        let deadline = ProcessInfo.processInfo.systemUptime + 570
        var target: UInt64 = 0
        for line in plan.split(whereSeparator: { $0.isNewline }) {
            let fields = line.split(separator: " ")
            guard fields.count == 3, let frames = UInt64(fields[0]), let buttons = UInt64(fields[1]),
                  let steer = Int32(fields[2]) else { return XCTFail("Piano di guida non valido") }
            target += frames
            sc_pad(session, buttons, steer, 0, 0, 0)
            var stats = sc_step(session, 100000)
            while stats.frames < target && stats.state == SC_RUNNING && ProcessInfo.processInfo.systemUptime < deadline {
                stats = sc_step(session, 100000)
            }
            guard stats.frames >= target && stats.state == SC_RUNNING else {
                return XCTFail("Arresto al frame " + String(stats.frames) + " · " + String(cString: sc_message(session)))
            }
        }
        // Inspect the actual guest pixels. The first track shows blue sky and an orange damage meter.
        var serial: UInt64 = 0
        XCTAssertTrue(sc_frame(session, 0, { ctx, pixels, w, h in
            guard let ctx, let pixels, w == 1280, h == 720 else { return XCTFail("Framebuffer 720p assente") }
            let width = Int(w), height = Int(h)
            let sky = pixels[(height * 16 / 100) * width + width / 2]
            let meter = pixels[(height * 22 / 1000) * width + width * 87 / 100]
            XCTAssertGreaterThan((sky >> 16) & 255, 225)
            XCTAssertGreaterThan((sky >> 8) & 255, 190)
            XCTAssertGreaterThan(meter & 255, 180)
            XCTAssertLessThan((meter >> 16) & 255, 70)
            let data = Data(bytes: pixels, count: width * height * 4)
            guard let provider = CGDataProvider(data: data as CFData),
                  let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                                      bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                                      provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent),
                  let png = UIImage(cgImage: image).pngData() else { return XCTFail("Cattura assente") }
            let capture = XCTAttachment(data: png, uniformTypeIdentifier: "public.png")
            capture.name = "Need for Madness - gara"
            capture.lifetime = .keepAlways
            Unmanaged<MetalTests>.fromOpaque(ctx).takeUnretainedValue().add(capture)
        }, Unmanaged.passUnretained(self).toOpaque(), &serial), String(cString: sc_message(session)))
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

