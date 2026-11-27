#ifndef KOBROWSE_BRIGHTNESS_H
#define KOBROWSE_BRIGHTNESS_H

// Adapted from Kobalt's Qt5 NativeLight and BrightnessGesture, revision
// 4e1bd56743b20751e2429ec97cc14af169c1af70. Copyright 2026 Nico Verbruggen;
// MIT license: res/KOBALT-LICENSE.txt (also included in the device package).
#include <QApplication>
#include <QFile>
#include <QPointer>
#include <QPoint>
#include <QSize>
#include <QStringList>
#include <QThread>
#include <QtMath>
#include <dlfcn.h>
#include <new>

class BrightnessGesture {
public:
    void begin(const QPoint &point, const QSize &screen, bool enabled) {
        origin = point; height = qMax(1, screen.height()); active_ = false;
        candidate = enabled && point.x() >= 0 && point.x() <= screen.width() * .13;
    }
    bool move(const QPoint &point) {
        if (!candidate) return false;
        const QPoint delta = point - origin;
        if (!active_) {
            if (qAbs(delta.x()) > 32 && qAbs(delta.x()) >= qAbs(delta.y())) candidate = false;
            else if (qAbs(delta.y()) > 32 && qAbs(delta.y()) > qAbs(delta.x()) * 1.5) active_ = true;
        }
        return active_;
    }
    int change(const QPoint &point) const {
        const double moved = double(origin.y() - point.y()) / height;
        const int change = qRound(150 * qAbs(moved));
        return moved < 0 ? -change : change;
    }
    bool active() const { return active_; }
    void cancel() { candidate = active_ = false; }
private:
    QPoint origin;
    int height = 1;
    bool candidate = false, active_ = false;
};

class BrightnessBackend {
public:
    virtual ~BrightnessBackend() {}
    virtual bool begin() = 0;
    virtual int startLevel() const = 0;
    virtual int setLevel(int percent) = 0;
    virtual bool finish(bool save) = 0;
};

// These are private Nickel APIs, not a stable SDK. Match Kobalt's use of
// FrontLight and BrightnessEventFilter and fail closed if prerequisites are
// missing. The untested allocation ABI remains a device-testing risk.
class NativeBrightness : public BrightnessBackend {
public:
    struct Api {
        const void *(*device)();
        bool (*supports)(const void *);
        bool (*sensor)(const void *);
        void (*constructLight)(void *, QObject *);
        int (*brightness)(const void *);
        int (*maximum)(const void *);
        void (*setBrightness)(const void *, int, int);
        void (*cancelChanging)(void *);
        void (*constructGesture)(void *, QObject *, void *);
        void (*finishGesture)(void *);
        Api() : device(nullptr), supports(nullptr), sensor(nullptr), constructLight(nullptr),
            brightness(nullptr), maximum(nullptr), setBrightness(nullptr), cancelChanging(nullptr),
            constructGesture(nullptr), finishGesture(nullptr) {}
        bool valid() const {
            return device && supports && sensor && constructLight && brightness && maximum
                && setBrightness && cancelChanging && constructGesture && finishGesture;
        }
    };
    explicit NativeBrightness(QObject *parent) : owner(parent), api(resolve()) {}
    // Explicit API injection is for isolated tests; production always resolves
    // symbols and checks the firmware and 32-bit ABI before constructing.
    NativeBrightness(QObject *parent, const Api &injected, size_t lightSize, size_t gestureSize)
        : owner(parent), api(injected), testApi(true), lightBytes(lightSize), gestureBytes(gestureSize) {}
    ~NativeBrightness() override { finish(false); delete light.data(); }

    static bool firmwareSupported(const QByteArray &version) {
        const QList<QByteArray> parts = version.trimmed().split(',');
        if (parts.size() < 3) return false;
        // This first device-test build targets the user's firmware and the
        // firmware for which Kobalt documented the allocation sizes.
        return parts.at(2) == "4.38.23697" || parts.at(2) == "4.46.23836";
    }
    bool begin() override {
        if (start >= 0) return true;
        if (!qApp || QThread::currentThread() != qApp->thread() || !owner || !api.valid()) return false;
        if (!testApi) {
            QFile f("/mnt/onboard/.kobo/version");
            if (sizeof(void *) != 4 || !f.open(QIODevice::ReadOnly) || !firmwareSupported(f.read(512))) return false;
        }
        const void *device = api.device();
        // Glo HD and Libra 2 have no ambient sensor. Avoid changing automatic
        // brightness on other devices until its PowerSettings ABI is ported.
        if (!device || !api.supports(device) || api.sensor(device)) return false;
        if (!light) {
            void *storage = ::operator new(lightBytes);
            api.constructLight(storage, owner);
            light = static_cast<QObject *>(storage);
        }
        if (api.maximum(light.data()) != 100) return false;
        const int current = api.brightness(light.data());
        if (current < 0 || current > 100) return false;
        start = current;
        return true;
    }
    int startLevel() const override { return start; }
    int setLevel(int percent) override {
        if (start < 0 || !light) return -1;
        api.cancelChanging(light.data());
        const int next = qBound(0, percent, 100);
        if (api.brightness(light.data()) != next) api.setBrightness(light.data(), next, 0);
        const int actual = api.brightness(light.data());
        return actual == next ? actual : -1;
    }
    bool finish(bool save) override {
        if (start < 0) return true;
        const int original = start; start = -1;
        if (!light) return false;
        if (!save) {
            api.cancelChanging(light.data());
            if (api.brightness(light.data()) != original) api.setBrightness(light.data(), original, 0);
            return api.brightness(light.data()) == original;
        }
        if (api.brightness(light.data()) == original) return true;
        // Kobalt uses the native filter's finishGesture to persist the shared
        // brightness, then destroys it synchronously to cancel its timer.
        void *storage = ::operator new(gestureBytes);
        api.constructGesture(storage, nullptr, nullptr);
        api.finishGesture(storage);
        delete static_cast<QObject *>(storage);
        return true; // Nickel does not expose the result of the save request.
    }
private:
    template <typename T> static T symbol(const char *name) { return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name)); }
    static Api resolve() {
        Api a;
        a.device = symbol<decltype(a.device)>("_ZN6Device16getCurrentDeviceEv");
        a.supports = symbol<decltype(a.supports)>("_ZNK6Device13supportsLightEv");
        a.sensor = symbol<decltype(a.sensor)>("_ZNK6Device21hasAmbientLightSensorEv");
        a.constructLight = symbol<decltype(a.constructLight)>("_ZN10FrontLightC1EP7QObject");
        a.brightness = symbol<decltype(a.brightness)>("_ZNK10FrontLight10brightnessEv");
        a.maximum = symbol<decltype(a.maximum)>("_ZNK10FrontLight13maxBrightnessEv");
        a.setBrightness = symbol<decltype(a.setBrightness)>("_ZNK10FrontLight13setBrightnessEii");
        a.cancelChanging = symbol<decltype(a.cancelChanging)>("_ZN10FrontLight24cancelBrightnessChangingEv");
        a.constructGesture = symbol<decltype(a.constructGesture)>("_ZN21BrightnessEventFilterC1EP7QObjectP23BrightnessEventDelegate");
        a.finishGesture = symbol<decltype(a.finishGesture)>("_ZN21BrightnessEventFilter13finishGestureEv");
        return a;
    }
    QPointer<QObject> owner;
    QPointer<QObject> light;
    Api api;
    bool testApi = false;
    size_t lightBytes = 32, gestureBytes = 104;
    int start = -1;
};
#endif
