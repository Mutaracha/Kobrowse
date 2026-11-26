#include "../src/pagination.h"
#include <QApplication>
#include <QTreeWidget>
#include <QScrollBar>
#include <QTextStream>
#include <cstdlib>

static void require(bool ok, const char *message) {
    if (!ok) { QTextStream(stderr) << "FAIL: " << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
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
    QTextStream(stdout) << "Pagination widget test passed\n";
}
