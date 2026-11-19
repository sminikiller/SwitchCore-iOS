import XCTest

final class NativeDiagnosticsTests: XCTestCase {
    // The M0 panel runs its probes on open; the JIT test needs StikDebug and is left untouched here.
    func testNativeReportShowsProbeResults() throws {
        let app = XCUIApplication()
        app.launch()
        app.buttons["Diagnostica nativa"].tap()
        let report = app.descendants(matching: .any)["native-report"].firstMatch
        let finished = NSPredicate(format: "label CONTAINS %@", "[b] Indirizzi")
        XCTAssertEqual(XCTWaiter.wait(for: [XCTNSPredicateExpectation(predicate: finished, object: report)], timeout: 90),
                       .completed)
        let text = report.label
        XCTAssertTrue(text.contains("[a] Registri"), text)
        XCTAssertTrue(text.contains("CS_DEBUGGED"), text)
        XCTAssertTrue(text.contains("[e] JIT StikDebug"), text)
        XCTAssertTrue(app.buttons["Attiva JIT con StikDebug"].exists)
        XCTAssertTrue(app.buttons["Copia rapporto"].exists)
        let attachment = XCTAttachment(string: text)
        attachment.name = "Rapporto M0 nel simulatore"
        attachment.lifetime = .keepAlways
        add(attachment)
        let shot = XCTAttachment(screenshot: app.screenshot())
        shot.name = "Diagnostica nativa"
        shot.lifetime = .keepAlways
        add(shot)
    }
}
