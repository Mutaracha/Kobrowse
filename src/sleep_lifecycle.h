#ifndef KOBROWSE_SLEEP_LIFECYCLE_H
#define KOBROWSE_SLEEP_LIFECYCLE_H

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <functional>

class QWidget;

// A snapshot is valid only if all necessary native queries and managers exist.
struct SleepViewState {
    bool available, sleeping, sleepView, pin, usb;
    SleepViewState(bool available_ = false, bool sleeping_ = false, bool sleepView_ = false,
                   bool pin_ = false, bool usb_ = false)
        : available(available_), sleeping(sleeping_), sleepView(sleepView_), pin(pin_), usb(usb_) {}
};

// Coordinates only our own overlays. Never hooks or calls Nickel sleep methods.
class SleepLifecycle : public QObject {
    Q_OBJECT
public:
    typedef std::function<SleepViewState()> Query;
    explicit SleepLifecycle(const Query &query, QObject *parent = nullptr);
    bool bind(QObject *manager);
    void attach(QWidget *window, int order, const std::function<void()> &restored = std::function<void()>());
    void poll();
    bool pending() const { return pending_; }
    bool connected() const { return !manager_.isNull(); }
    std::function<void()> didRestore;
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
private slots:
    void aboutToSleep();
    void resumed();
    void tryRestore();
private:
    struct Entry {
        QPointer<QWidget> window;
        int order;
        bool hidden;
        std::function<void()> restored;
    };
    void hideWindows();
    bool hasHiddenWindows() const;
    void schedule();
    Query query_;
    QPointer<QObject> manager_;
    QVector<Entry> windows_;
    QTimer retry_;
    bool pending_, observedSuspended_;
};

// Resolves only queries; missing information fails closed. No Qt-private objects
// are allocated and no PLT hook is installed.
SleepViewState queryNativeSleepView();
QObject *nativePowerManager();
#endif
