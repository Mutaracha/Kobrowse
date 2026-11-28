#include "../src/sleep_lifecycle.h"
#include <QApplication>
#include <QWidget>
#include <QTextStream>
#include <QStringList>
#include <QVariant>
#include <cstdlib>

class FakePowerManager : public QObject {
    Q_OBJECT
public:
    void sleep() { emit aboutToSleep(); }
    void earlyWake() { emit resumingFromSleep(); }
    void lateWake() { emit resumedFromSleep(); }
signals:
    void aboutToSleep();
    void resumingFromSleep();
    void resumedFromSleep();
};
static void require(bool ok, const char *message) {
    if (!ok) { QTextStream(stderr) << "FAIL: " << message << '\n'; std::exit(1); }
}
static void restoreNow(SleepLifecycle &lifecycle) {
    require(QMetaObject::invokeMethod(&lifecycle, "tryRestore", Qt::DirectConnection), "invoke wake attempt");
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QWidget host; host.resize(600, 800); host.show();
    QWidget browser(&host), search(&host); browser.show(); search.show(); app.processEvents();
    SleepViewState state(true, false, false, false, false);
    SleepLifecycle life([&state]() { return state; });
    FakePowerManager manager;
    require(life.bind(&manager) && life.bind(&manager), "idempotent binding");
    QStringList restored;
    life.attach(&search, 1, [&restored]() { restored << "search"; });
    life.attach(&browser, 0, [&restored]() { restored << "browser"; });
    life.attach(&browser, 0, [&restored]() { restored << "duplicate"; });
    int notifications = 0; life.didRestore = [&notifications]() { ++notifications; };
    manager.sleep();
    require(life.pending() && browser.isHidden() && search.isHidden()
            && browser.property("kobrowseSleepHidden").toBool(), "sleep hides managed overlays");
    state.sleeping = true; state.sleepView = true; life.poll(); manager.earlyWake(); restoreNow(life);
    require(life.pending() && browser.isHidden(), "sleep and native sleep view block restore");
    state.sleeping = false; restoreNow(life);
    require(life.pending() && browser.isHidden(), "visible native sleep view blocks restore");
    state.sleepView = false; state.pin = true; restoreNow(life);
    require(life.pending() && search.isHidden(), "PIN blocks restore");
    state.pin = false; state.usb = true; restoreNow(life);
    require(life.pending() && browser.isHidden(), "USB blocks restore");
    state.usb = false; state.available = false; restoreNow(life);
    require(life.pending() && browser.isHidden(), "missing queries fail closed");
    state.available = true; manager.lateWake(); restoreNow(life);
    require(!life.pending() && browser.isVisible() && search.isVisible()
            && restored == (QStringList() << "browser" << "search") && notifications == 1,
            "restore in stacking order and callback once");
    manager.lateWake(); restoreNow(life);
    require(notifications == 1 && restored.size() == 2, "duplicate resume does nothing");
    restored.clear();
    host.hide(); app.processEvents();
    require(!browser.isHidden() && !search.isHidden(), "parent hide does not explicitly hide children");
    manager.sleep();
    require(life.pending() && browser.isHidden() && search.isHidden(), "capture overlays when parent already hidden");
    manager.earlyWake(); restoreNow(life);
    require(life.pending() && browser.isHidden(), "hidden parent delays restore");
    host.show(); app.processEvents(); restoreNow(life);
    require(!life.pending() && browser.isVisible() && search.isVisible(), "restore after parent returns");
    // Polling also handles a missed sleep/resume signal, without guessing based
    // on a transient empty snapshot immediately after aboutToSleep.
    restored.clear(); state.sleeping = true; life.poll();
    require(life.pending() && browser.isHidden(), "poll detects missed sleep");
    state.sleeping = false; life.poll(); restoreNow(life);
    require(!life.pending() && browser.isVisible() && search.isVisible(), "poll detects missed resume");
    manager.sleep(); life.poll();
    require(life.pending() && browser.isHidden(), "no early restore before native sleep is observed");
    search.close(); manager.earlyWake(); restoreNow(life);
    require(browser.isVisible() && search.isHidden(), "closed search is not resurrected");
    SleepLifecycle unavailable([]() { return SleepViewState(); });
    QWidget other(&host); other.show();
    unavailable.attach(&other, 0);
    require(!unavailable.bind(&manager), "missing query prevents binding");
    manager.sleep(); require(other.isVisible(), "unavailable lifecycle never hides overlay");
    QObject missingSignals;
    SleepLifecycle missing([]() { return SleepViewState(true); });
    require(!missing.bind(&missingSignals), "missing signals prevent binding");
    QTextStream(stdout) << "Sleep lifecycle tests passed\n";
}
#include "sleep_lifecycle_test.moc"
