#ifndef KOBROWSE_SETTING_SLIDER_H
#define KOBROWSE_SETTING_SLIDER_H
// E-ink-friendly ticked slider, with explicit handling of touch gestures.
#include <QWidget>
#include <QPainter>
#include <QtMath>
#include <functional>

class SettingSlider : public QWidget {
public:
    SettingSlider(double minimum, double maximum, double step, QWidget *parent = nullptr, qreal scale = 1)
        : QWidget(parent), lo(minimum), increment(step), steps(qMax(1, qRound((maximum - minimum) / step))), uiScale(scale) {
        setMinimumWidth(qMax(90, qRound(120 * uiScale)));
        setMinimumHeight(qMax(48, qRound(72 * uiScale))); setFocusPolicy(Qt::NoFocus);
    }
    void setValue(double value) {
        const int next = qBound(0, qRound((value - lo) / increment), steps);
        if (position != next) { position = next; update(); }
    }
    double value() const { return lo + position * increment; }
    void begin(int x) { original = position; tracking = true; update(); move(x); }
    void move(int x) {
        if (!tracking) return;
        const int inset = qMin(qMax(12, qRound(25 * uiScale)), qMax(0, width() / 2 - 1));
        const int span = qMax(1, width() - 2 * inset);
        const int next = qBound(0, qRound(double(x - inset) * steps / span), steps);
        if (next != position) { position = next; update(); if (changed) changed(value()); }
    }
    void finish() { const bool wasTracking = tracking; tracking = false; if (wasTracking) update(); if (wasTracking && position != original && finished) finished(); }
    void cancel() {
        if (tracking && position != original) { position = original; update(); if (changed) changed(value()); }
        tracking = false; update();
    }
    std::function<void(double)> changed;
    std::function<void()> finished;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.fillRect(rect(), Qt::white); p.setRenderHint(QPainter::Antialiasing);
        const int inset = qMin(qMax(12, qRound(25 * uiScale)), qMax(0, width() / 2 - 1));
        const int span = qMax(1, width() - 2 * inset);
        const int x = inset + qRound(double(position) * span / steps);
        const int center = height() / 2;
        const int stroke = qMax(1, qRound(2 * uiScale));
        p.setPen(QPen(Qt::black, stroke));
        const int marks = qMin(steps, 12);
        for (int i = 0; i <= marks; ++i) {
            const int marker = inset + qRound(double(i) * span / qMax(1, marks));
            p.drawLine(marker, center - qRound(32 * uiScale), marker, center - qRound(24 * uiScale));
        }
        p.drawLine(inset, center, inset + span, center);
        const int fillHeight = qMax(2, qRound(4 * uiScale));
        p.fillRect(QRect(inset, center - fillHeight / 2, qMax(0, x - inset), fillHeight), Qt::black);
        const int outer = qMax(10, qRound(23 * uiScale)), inner = qMax(7, qRound(16 * uiScale));
        p.setBrush(tracking ? Qt::black : Qt::white); p.drawEllipse(QPoint(x, center), outer, outer);
        p.setPen(QPen(tracking ? Qt::white : Qt::black, stroke)); p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPoint(x, center), inner, inner);
    }
private:
    double lo, increment;
    int steps, position = 0, original = 0;
    qreal uiScale;
    bool tracking = false;
};
#endif
