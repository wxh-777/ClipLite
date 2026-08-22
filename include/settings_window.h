#ifndef CLIPLITE_SETTINGS_WINDOW_H
#define CLIPLITE_SETTINGS_WINDOW_H

#include "render_context.h"

class SettingsWindow {
public:
    bool attach(HWND hwnd, UINT dpi);
    void detach();
    bool resize(UINT width, UINT height, UINT dpi);
    RenderContext& renderer() { return renderer_; }

private:
    RenderContext renderer_;
};

#endif
