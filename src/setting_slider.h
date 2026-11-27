#ifndef KOBROWSE_SETTING_SLIDER_H
#define KOBROWSE_SETTING_SLIDER_H
// E-ink-friendly ticked slider, with explicit handling of touch gestures.
#include <QWidget>
#include <QPainter>
#include <QtMath>
#include <functional>

class SettingSlider : public QWidget {
public:
    SettingSlider(double minimum, double maximum, double step, QWidget *parent = nullptr)
        : QWidget(parent), lo(minimum), increment(step), steps(qMax(1, qRound((maximum - minimum) / step))) {
        setMinimumWidth(90); setMinimumHeight(44); setFocusPolicy(Qt::NoFocus);
    }
    void setValue(double value) {
        const int next = qBound(0, qRound((value - lo) / increment), steps);
        if (position != next) { position = next; update(); }
    }
    double value() const { return lo + position * increment; }
    void begin(int x) { original = position; tracking = true; move(x); }
    void move(int x) {
        if (!tracking) return;
        const int inset = qMin(16, qMax(0, width() / 2 - 1));
        const int span = qMax(1, width() - 2 * inset);
        const int next = qBound(0, qRound(double(x - inset) * steps / span), steps);
        if (next != position) { position = next; update(); if (changed) changed(value()); }
    }
    void finish() { tracking = false; }
    void cancel() {
        if (tracking && position != original) { position = original; update(); if (changed) changed(value()); }
        tracking = false;
    }
    std::function<void(double)> changed;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(), Qt::white); p.setRenderHint(QPainter::Antialiasing);
        const int inset = qMin(16, qMax(0, width() / 2 - 1));
        const int span = qMax(1, width() - 2 * inset);
        const int x = inset + qRound(double(position) * span / steps);
        const int center = height() / 2;
        p.setPen(QPen(Qt::black, 1));
        const int marks = qMin(steps, 12);
        for (int i = 0; i <= marks; ++i) {
            const int marker = inset + qRound(double(i) * span / qMax(1, marks));
            p.drawLine(marker, center - 13, marker, center - 9);
        }
        p.drawLine(inset, center, inset + span, center);
        p.fillRect(QRect(inset, center - 1, qMax(0, x - inset), 3), Qt::black);
        p.setBrush(Qt::white); p.drawEllipse(QPoint(x, center), 11, 11); p.drawEllipse(QPoint(x, center), 8, 8);
    }
private:
    double lo, increment;
    int steps, position = 0, original = 0;
    bool tracking = false;
};
#endif
