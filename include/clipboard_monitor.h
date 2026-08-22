#ifndef CLIPLITE_CLIPBOARD_MONITOR_H
#define CLIPLITE_CLIPBOARD_MONITOR_H

#include <windows.h>

#include <functional>

class ClipboardMonitor {
public:
    using UpdateHandler = std::function<void()>;

    bool start(HWND owner, UpdateHandler handler);
    void stop();
    bool handleMessage(UINT message) const;
    bool running() const { return owner_ != nullptr; }

private:
    HWND owner_ = nullptr;
    UpdateHandler handler_;
};

#endif
