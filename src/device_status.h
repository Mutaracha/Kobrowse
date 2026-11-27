#ifndef KOBROWSE_DEVICE_STATUS_H
#define KOBROWSE_DEVICE_STATUS_H
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QTime>
#include <QTimer>
#include <QGuiApplication>

struct BatteryStatus { int percent; bool charging; };
inline BatteryStatus readBatteryStatus(const QString &directory) {
    BatteryStatus result = { -1, false };
    const QDir supplies(directory);
    const QStringList entries = supplies.entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : entries) {
        const QString path = supplies.filePath(name);
        QFile type(path + QLatin1String("/type"));
        if (!type.open(QIODevice::ReadOnly) || QString::fromLatin1(type.readAll()).trimmed().compare(QLatin1String("Battery"), Qt::CaseInsensitive)) continue;
        QFile capacity(path + QLatin1String("/capacity"));
        if (!capacity.open(QIODevice::ReadOnly)) continue;
        bool ok = false;
        const int value = QString::fromLatin1(capacity.readAll()).trimmed().toInt(&ok);
        if (!ok || value < 0 || value > 100) continue;
        result.percent = value;
        QFile status(path + QLatin1String("/status"));
        if (status.open(QIODevice::ReadOnly)) {
            const QString state = QString::fromLatin1(status.readAll()).trimmed();
            result.charging = state.compare(QLatin1String("Charging"), Qt::CaseInsensitive) == 0
                           || state.compare(QLatin1String("Full"), Qt::CaseInsensitive) == 0;
        }
        break;
    }
    return result;
}
class BatteryIcon : public QWidget {
public:
    explicit BatteryIcon(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(40, 24); }
    void setBattery(BatteryStatus value) {
        if (status.percent != value.percent || status.charging != value.charging) { status = value; update(); }
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setPen(QPen(Qt::black, 2)); p.setBrush(Qt::white);
        p.drawRect(QRect(1, 3, 33, 18)); p.fillRect(QRect(35, 9, 3, 7), Qt::black);
        if (status.percent >= 0) p.fillRect(QRect(4, 6, 27 * status.percent / 100, 13), Qt::black);
    }
private:
    BatteryStatus status = { -1, false };
};
class DeviceStatusBar : public QWidget {
public:
    explicit DeviceStatusBar(QWidget *parent = nullptr, const QString &powerDirectory = QString::fromLatin1("/sys/class/power_supply"))
        : QWidget(parent), powerDirectory(powerDirectory) {
        QHBoxLayout *row = new QHBoxLayout(this); row->setContentsMargins(0, 0, 0, 0);
        m_clock = new QLabel(this); row->addWidget(m_clock); row->addStretch(1);
        m_battery = new QLabel(this); row->addWidget(m_battery);
        m_icon = new BatteryIcon(this); row->addWidget(m_icon);
        connect(&timer, &QTimer::timeout, this, [this]() { refresh(); });
        if (QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
            connect(app, &QGuiApplication::applicationStateChanged, this,
                    [this](Qt::ApplicationState state) { if (state == Qt::ApplicationActive) refresh(); });
        timer.start(60000); refresh();
    }
    void refresh() {
        const QString now = QTime::currentTime().toString(QLatin1String("H:mm"));
        if (m_clock->text() != now) m_clock->setText(now);
        const BatteryStatus battery = readBatteryStatus(powerDirectory);
        const QString text = battery.percent < 0 ? QString::fromLatin1("--%")
            : QString::number(battery.percent) + QLatin1String("%") + (battery.charging ? QLatin1String(" +") : QLatin1String(""));
        if (m_battery->text() != text) m_battery->setText(text);
        m_icon->setBattery(battery);
    }
private:
    QString powerDirectory;
    QLabel *m_clock = nullptr, *m_battery = nullptr;
    BatteryIcon *m_icon = nullptr;
    QTimer timer;
};
#endif
