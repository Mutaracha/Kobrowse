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

#ifndef KOBROWSE_BUILD_NUMBER
#define KOBROWSE_BUILD_NUMBER "local"
#endif
#ifndef KOBROWSE_BUILD_COMMIT
#define KOBROWSE_BUILD_COMMIT "unknown"
#endif

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
    explicit BatteryIcon(QWidget *parent = nullptr) : QWidget(parent) { setScale(1); }
    void setScale(qreal scale) { setFixedSize(qMax(1, qRound(32 * scale)), qMax(1, qRound(20 * scale))); }
    void setBattery(BatteryStatus value) {
        if (status.percent != value.percent || status.charging != value.charging) { status = value; update(); }
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.scale(width() / 32.0, height() / 20.0);
        p.setPen(QPen(Qt::black, 1.5)); p.setBrush(Qt::white);
        p.drawRect(QRectF(1, 3, 26, 14)); p.fillRect(QRectF(28, 7, 3, 6), Qt::black);
        if (status.percent >= 0) p.fillRect(QRectF(3, 5, 22.0 * status.percent / 100, 10), Qt::black);
    }
private:
    BatteryStatus status = { -1, false };
};
class DeviceStatusBar : public QWidget {
public:
    explicit DeviceStatusBar(QWidget *parent = nullptr, const QString &powerDirectory = QString::fromLatin1("/sys/class/power_supply"))
        : QWidget(parent), powerDirectory(powerDirectory) {
        QHBoxLayout *row = new QHBoxLayout(this); row->setContentsMargins(0, 0, 0, 0);
        m_clock = new QLabel(this); m_clock->setObjectName(QLatin1String("statusClock")); row->addWidget(m_clock); row->addStretch(1);
        m_build = new QLabel(QString::fromLatin1("#%1 %2").arg(QLatin1String(KOBROWSE_BUILD_NUMBER), QLatin1String(KOBROWSE_BUILD_COMMIT)), this);
        m_build->setObjectName(QLatin1String("statusBuild")); row->addWidget(m_build); row->addStretch(1);
        m_battery = new QLabel(this); m_battery->setObjectName(QLatin1String("statusBattery")); row->addWidget(m_battery);
        m_icon = new BatteryIcon(this); row->addWidget(m_icon);
        connect(&timer, &QTimer::timeout, this, [this]() { refresh(); });
        if (QGuiApplication *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
            connect(app, &QGuiApplication::applicationStateChanged, this,
                    [this](Qt::ApplicationState state) { if (state == Qt::ApplicationActive) refresh(); });
        timer.start(60000); refresh();
    }
    void setStatusFont(const QFont &font) {
        setFont(font);
        // Nickel may supply a QLabel font via its application stylesheet.
        // Set both the actual font and a local rule on each label.
        QString family = font.family(); family.remove(QLatin1Char('"')); family.remove(QLatin1Char('\\'));
        const QString style = QString::fromLatin1("font-family: \"%1\"; font-size: %2px; font-weight: normal;")
            .arg(family).arg(font.pixelSize());
        m_clock->setFont(font); m_clock->setStyleSheet(style);
        m_build->setFont(font); m_build->setStyleSheet(style);
        m_battery->setFont(font); m_battery->setStyleSheet(style);
    }
    void setScale(qreal scale) {
        m_icon->setScale(scale);
        layout()->setSpacing(qMax(2, qRound(5 * scale)));
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
    QLabel *m_clock = nullptr, *m_build = nullptr, *m_battery = nullptr;
    BatteryIcon *m_icon = nullptr;
    QTimer timer;
};
#endif
