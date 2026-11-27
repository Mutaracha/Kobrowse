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
    virtual QString errorDetail() const { return QString(); }
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
        const QList<QByteArray> numbers = parts.at(2).trimmed().split('.');
        if (numbers.size() != 3) return false;
        qulonglong n[3];
        for (int i = 0; i < 3; ++i) {
            if (numbers.at(i).isEmpty()) return false;
            for (char c : numbers.at(i)) if (c < '0' || c > '9') return false;
            bool ok = false; n[i] = numbers.at(i).toULongLong(&ok);
            if (!ok) return false;
        }
        return n[0] > 4 || (n[0] == 4 && (n[1] > 38 || (n[1] == 38 && n[2] >= 21908)));
    }
    QString errorDetail() const override { return error; }
    bool begin() override {
        if (start >= 0) return true;
        error.clear();
        if (!qApp || QThread::currentThread() != qApp->thread() || !owner) { error = QLatin1String("not in GUI thread"); return false; }
        if (!api.valid()) { error = QLatin1String("missing Nickel symbols: ") + missingSymbols(); return false; }
        if (!testApi) {
            QFile f("/mnt/onboard/.kobo/version");
            if (sizeof(void *) != 4) { error = QLatin1String("not 32-bit Nickel"); return false; }
            if (!f.open(QIODevice::ReadOnly)) { error = QLatin1String("cannot read firmware version"); return false; }
            const QByteArray version = f.read(512);
            if (!firmwareSupported(version)) { error = QLatin1String("firmware version unavailable or too old"); return false; }
        }
        const void *device = api.device();
        // Do not change automatic brightness until PowerSettings is ported.
        if (!device) { error = QLatin1String("Nickel device unavailable"); return false; }
        if (!api.supports(device)) { error = QLatin1String("device has no frontlight"); return false; }
        if (api.sensor(device)) { error = QLatin1String("automatic brightness sensor unsupported"); return false; }
        if (!light) {
            void *storage = ::operator new(lightBytes);
            api.constructLight(storage, owner);
            light = static_cast<QObject *>(storage);
        }
        if (api.maximum(light.data()) != 100) { error = QLatin1String("unexpected brightness range"); return false; }
        const int current = api.brightness(light.data());
        if (current < 0 || current > 100) { error = QLatin1String("invalid current brightness"); return false; }
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
    QString missingSymbols() const {
        QStringList names;
        if (!api.device) names << QLatin1String("Device::getCurrentDevice");
        if (!api.supports) names << QLatin1String("Device::supportsLight");
        if (!api.sensor) names << QLatin1String("Device::hasAmbientLightSensor");
        if (!api.constructLight) names << QLatin1String("FrontLight constructor");
        if (!api.brightness) names << QLatin1String("FrontLight::brightness");
        if (!api.maximum) names << QLatin1String("FrontLight::maxBrightness");
        if (!api.setBrightness) names << QLatin1String("FrontLight::setBrightness");
        if (!api.cancelChanging) names << QLatin1String("FrontLight::cancelBrightnessChanging");
        if (!api.constructGesture) names << QLatin1String("BrightnessEventFilter constructor");
        if (!api.finishGesture) names << QLatin1String("BrightnessEventFilter::finishGesture");
        return names.join(QLatin1String(", "));
    }
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
    QString error;
    int start = -1;
};
#endif
