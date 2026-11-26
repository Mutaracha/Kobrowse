#ifndef KOBROWSE_PAGINATION_H
#define KOBROWSE_PAGINATION_H
#include <algorithm>
#include <vector>
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
// Page boundaries at whole rows. Oversized rows occupy a page and are clipped by the delegate.
inline std::vector<int> pageBoundaries(const std::vector<int> &heights, int height) {
    height = std::max(1, height);
    std::vector<int> starts(1, 0);
    int used = 0;
    for (int i = 0; i < int(heights.size()); ++i) {
        const int h = std::max(1, std::min(height, heights[i]));
        if (used && used + h > height) { starts.push_back(i); used = 0; }
        used += h;
    }
    starts.push_back(int(heights.size()));
    return starts;
}
// Returns +1 for the next page, -1 for the previous page, 0 for a non-vertical gesture.
inline int pageSwipeDirection(int dx, int dy, int threshold) {
    return std::abs(dy) > threshold && std::abs(dy) > std::abs(dx) ? (dy < 0 ? 1 : -1) : 0;
}
#endif
