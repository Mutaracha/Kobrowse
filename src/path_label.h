#ifndef KOBROWSE_PATH_LABEL_H
#define KOBROWSE_PATH_LABEL_H
#include <QLabel>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QDir>

// Display only: never use this shortened text for filesystem operations.
inline QString displayBrowserPath(const QString &root, const QString &path, const QFont &font, int width) {
    const QString base = QDir::cleanPath(root), absolute = QDir::cleanPath(path);
    QString shown = absolute;
    if (absolute == base) shown = QLatin1String("/");
    else if (absolute.startsWith(base + QLatin1Char('/'))) shown = absolute.mid(base.size());
    const QFontMetrics metrics(font);
    if (width <= 0) return QString();
    if (metrics.width(shown) <= width) return shown;
    const QStringList parts = shown.split(QLatin1Char('/'), QString::SkipEmptyParts);
    for (int first = 1; first < parts.size(); ++first) {
        QStringList suffix;
        for (int i = first; i < parts.size(); ++i) suffix << parts.at(i);
        const QString candidate = QString::fromUtf8("…/") + suffix.join(QLatin1String("/"));
        if (metrics.width(candidate) <= width) return candidate;
    }
    // A single very long final component cannot fit intact; elide only it.
    return metrics.elidedText(parts.isEmpty() ? shown : parts.last(), Qt::ElideLeft, width);
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
private:
    void refresh() {
        const QString text = displayBrowserPath(m_root, m_path, font(), qMax(0, contentsRect().width() - 14));
        if (QLabel::text() != text) QLabel::setText(text);
        setToolTip(m_path);
    }
    QString m_root, m_path;
};
#endif
