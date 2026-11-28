#include "../src/native_keyboard.h"
#include <QApplication>
#include <QTextStream>
#include <QTimer>
#include <cstdlib>
#include <new>

enum KeyboardScript : int { Latin = 0, Cyrillic = 1 };
class FakeFrame : public QWidget {
public:
    explicit FakeFrame(QWidget *parent) : QWidget(parent) {}
    QSize sizeHint() const override { return QSize(500, 170); }
};
class FakeReceiver : public QObject {
public:
    explicit FakeReceiver(QLineEdit *editor) : QObject(editor) {}
};
class FakeController : public QObject {
    Q_OBJECT
public:
    explicit FakeController(QWidget *parent) : QObject(parent) {}
    void sendCommit() { emit commitRequested(); }
    void sendReturn() { emit returnPressed(); }
    void sendLanguage() { emit languageChanged(Cyrillic); }
signals:
    void commitRequested();
    void returnPressed();
    void languageChanged(KeyboardScript);
};
static FakeController *controller = nullptr;
static int frames = 0, destroys = 0, receivers = 0, script = -1;
static bool failCreate = false;
static void frame(void *p, QWidget *parent) { new (p) FakeFrame(parent); ++frames; }
static void destroy(QWidget *p) { ++destroys; delete p; }
static QObject *create(QWidget *parent, int s, const QLocale &) {
    script = s;
    if (failCreate) return nullptr;
    controller = new FakeController(parent); return controller;
}
static void receiver(void *p, QLineEdit *e, bool) { new (p) FakeReceiver(e); ++receivers; }
static void attach(QObject *, QObject *, bool) {}
static void goText(QObject *, const QString &) {}
static void require(bool ok, const char *message) {
    if (!ok) { QTextStream(stderr) << "FAIL: " << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QWidget host; host.resize(600, 800); QLineEdit editor(&host); host.show();
    NativeKeyboard::Api api; api.frame = frame; api.destroyFrame = destroy; api.create = create;
    api.receiver = receiver; api.attach = attach; api.goText = goText;
    int calls = 0, height = 0;
    {
        NativeKeyboard native(&host, &editor, api, sizeof(FakeFrame), sizeof(FakeReceiver));
        native.submitted = [&]() { ++calls; };
        native.heightChanged = [&](int h) { height = h; };
        require(native.showKeyboard() && height == 170 && script == 0, "show native keyboard");
        QWidget *widget = host.findChild<QWidget *>("kobrowseNativeKeyboard");
        require(widget && native.owns(widget) && widget->geometry() == QRect(0, 630, 600, 170), "placement and ownership");
        controller->sendCommit(); controller->sendReturn(); require(calls == 2, "controller signals forwarded");
        host.resize(500, 700); native.place(); require(widget->geometry() == QRect(0, 530, 500, 170), "resize moves keyboard");
        controller->sendLanguage(); app.processEvents(); app.processEvents();
        require(script == 1 && receivers == 2 && frames == 1, "language change recreates controller safely");
        native.hideKeyboard(); controller->sendCommit(); require(calls == 2, "hidden keyboard does not submit");
        require(native.showKeyboard(), "show again");
    }
    require(destroys == 1 && !host.findChild<QWidget *>("kobrowseNativeKeyboard"), "frame destroyed once");
    failCreate = true;
    { NativeKeyboard native(&host, &editor, api, sizeof(FakeFrame), sizeof(FakeReceiver));
      require(!native.showKeyboard(), "failed creation falls back"); }
    require(destroys == 2, "failed creation cleans frame");
    api.create = nullptr;
    { NativeKeyboard native(&host, &editor, api, sizeof(FakeFrame), sizeof(FakeReceiver));
      require(!native.showKeyboard(), "missing symbol fails closed"); }
    require(frames == 2 && destroys == 2, "missing symbol creates nothing");
    QTextStream(stdout) << "Native keyboard adapter tests passed\n";
}
#include "native_keyboard_test.moc"
