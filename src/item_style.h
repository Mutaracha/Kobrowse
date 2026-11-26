#ifndef KOBROWSE_ITEM_STYLE_H
#define KOBROWSE_ITEM_STYLE_H
#include <QColor>

// Keep names readable on e-ink; only the icon is muted for protected entries.
inline QColor itemIconColor(bool readOnly, bool marked) {
    return marked ? QColor(Qt::white) : readOnly ? QColor(135, 135, 135) : QColor(Qt::black);
}
#endif
