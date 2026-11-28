#include "native_keyboard.h"
#include "brightness.h" // common minimum firmware policy
#include <QApplication>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <dlfcn.h>
#include <new>

namespace {
template<class T> T symbol(const char *name) { return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name)); }
NativeKeyboard::Api resolve() {
    NativeKeyboard::Api a;
    a.frame = symbol<decltype(a.frame)>("_ZN13KeyboardFrameC1EP7QWidget");
    a.destroyFrame = symbol<decltype(a.destroyFrame)>("_ZN13KeyboardFrameD0Ev");
    a.create = symbol<decltype(a.create)>("_ZN13KeyboardFrame14createKeyboardE14KeyboardScriptRK7QLocale");
    a.receiver = symbol<decltype(a.receiver)>("_ZN16KeyboardReceiverC1EP9QLineEditb");
    a.attach = symbol<decltype(a.attach)>("_ZN24SearchKeyboardController11setReceiverEP16KeyboardReceiverb");
    a.goText = symbol<decltype(a.goText)>("_ZN24SearchKeyboardController9setGoTextERK7QString");
    return a;
}
}
NativeKeyboard::Api::Api() : frame(nullptr), destroyFrame(nullptr), create(nullptr), receiver(nullptr), attach(nullptr), goText(nullptr) {}
bool NativeKeyboard::Api::valid() const { return frame && destroyFrame && create && receiver && attach && goText; }
NativeKeyboard::NativeKeyboard(QWidget *host, QLineEdit *editor)
    : QObject(host), editor_(editor), host_(host), api_(resolve()), frameBytes_(28), receiverBytes_(36), injected_(false), ready_(false), script_(0) {}
NativeKeyboard::NativeKeyboard(QWidget *host, QLineEdit *editor, const Api &api, size_t frameBytes, size_t receiverBytes)
    : QObject(host), editor_(editor), host_(host), api_(api), frameBytes_(frameBytes), receiverBytes_(receiverBytes), injected_(true), ready_(false), script_(0) {}
NativeKeyboard::~NativeKeyboard() { reset(); }
void NativeKeyboard::reset() {
    ready_ = false;
    if (frame_) api_.destroyFrame(frame_.data());
    frame_.clear();
    if (receiver_) delete receiver_.data();
    receiver_.clear();
}
bool NativeKeyboard::create() {
    if (!host_ || !editor_ || !qApp || QThread::currentThread() != qApp->thread() || !api_.valid()) return false;
    if (!injected_) {
        QFile version("/mnt/onboard/.kobo/version");
        if (sizeof(void *) != 4 || !version.open(QIODevice::ReadOnly)
            || !NativeBrightness::firmwareSupported(version.read(512))) return false;
    }
    if (!frame_) {
        void *storage = ::operator new(frameBytes_);
        try { api_.frame(storage, host_); }
        catch (...) { ::operator delete(storage); return false; }
        frame_ = static_cast<QWidget *>(storage);
        frame_->setObjectName(QLatin1String("kobrowseNativeKeyboard"));
        frame_->setPalette(QApplication::palette());
        frame_->setStyleSheet(QLatin1String("QWidget{background:white;color:black;}"));
    }
    QObject *controller = api_.create(frame_.data(), script_, QLocale());
    if (!controller) { reset(); return false; }
    QWidget *owner = qobject_cast<QWidget *>(controller->parent());
    if (!owner || (owner != frame_ && !frame_->isAncestorOf(owner))) { reset(); return false; }
    void *storage = ::operator new(receiverBytes_);
    try { api_.receiver(storage, editor_.data(), false); }
    catch (...) { ::operator delete(storage); reset(); return false; }
    // Receiver has editor ownership (as in Kobalt); clear the previous one after
    // the new controller has taken over. Controllers live in the frame.
    QObject *oldReceiver = receiver_.data();
    receiver_ = static_cast<QObject *>(storage);
    api_.attach(controller, receiver_.data(), true);
    if (oldReceiver) delete oldReceiver;
    api_.goText(controller, QString::fromUtf8("Поиск"));
    const bool commitOk = connect(controller, SIGNAL(commitRequested()), this, SLOT(commit()));
    const bool returnOk = connect(controller, SIGNAL(returnPressed()), this, SLOT(commit()));
    const bool scriptOk = connect(controller, SIGNAL(languageChanged(KeyboardScript)), this, SLOT(changeScript(KeyboardScript)));
    if (!commitOk || !returnOk || !scriptOk) { reset(); return false; }
    ready_ = true;
    return true;
}
bool NativeKeyboard::showKeyboard() {
    if (!ready_ && !create()) return false;
    place(); frame_->show(); frame_->raise(); editor_->setFocus();
    return true;
}
void NativeKeyboard::place() {
    if (!frame_) return;
    const int h = qMax(1, frame_->sizeHint().height());
    frame_->setGeometry(0, host_->height() - h, host_->width(), h);
    if (heightChanged) heightChanged(h);
}
void NativeKeyboard::hideKeyboard() { if (frame_) frame_->hide(); }
bool NativeKeyboard::owns(QWidget *w) const { return frame_ && w && (w == frame_ || frame_->isAncestorOf(w)); }
void NativeKeyboard::commit() { if (frame_ && frame_->isVisible() && host_->isVisible() && submitted) submitted(); }
void NativeKeyboard::changeScript(KeyboardScript value) {
    script_ = int(value); ready_ = false;
    // Defer until the native signal has returned, as in Kobalt.
    QTimer *timer = new QTimer(this); timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, timer]() {
        timer->deleteLater();
        if (host_->isVisible() && !showKeyboard()) {
            if (heightChanged) heightChanged(0);
        }
    });
    timer->start(0);
}
