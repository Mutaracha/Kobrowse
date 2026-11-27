#ifndef KOBROWSE_FOOTER_NOTICE_H
#define KOBROWSE_FOOTER_NOTICE_H
#include <QWidget>
#include <QPainter>
#include <QTextLayout>
#include <QFontMetrics>
#include <QStringList>

// Constrain notices to two lines without changing the height of the file list.
inline QStringList footerNoticeLines(const QString &message, const QFont &font, int width) {
    QString text = message.simplified();
    QStringList result;
    if (text.isEmpty() || width <= 0) return result;
    QTextLayout layout(text, font);
    QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option); layout.beginLayout();
    QTextLine first = layout.createLine();
    if (first.isValid()) {
        first.setLineWidth(width);
        result << text.mid(first.textStart(), first.textLength()).trimmed();
        const int next = first.textStart() + first.textLength();
        if (next < text.size()) result << QFontMetrics(font).elidedText(text.mid(next).trimmed(), Qt::ElideRight, width);
    }
    layout.endLayout();
    return result;
}
class FooterNotice : public QWidget {
public:
    explicit FooterNotice(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setMessage(const QString &text) { if (text != message) { message = text; setAccessibleName(text); update(); } }
    QString text() const { return message; }
    QSize sizeHint() const override { return QSize(100, QFontMetrics(font()).lineSpacing() * 2 + 4); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setFont(font()); painter.setPen(Qt::black);
        const QStringList lines = footerNoticeLines(message, font(), width());
        const int h = QFontMetrics(font()).lineSpacing();
        int y = (height() - lines.size() * h) / 2;
        for (const QString &line : lines) {
            painter.drawText(QRect(0, y, width(), h), Qt::AlignCenter, line);
            y += h;
        }
    }
private:
    QString message;
};
#endif
