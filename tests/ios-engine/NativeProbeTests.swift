import XCTest

// Probes of the M0 diagnostics panel (docs/nativo.md) that need no JIT, run in the simulator process.
final class NativeProbeTests: XCTestCase {
    func testRegisterProbeCountsEveryCheck() {
        let report = sc_native_registers(2, 2_000)
        XCTAssertEqual(report.threads, 2)
        #if arch(arm64)
        XCTAssertEqual(report.checks, 4_000)
        XCTAssertLessThanOrEqual(report.x18_changed, report.checks)
        XCTAssertLessThanOrEqual(report.tpidr_changed, report.checks)
        #else
        XCTAssertEqual(report.checks, 0)
        #endif
    }

    func testAddressMapCoversFourToSixtyFourGiB() {
        var map = [CChar](repeating: 0, count: 512)
        var base: UInt64 = 0
        var size: UInt64 = 0
        sc_native_addresses(&map, map.count, &base, &size)
        let cells = String(cString: map)
        XCTAssertEqual(cells.count, 240)
        XCTAssertTrue(cells.allSatisfy { $0 == "+" || $0 == "." }, cells)
        XCTAssertEqual(size % (256 << 20), 0)
        if size > 0 {
            XCTAssertGreaterThanOrEqual(base, 4 << 30)
            XCTAssertLessThanOrEqual(base + size, 64 << 30)
            XCTAssertTrue(cells.contains("+"))
        }
    }

    func testHighAddressesListEveryProbe() {
        var text = [CChar](repeating: 0, count: 256)
        sc_native_high_addresses(&text, text.count)
        let items = String(cString: text).split(separator: " ")
        XCTAssertEqual(items.map { String($0.dropLast()) },
                       ["64G", "128G", "256G", "448G", "1024G", "4096G", "16384G", "65536G"])
        XCTAssertTrue(items.allSatisfy { $0.hasSuffix("+") || $0.hasSuffix(".") })
    }

    func testCodeSigningFlagsAreReadable() {
        XCTAssertNotEqual(sc_native_cs_flags(), 0)
    }

    // A brk without the StikDebug script attached ends the process: the probe must stop before it.
    func testJitProbeRefusesWithoutDebugger() {
        guard !sc_native_debugger_attached() else { return }
        var error = [CChar](repeating: 0, count: 256)
        let result = sc_native_jit(nil, &error, error.count)
        XCTAssertFalse(result.attempted)
        XCTAssertFalse(result.detached)
        XCTAssertEqual(result.before_detach, -1)
        XCTAssertFalse(String(cString: error).isEmpty)
    }
}
