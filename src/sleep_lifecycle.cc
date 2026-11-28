#include "sleep_lifecycle.h"
#include "sleep_policy.h"
#include <QEvent>
#include <QVariant>
#include <QWidget>
#include <dlfcn.h>
#include <algorithm>

namespace {
template <typename T> T symbol(const char *name) { return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name)); }
typedef QObject *(*Manager)();
typedef bool (*Query)(const void *);
}
QObject *nativePowerManager() {
    const Manager get = symbol<Manager>("_ZN22N3PowerWorkflowManager14sharedInstanceEv");
    return get ? get() : nullptr;
}
SleepViewState queryNativeSleepView() {
    const Manager power = symbol<Manager>("_ZN22N3PowerWorkflowManager14sharedInstanceEv");
    const Query sleeping = symbol<Query>("_ZNK22N3PowerWorkflowManager8sleepingEv");
    const Query visible = symbol<Query>("_ZNK22N3PowerWorkflowManager16sleepViewVisibleEv");
    const Query pin = symbol<Query>("_ZNK22N3PowerWorkflowManager25pinCodeInputDialogVisibleEv");
    const Manager usb = symbol<Manager>("_ZN19PlugWorkflowManager14sharedInstanceEv");
    const Query usbVisible = symbol<Query>("_ZN19PlugWorkflowManager20pluggedInViewVisibleEv");
    if (!power || !sleeping || !visible || !pin || !usb || !usbVisible) return SleepViewState();
    const void *p = power(), *u = usb();
    if (!p || !u) return SleepViewState();
    return SleepViewState(true, sleeping(p), visible(p), pin(p), usbVisible(u));
}
SleepLifecycle::SleepLifecycle(const Query &query, QObject *parent)
    : QObject(parent), query_(query), pending_(false), observedSuspended_(false) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &SleepLifecycle::tryRestore);
}
bool SleepLifecycle::bind(QObject *manager) {
    if (manager_ == manager && manager) return true;
    if (manager_) disconnect(manager_, nullptr, this, nullptr);
    manager_.clear();
    if (!manager || !query_ || !query_().available) return false;
    const bool sleep = connect(manager, SIGNAL(aboutToSleep()), this, SLOT(aboutToSleep()), Qt::DirectConnection);
    const bool early = connect(manager, SIGNAL(resumingFromSleep()), this, SLOT(resumed()), Qt::DirectConnection);
    const bool late = connect(manager, SIGNAL(resumedFromSleep()), this, SLOT(resumed()), Qt::DirectConnection);
    if (!sleep || (!early && !late)) { disconnect(manager, nullptr, this, nullptr); return false; }
    manager_ = manager;
    return true;
}
void SleepLifecycle::attach(QWidget *window, int order, const std::function<void()> &restored) {
    if (!window) return;
    windows_.erase(std::remove_if(windows_.begin(), windows_.end(), [](const Entry &e) { return e.window.isNull(); }), windows_.end());
    for (const Entry &entry : windows_) if (entry.window == window) return;
    Entry entry; entry.window = window; entry.order = order; entry.hidden = false; entry.restored = restored;
    windows_.append(entry);
    std::stable_sort(windows_.begin(), windows_.end(), [](const Entry &a, const Entry &b) { return a.order < b.order; });
    window->installEventFilter(this);
}
bool SleepLifecycle::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::Close || event->type() == QEvent::Destroy) {
        for (Entry &entry : windows_) if (entry.window == obj) {
            entry.hidden = false;
            if (entry.window) entry.window->setProperty("kobrowseSleepHidden", false);
            entry.window.clear();
        }
    }
    return QObject::eventFilter(obj, event);
}
bool SleepLifecycle::hasHiddenWindows() const {
    for (const Entry &entry : windows_) if (entry.window && entry.hidden) return true;
    return false;
}
void SleepLifecycle::hideWindows() {
    // isHidden() checks explicit hiding, not an invisible parent. Nickel may
    // already have hidden the parent before emitting the sleep signal.
    for (Entry &entry : windows_) {
        QWidget *window = entry.window.data();
        if (!window || entry.hidden || window->isHidden()) continue;
        entry.hidden = true;
        window->setProperty("kobrowseSleepHidden", true);
        window->hide();
    }
}
void SleepLifecycle::aboutToSleep() {
    retry_.stop(); pending_ = true; observedSuspended_ = false;
    hideWindows();
    if (!hasHiddenWindows()) pending_ = false;
}
void SleepLifecycle::schedule() { if (pending_ && !retry_.isActive()) retry_.start(0); }
void SleepLifecycle::resumed() { schedule(); }
void SleepLifecycle::poll() {
    if (!manager_ || !query_) return;
    const SleepViewState state = query_();
    if (!state.available) return;
    const bool suspended = state.sleeping || state.sleepView;
    if (suspended && !pending_) { aboutToSleep(); observedSuspended_ = pending_; }
    else if (pending_ && suspended) observedSuspended_ = true;
    else if (pending_ && observedSuspended_) schedule();
}
void SleepLifecycle::tryRestore() {
    if (!pending_ || !hasHiddenWindows()) { pending_ = false; return; }
    const SleepViewState state = query_ ? query_() : SleepViewState();
    if (!sleepRecoveryAllowed(state.available, state.sleeping, state.sleepView, state.pin, state.usb)) {
        retry_.start(250); return;
    }
    // Do not mark an overlay restored until its parent can actually show it.
    for (const Entry &entry : windows_) {
        QWidget *window = entry.window.data();
        if (window && entry.hidden && window->parentWidget() && !window->parentWidget()->isVisible()) {
            retry_.start(250); return;
        }
    }
    pending_ = false; observedSuspended_ = false;
    // Entries are ordered: main window below search. A callback may close a
    // window, so hold a QPointer and re-check it before using it.
    for (Entry &entry : windows_) {
        QPointer<QWidget> window = entry.window;
        if (!window || !entry.hidden) continue;
        entry.hidden = false;
        window->setProperty("kobrowseSleepHidden", false);
        window->show(); window->raise();
        if (window && entry.restored) entry.restored();
    }
    if (didRestore) didRestore();
}
