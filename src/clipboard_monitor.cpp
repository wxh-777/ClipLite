#include "clipboard_monitor.h"

bool ClipboardMonitor::start(HWND owner, UpdateHandler handler) {
    if (!owner || !handler || owner_) return false;
    if (!AddClipboardFormatListener(owner)) return false;
    owner_ = owner;
    handler_ = std::move(handler);
    return true;
}

void ClipboardMonitor::stop() {
    if (owner_) RemoveClipboardFormatListener(owner_);
    owner_ = nullptr;
    handler_ = {};
}

bool ClipboardMonitor::handleMessage(UINT message) const {
    if (message != WM_CLIPBOARDUPDATE || !owner_ || !handler_) return false;
    handler_();
    return true;
}
