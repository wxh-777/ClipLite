#ifndef CLIPLITE_HOTKEY_MANAGER_H
#define CLIPLITE_HOTKEY_MANAGER_H

#include <windows.h>

#include <functional>

struct HotkeyBinding {
    UINT modifiers = 0;
    UINT virtualKey = 0;
};

class HotkeyManager {
public:
    struct Callbacks {
        std::function<void()> showHistory;
        std::function<void()> showSettings;
        std::function<void()> togglePause;
    };

    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    bool configure(HWND owner, const HotkeyBinding& history, const HotkeyBinding& settings,
                   const HotkeyBinding& pause, bool replaceWinV, Callbacks callbacks);
    void stop();
    bool handleMessage(UINT message, WPARAM wParam);
    bool registrationWarning() const { return registrationWarning_; }
    bool interceptorInstalled() const { return hook_ != nullptr; }

private:
    bool registerOne(int id, const HotkeyBinding& binding, const HotkeyBinding& fallback);
    void resetInterceptorState();
    static LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam);

    HWND owner_ = nullptr;
    HHOOK hook_ = nullptr;
    Callbacks callbacks_;
    bool registrationWarning_ = false;
    bool winKeyDown_ = false;
    bool suppressWinV_ = false;
    bool suppressVKeyUp_ = false;
    bool winKeyForwarded_ = false;
    UINT pendingWinKey_ = VK_LWIN;
    DWORD winKeyDownTime_ = 0;

    static HotkeyManager* active_;
};

#endif
