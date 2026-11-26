#include "../src/pagination.h"
#include "../src/action_menu.h"
#include <QApplication>
#include <QTreeWidget>
#include <QScrollBar>
#include <QEvent>
#include <QTextStream>
#include <cstdlib>

class PaintCounter : public QObject {
public:
    int paints = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) ++paints;
        return false;
    }
};
static void require(bool ok, const char *message) {
    if (!ok) { QTextStream(stderr) << "FAIL: " << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTreeWidget menu;
    menu.setColumnCount(1); menu.setHeaderHidden(true);
    int inserted = 0, removed = 0;
    QObject::connect(menu.model(), &QAbstractItemModel::rowsInserted, [&inserted](const QModelIndex &, int, int) { ++inserted; });
    QObject::connect(menu.model(), &QAbstractItemModel::rowsRemoved, [&removed](const QModelIndex &, int, int) { ++removed; });
    QList<ActionMenuEntry> actions;
    actions << ActionMenuEntry(QString::fromUtf8("Архивировать"), 3, 60)
            << ActionMenuEntry(QString(), 0, 8, false)
            << ActionMenuEntry(QString::fromUtf8("Закрыть"), 7, 60);
    require(syncActionMenu(&menu, actions), "initial menu build");
    require(menu.topLevelItemCount() == 3 && !(menu.topLevelItem(1)->flags() & Qt::ItemIsEnabled), "menu separator disabled");
    menu.resize(300, 250); menu.show();
    PaintCounter paints; menu.viewport()->installEventFilter(&paints);
    app.processEvents(); app.processEvents();
    const int oldInserted = inserted, oldRemoved = removed, oldPaints = paints.paints;
    require(!syncActionMenu(&menu, actions), "unchanged menu reused");
    app.processEvents();
    require(inserted == oldInserted && removed == oldRemoved, "no model churn on unchanged menu");
    require(paints.paints == oldPaints, "unchanged menu does not request a repaint");
    actions.prepend(ActionMenuEntry(QString::fromUtf8("Домашняя"), 4, 60));
    require(syncActionMenu(&menu, actions), "changed menu rebuilt");
    require(menu.topLevelItemCount() == 4 && menu.topLevelItem(0)->data(0, Qt::UserRole).toInt() == 4,
            "changed menu actions preserved");
    menu.setUpdatesEnabled(false);
    actions.removeFirst();
    syncActionMenu(&menu, actions);
    require(!menu.updatesEnabled(), "preserve caller update state");
    menu.setUpdatesEnabled(true);
    QTreeWidget list;
    list.setColumnCount(1); list.setHeaderHidden(true); list.setRootIsDecorated(false);
    list.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list.setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list.resize(400, 180);
    const int sizes[] = {50, 50, 90, 25, 70, 50, 45};
    for (int i = 0; i < 7; ++i) {
        QTreeWidgetItem *item = new QTreeWidgetItem(&list);
        item->setText(0, QString::number(i)); item->setSizeHint(0, QSize(300, sizes[i]));
    }
    list.show(); app.processEvents();
    const int available = list.viewport()->height();
    require(available > 1, "viewport laid out");
    std::vector<int> heights;
    for (int i = 0; i < 7; ++i) heights.push_back(sizes[i]);
    const std::vector<int> pages = pageBoundaries(heights, available);
    require(pages.size() > 2, "multiple pages");
    int seen = 0;
    for (int page = 0; page + 1 < int(pages.size()); ++page) {
        for (int i = 0; i < 7; ++i) list.topLevelItem(i)->setHidden(i < pages[page] || i >= pages[page+1]);
        list.doItemsLayout(); list.scrollToTop(); app.processEvents();
        for (int i = 0; i < 7; ++i) {
            if (i < pages[page] || i >= pages[page+1]) continue;
            ++seen;
            QRect rect = list.visualItemRect(list.topLevelItem(i));
            require(rect.top() >= 0 && rect.bottom() < available, "whole row visible");
        }
    }
    require(seen == 7, "each row appears exactly once");
    QTextStream(stdout) << "Pagination and action menu widget tests passed\n";
}
