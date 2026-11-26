#ifndef KOBROWSE_ACTION_MENU_H
#define KOBROWSE_ACTION_MENU_H
#include <QList>
#include <QTreeWidget>
#include <QStyledItemDelegate>
#include <QPainter>

// One dark line, instead of two adjacent item borders.
class ActionMenuDelegate : public QStyledItemDelegate {
public:
    explicit ActionMenuDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (!(index.flags() & Qt::ItemIsEnabled)) {
            p->save();
            p->fillRect(option.rect, Qt::white);
            p->setPen(QColor(80, 80, 80));
            p->drawLine(option.rect.left() + 8, option.rect.center().y(),
                        option.rect.right() - 8, option.rect.center().y());
            p->restore();
            return;
        }
        QStyledItemDelegate::paint(p, option, index);
    }
};

inline bool emptyFooterTap(QWidget *footer, const QPoint &pointInFooter) {
    return footer && footer->rect().contains(pointInFooter) && !footer->childAt(pointInFooter);
}

struct ActionMenuEntry {
    QString text;
    int action;
    int height;
    bool enabled;
    ActionMenuEntry(const QString &t, int a, int h, bool e = true)
        : text(t), action(a), height(h), enabled(e) {}
};

// Keep the existing items if the visible actions are identical. A rebuild is
// batched inside the popup only, so it cannot invalidate the whole window.
inline bool syncActionMenu(QTreeWidget *menu, const QList<ActionMenuEntry> &entries) {
    bool same = menu->topLevelItemCount() == entries.size();
    for (int i = 0; same && i < entries.size(); ++i) {
        const QTreeWidgetItem *item = menu->topLevelItem(i);
        const ActionMenuEntry &entry = entries.at(i);
        same = item->text(0) == entry.text && item->data(0, Qt::UserRole).toInt() == entry.action
            && item->sizeHint(0).height() == entry.height
            && bool(item->flags() & Qt::ItemIsEnabled) == entry.enabled;
    }
    if (same) return false;
    const bool updates = menu->updatesEnabled();
    if (updates) menu->setUpdatesEnabled(false);
    menu->clear();
    for (const ActionMenuEntry &entry : entries) {
        QTreeWidgetItem *item = new QTreeWidgetItem(menu);
        item->setText(0, entry.text);
        item->setData(0, Qt::UserRole, entry.action);
        item->setSizeHint(0, QSize(0, entry.height));
        if (!entry.enabled) item->setFlags(Qt::NoItemFlags);
    }
    if (updates) menu->setUpdatesEnabled(true);
    return true;
}
#endif
