#ifndef KOBROWSE_PATH_LABEL_H
#define KOBROWSE_PATH_LABEL_H
#include <QLabel>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QDir>
#include <QPainter>

// Display only: never use this shortened text for filesystem operations.
inline QString displayBrowserPath(const QString &root, const QString &path, const QFont &font, int width) {
    const QString base = QDir::cleanPath(root), absolute = QDir::cleanPath(path);
    QString shown = absolute;
    const bool isRoot = absolute == base;
    if (isRoot) shown = QLatin1String("/");
    else if (absolute.startsWith(base + QLatin1Char('/'))) shown = absolute.mid(base.size());
    if (!isRoot && !shown.endsWith(QLatin1Char('/'))) shown += QLatin1Char('/');
    const QFontMetrics metrics(font);
    if (width <= 0) return QString();
    if (metrics.width(shown) <= width) return shown;
    const QStringList parts = shown.split(QLatin1Char('/'), QString::SkipEmptyParts);
    for (int first = 1; first < parts.size(); ++first) {
        QStringList suffix;
        for (int i = first; i < parts.size(); ++i) suffix << parts.at(i);
        const QString candidate = QString::fromUtf8("…/") + suffix.join(QLatin1String("/")) + QLatin1Char('/');
        if (metrics.width(candidate) <= width) return candidate;
    }
    // Keep the trailing slash when eliding a single long component.
    const QString slash = QLatin1String("/");
    const int slashWidth = metrics.width(slash);
    if (width < slashWidth) return QString();
    if (parts.isEmpty()) return slash;
    return metrics.elidedText(parts.last(), Qt::ElideLeft, width - slashWidth) + slash;
}

class BrowserPathLabel : public QLabel {
public:
    explicit BrowserPathLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setWordWrap(false); setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setTextFormat(Qt::PlainText);
    }
    void setPath(const QString &root, const QString &path) { m_root = root; m_path = path; refresh(); }
protected:
    void resizeEvent(QResizeEvent *event) override { QLabel::resizeEvent(event); refresh(); }
    void paintEvent(QPaintEvent *event) override {
        QLabel::paintEvent(event);
        QPainter painter(this);
        painter.setPen(QPen(QColor(80, 80, 80), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(3, 3, -4, -4));
    }
private:
    void refresh() {
        const QString text = displayBrowserPath(m_root, m_path, font(), qMax(0, contentsRect().width() - 14));
        if (QLabel::text() != text) QLabel::setText(text);
        setToolTip(m_path);
    }
    QString m_root, m_path;
};
#endif
