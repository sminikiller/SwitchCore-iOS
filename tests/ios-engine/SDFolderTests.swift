import XCTest

// «Aggiungi cartella alla SD» with a switch folder of NROs: nx-hbmenu lists the NRO and starts it.
final class SDFolderTests: XCTestCase {
    func testHomebrewMenuStartsNROFromAddedSwitchFolder() throws {
        executionTimeAllowance = 400
        let menu = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "hbmenu", withExtension: "nro"),
                                 "Preparare la fixture con tools/verify-independent.py --fetch-only build-fixtures")
        let rally = try XCTUnwrap(Bundle.main.url(forResource: "rally", withExtension: "nro"))
        let fm = FileManager.default
        let temp = fm.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? fm.removeItem(at: temp) }
        let title = temp.appendingPathComponent("title")
        // nx-hbmenu creates an empty switch/ on its first launch, usually before the user adds the folder.
        try fm.createDirectory(at: title.appendingPathComponent("sdmc/switch"), withIntermediateDirectories: true)
        let folder = temp.appendingPathComponent("Files/switch")
        try fm.createDirectory(at: folder, withIntermediateDirectories: true)
        try fm.copyItem(at: rally, to: folder.appendingPathComponent("rally.nro"))
        try ExtractedGame.installSDDirectory(folder, in: title)
        XCTAssertTrue(fm.fileExists(atPath: title.appendingPathComponent("sdmc/switch/rally.nro").path))
        XCTAssertThrowsError(try ExtractedGame.installSDDirectory(folder, in: title), "Un file esistente non va sostituito")

        let session = try XCTUnwrap(sc_create())
        defer { sc_destroy(session) }
        XCTAssertTrue(sc_load(session, SC_SWITCH_1, menu.path, title.path), String(cString: sc_message(session)))
        // hbmenu presents 1280 x 720 frames, Rally a 320 x 180 field. Only symbols the app itself calls
        // survive dead stripping in the host executable, so the switch is read from the frames.
        let rallyFrame: UInt32 = 320 << 16 | 180
        var size: UInt32 = 0
        var serial: UInt64 = 0
        var stats = sc_step(session, 100_000)
        let deadline = ProcessInfo.processInfo.systemUptime + 300
        // After the first menu frames, press A every 8 frames: hbmenu starts the selected entry on key down.
        while stats.state == SC_RUNNING && size != rallyFrame && ProcessInfo.processInfo.systemUptime < deadline {
            let pressA = stats.frames >= 5 && (stats.frames / 8) % 2 == 0
            sc_pad(session, pressA ? 1 : 0, 0, 0, 0, 0)
            stats = sc_step(session, 100_000)
            withUnsafeMutablePointer(to: &size) { pointer in
                _ = sc_frame(session, serial, { context, _, width, height in
                    context?.assumingMemoryBound(to: UInt32.self).pointee = width << 16 | height
                }, pointer, &serial)
            }
        }
        let log = String(String(cString: sc_log(session)).suffix(2000))
        XCTAssertEqual(size, rallyFrame, String(cString: sc_message(session)) + "\n" + log)
    }
}
