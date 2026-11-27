#include "../src/brightness.h"
#include <QApplication>
#include <QTextStream>
#include <cstdlib>

static void require(bool ok, const char *message) {
    if (!ok) { QTextStream(stderr) << "FAIL: " << message << '\n'; std::exit(1); }
}
static int level = 40, saves = 0, cancellations = 0, lightDestroyed = 0, gestureDestroyed = 0;
static bool hasSensor = false, hasLight = true, failSet = false;
struct FakeLight : QObject {
    explicit FakeLight(QObject *parent) : QObject(parent) {}
    ~FakeLight() override { ++lightDestroyed; }
};
struct FakeGesture : QObject {
    ~FakeGesture() override { ++gestureDestroyed; }
};
static const void *device() { return &level; }
static bool supports(const void *) { return hasLight; }
static bool sensor(const void *) { return hasSensor; }
static void constructLight(void *p, QObject *parent) { new (p) FakeLight(parent); }
static int brightness(const void *) { return level; }
static int maximum(const void *) { return 100; }
static void setBrightness(const void *, int value, int) { if (!failSet) level = value; }
static void cancelChanging(void *) { ++cancellations; }
static void constructGesture(void *p, QObject *, void *) { new (p) FakeGesture; }
static void finishGesture(void *) { ++saves; }

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    BrightnessGesture g;
    const QSize screen(1000, 1000);
    g.begin(QPoint(100, 600), screen, true);
    require(!g.move(QPoint(101, 570)), "gesture has dead zone");
    require(g.move(QPoint(102, 500)) && g.change(QPoint(102, 500)) == 15, "vertical gesture and sensitivity");
    require(g.change(QPoint(100, 700)) == -15, "negative change symmetric");
    require(g.change(QPoint(100, 600)) == 0, "return to origin");
    g.cancel(); require(!g.move(QPoint(100, 300)), "cancel stops gesture");
    g.begin(QPoint(131, 600), screen, true);
    require(!g.move(QPoint(131, 400)), "outside edge");
    g.begin(QPoint(5, 600), screen, true);
    require(!g.move(QPoint(60, 600)) && !g.move(QPoint(5, 400)), "horizontal intent cannot become brightness");
    g.begin(QPoint(5, 600), screen, false);
    require(!g.move(QPoint(5, 400)), "disabled gesture");
    require(NativeBrightness::firmwareSupported("serial,kernel,4.38.23697,other\n"), "target firmware accepted");
    require(NativeBrightness::firmwareSupported("serial,kernel,4.46.23836"), "documented Kobalt ABI accepted");
    require(!NativeBrightness::firmwareSupported("serial,kernel,4.38.99999"), "unknown firmware refused");
    require(!NativeBrightness::firmwareSupported("bad"), "malformed firmware refused");

    NativeBrightness::Api api;
    api.device = device; api.supports = supports; api.sensor = sensor; api.constructLight = constructLight;
    api.brightness = brightness; api.maximum = maximum; api.setBrightness = setBrightness;
    api.cancelChanging = cancelChanging; api.constructGesture = constructGesture; api.finishGesture = finishGesture;
    QObject parent;
    {
        NativeBrightness::Api missing = api; missing.finishGesture = nullptr;
        NativeBrightness unavailable(&parent, missing, sizeof(FakeLight), sizeof(FakeGesture));
        require(!unavailable.begin(), "missing symbol disables native brightness");
        hasSensor = true;
        NativeBrightness light(&parent, api, sizeof(FakeLight), sizeof(FakeGesture));
        require(!light.begin(), "sensor device disabled to preserve automatic mode");
        hasSensor = false; hasLight = false;
        require(!light.begin(), "device without light disabled");
        hasLight = true;
        require(light.begin() && light.startLevel() == 40, "begin reads current level");
        require(light.setLevel(120) == 100 && level == 100, "clamp at maximum");
        require(light.setLevel(-2) == 0 && level == 0, "clamp at minimum");
        require(light.finish(false) && level == 40 && saves == 0, "cancel restores without saving");
        require(light.begin() && light.setLevel(75) == 75 && light.finish(true), "save completed gesture");
        require(saves == 1 && gestureDestroyed == 1 && level == 75, "native finish and immediate destruction");
        require(light.begin() && light.setLevel(75) == 75 && light.finish(true) && saves == 1, "unchanged gesture does not persist");
        require(light.begin(), "begin failure simulation"); failSet = true;
        require(light.setLevel(50) == -1, "device write failure reported");
        failSet = false; require(light.finish(false) && level == 75, "restore after device failure");
        require(light.begin() && light.setLevel(30) == 30, "active gesture before destruction");
    }
    require(level == 75 && lightDestroyed == 1 && saves == 1 && cancellations > 0, "destruction cancels and frees native object");
    QTextStream(stdout) << "Brightness gesture and mock native backend tests passed\n";
}
