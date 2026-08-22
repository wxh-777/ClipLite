#ifndef CLIPLITE_HISTORY_WINDOW_H
#define CLIPLITE_HISTORY_WINDOW_H

#include "render_context.h"

class HistoryWindow {
public:
    bool attach(HWND hwnd, UINT dpi);
    void detach();
    bool resize(UINT width, UINT height, UINT dpi);
    RenderContext& renderer() { return renderer_; }

private:
    RenderContext renderer_;
};

#endif
