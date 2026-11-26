#ifndef KOBROWSE_PAGINATION_H
#define KOBROWSE_PAGINATION_H
#include <algorithm>
struct PageState { int current, total; bool previous, next; };
// QScrollBar values are pixels. The last page may be shorter than pageStep.
inline PageState pageState(int value, int maximum, int step) {
    maximum = std::max(0, maximum);
    step = std::max(1, step);
    value = std::max(0, std::min(value, maximum));
    const int total = 1 + maximum / step + (maximum % step != 0);
    const int current = value == maximum ? total : 1 + value / step;
    return PageState{current, total, value > 0, value < maximum};
}
#endif
