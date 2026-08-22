#include "settings_window.h"

bool SettingsWindow::attach(HWND hwnd, UINT dpi) {
    return renderer_.attach(hwnd, dpi);
}

void SettingsWindow::detach() {
    renderer_.detach();
}

bool SettingsWindow::resize(UINT width, UINT height, UINT dpi) {
    return renderer_.resize(width, height, dpi);
}
