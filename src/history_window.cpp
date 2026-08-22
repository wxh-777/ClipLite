#include "history_window.h"

bool HistoryWindow::attach(HWND hwnd, UINT dpi) {
    return renderer_.attach(hwnd, dpi);
}

void HistoryWindow::detach() {
    renderer_.detach();
}

bool HistoryWindow::resize(UINT width, UINT height, UINT dpi) {
    return renderer_.resize(width, height, dpi);
}
