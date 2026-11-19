import XCTest

final class GameplayTests: XCTestCase {
    func testRallyReachesMetalAndRespondsToPause() throws {
        let app = XCUIApplication()
        app.launch()
        app.buttons["play-Rally"].tap()
        let screen = app.descendants(matching: .any)["guest-screen"].firstMatch
        let presented = NSPredicate(format: "exists == true AND value == %@", "Frame Metal")
        let ready = XCTNSPredicateExpectation(predicate: presented, object: screen)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 50), .completed)
        let shot = XCTAttachment(screenshot: app.screenshot())
        shot.name = "Rally - frame Metal"
        shot.lifetime = .keepAlways
        add(shot)
        app.buttons["Pausa"].tap()
        XCTAssertTrue(app.staticTexts["In pausa"].exists)
        app.buttons["Riprendi"].tap()
        app.buttons["+"].press(forDuration: 0.8)
        XCTAssertTrue(app.staticTexts.containing(NSPredicate(format: "label CONTAINS %@", "Processo terminato")).firstMatch.waitForExistence(timeout: 15))
    }
}

