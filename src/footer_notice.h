#ifndef KOBROWSE_FOOTER_NOTICE_H
#define KOBROWSE_FOOTER_NOTICE_H
#include <QWidget>
#include <QPainter>
#include <QTextLayout>
#include <QFontMetrics>
#include <QStringList>
#include <QPushButton>

// A stable two-column count display with right-aligned labels.
class FooterCountsButton : public QPushButton {
public:
    explicit FooterCountsButton(QWidget *parent = nullptr) : QPushButton(parent) { setFocusPolicy(Qt::NoFocus); }
    void setCounterFont(const QFont &value) {
        setFont(value);
        const QFontMetrics fm(value);
        labelWidth = qMax(fm.width(QString::fromUtf8("файлов:")), fm.width(QString::fromUtf8("папок:")));
        int digitWidth = 0;
        for (char digit = '0'; digit <= '9'; ++digit) digitWidth = qMax(digitWidth, fm.width(QLatin1Char(digit)));
        digitColumnWidth = digitWidth;
        updateWidth();
    }
    void setCounts(int files, int folders) {
        if (fileCount == files && folderCount == folders) return;
        fileCount = files; folderCount = folders; updateWidth(); update();
    }
    QSize sizeHint() const override { return QSize(width(), QFontMetrics(font()).lineSpacing() * 2 + 2); }
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setFont(font());
        if (isDown()) p.fillRect(rect(), Qt::black);
        p.setPen(isDown() ? Qt::white : Qt::black);
        const int line = QFontMetrics(font()).lineSpacing();
        const int top = (height() - 2 * line) / 2;
        p.drawText(QRect(0, top, labelWidth, line), Qt::AlignRight | Qt::AlignVCenter, QString::fromUtf8("файлов:"));
        p.drawText(QRect(0, top + line, labelWidth, line), Qt::AlignRight | Qt::AlignVCenter, QString::fromUtf8("папок:"));
        const int space = QFontMetrics(font()).width(QLatin1Char(' '));
        p.drawText(QRect(labelWidth + space, top, width() - labelWidth - space, line), Qt::AlignLeft | Qt::AlignVCenter, QString::number(fileCount));
        p.drawText(QRect(labelWidth + space, top + line, width() - labelWidth - space, line), Qt::AlignLeft | Qt::AlignVCenter, QString::number(folderCount));
    }
private:
    void updateWidth() {
        const int digits = qMax(3, qMax(QString::number(qMax(0, fileCount)).size(), QString::number(qMax(0, folderCount)).size()));
        setFixedWidth(labelWidth + QFontMetrics(font()).width(QLatin1Char(' ')) + digits * digitColumnWidth + 2);
    }
    int labelWidth = 0, digitColumnWidth = 0, fileCount = -1, folderCount = -1;
};

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
