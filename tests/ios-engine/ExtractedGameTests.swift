import XCTest

final class ExtractedGameTests: XCTestCase {
    func testImportAndBootApplication() throws {
        let fm = FileManager.default
        let temp = fm.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? fm.removeItem(at: temp) }
        let source = temp.appendingPathComponent("Test game")
        let exefs = source.appendingPathComponent("exefs")
        try fm.createDirectory(at: exefs, withIntermediateDirectories: true)
        let library = temp.appendingPathComponent("library")
        try fm.createDirectory(at: library, withIntermediateDirectories: true)
        let main = exefs.appendingPathComponent("main")
        var nso = Data(repeating: 0, count: 0x104)
        func word(_ offset: Int, _ value: UInt32) {
            for i in 0..<4 { nso[offset + i] = UInt8(truncatingIfNeeded: value >> (i * 8)) }
        }
        word(0, 0x304f534e)
        word(0x10, 0x100); word(0x18, 4)
        word(0x24, 0x1000); word(0x34, 0x1000)
        word(0x100, 0xd40000e1) // svc ExitProcess
        try nso.write(to: main)
        let rom = source.appendingPathComponent("romfs.bin")
        try Data([1, 2, 3]).write(to: rom)
        let staged = try ExtractedGame.prepare(source, in: library)
        XCTAssertEqual(staged.id, try ExtractedGame.prepare(source, in: library).id)
        let app = staged.staging.appendingPathComponent("application")
        XCTAssertEqual(try Data(contentsOf: app.appendingPathComponent("romfs.bin")), Data([1, 2, 3]))
        let session = try XCTUnwrap(sc_create())
        defer { sc_destroy(session) }
        XCTAssertTrue(sc_load(session, SC_SWITCH_1, app.path, staged.staging.path), String(cString: sc_message(session)))
        XCTAssertEqual(sc_step(session, 1024).state, SC_EXITED)
        let assets = temp.appendingPathComponent("stk-data")
        try fm.createDirectory(at: assets, withIntermediateDirectories: true)
        try Data([7, 8]).write(to: assets.appendingPathComponent("sample"))
        try ExtractedGame.installSDDirectory(assets, in: staged.staging)
        XCTAssertEqual(try Data(contentsOf: staged.staging.appendingPathComponent("sdmc/stk-data/sample")), Data([7, 8]))
        XCTAssertThrowsError(try ExtractedGame.installSDDirectory(assets, in: staged.staging))
        try Data([1, 2, 4]).write(to: rom)
        XCTAssertNotEqual(staged.id, try ExtractedGame.prepare(source, in: library).id)
        try Data([0]).write(to: main)
        XCTAssertThrowsError(try ExtractedGame.prepare(source, in: library))
    }
}
