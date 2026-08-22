#include "hotkey_manager.h"

#include <algorithm>

HotkeyManager* HotkeyManager::active_ = nullptr;

namespace {

constexpr int kHistoryId = 1;
constexpr int kSettingsId = 3;
constexpr int kPauseId = 4;

bool isWinKey(UINT key) {
    return key == VK_LWIN || key == VK_RWIN;
}

} // namespace

HotkeyManager::~HotkeyManager() {
    stop();
}

bool HotkeyManager::registerOne(int id, const HotkeyBinding& binding,
                                const HotkeyBinding& fallback) {
    if (RegisterHotKey(owner_, id, binding.modifiers | MOD_NOREPEAT, binding.virtualKey)) {
        return true;
    }
    registrationWarning_ = true;
    return RegisterHotKey(owner_, id, fallback.modifiers | MOD_NOREPEAT, fallback.virtualKey) != FALSE;
}

bool HotkeyManager::configure(HWND owner, const HotkeyBinding& history,
                              const HotkeyBinding& settings, const HotkeyBinding& pause,
                              bool replaceWinV, Callbacks callbacks) {
    if (!owner) return false;
    stop();
    owner_ = owner;
    callbacks_ = std::move(callbacks);
    registrationWarning_ = false;
    const bool historyOk = registerOne(kHistoryId, history, HotkeyBinding{MOD_ALT, 'V'});
    const bool settingsOk = registerOne(kSettingsId, settings,
                                        HotkeyBinding{MOD_CONTROL | MOD_ALT, 'S'});
    const bool pauseOk = registerOne(kPauseId, pause,
                                     HotkeyBinding{MOD_CONTROL | MOD_SHIFT, 'P'});
    if (replaceWinV) {
        active_ = this;
        hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
        if (!hook_) {
            registrationWarning_ = true;
            active_ = nullptr;
        }
    }
    return historyOk && settingsOk && pauseOk;
}

void HotkeyManager::resetInterceptorState() {
    winKeyDown_ = false;
    suppressWinV_ = false;
    suppressVKeyUp_ = false;
    winKeyForwarded_ = false;
    pendingWinKey_ = VK_LWIN;
    winKeyDownTime_ = 0;
}

void HotkeyManager::stop() {
    if (owner_) {
        UnregisterHotKey(owner_, kHistoryId);
        UnregisterHotKey(owner_, kSettingsId);
        UnregisterHotKey(owner_, kPauseId);
    }
    if (hook_) UnhookWindowsHookEx(hook_);
    if (active_ == this) active_ = nullptr;
    hook_ = nullptr;
    owner_ = nullptr;
    callbacks_ = {};
    registrationWarning_ = false;
    resetInterceptorState();
}

bool HotkeyManager::handleMessage(UINT message, WPARAM wParam) {
    if (message != WM_HOTKEY || !owner_) return false;
    if (wParam == kHistoryId) {
        if (callbacks_.showHistory) callbacks_.showHistory();
    } else if (wParam == kSettingsId) {
        if (callbacks_.showSettings) callbacks_.showSettings();
    } else if (wParam == kPauseId) {
        if (callbacks_.togglePause) callbacks_.togglePause();
    } else {
        return false;
    }
    return true;
}

LRESULT CALLBACK HotkeyManager::keyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    HotkeyManager* manager = active_;
    if (code != HC_ACTION || !manager || !manager->hook_) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    if (!key || (key->flags & LLKHF_INJECTED)) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
    const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
    const bool winKey = isWinKey(key->vkCode);

    if (manager->winKeyDown_ && !winKey &&
        static_cast<DWORD>(key->time - manager->winKeyDownTime_) > 10000) {
        manager->resetInterceptorState();
    }

    const auto forwardWinDown = [&]() {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(manager->pendingWinKey_);
        return SendInput(1, &input, sizeof(input)) == 1;
    };

    if (winKey) {
        if (keyDown) {
            if (manager->winKeyDown_) return 1;
            manager->winKeyDown_ = true;
            manager->pendingWinKey_ = key->vkCode;
            manager->winKeyDownTime_ = key->time;
            manager->winKeyForwarded_ = false;
            return 1;
        }
        if (keyUp) {
            if (manager->winKeyDown_ && manager->suppressWinV_) {
                manager->resetInterceptorState();
                return 1;
            }
            if (manager->winKeyDown_) {
                manager->winKeyDown_ = false;
                manager->winKeyForwarded_ = forwardWinDown();
            }
            if (manager->winKeyForwarded_) {
                manager->winKeyForwarded_ = false;
                return CallNextHookEx(nullptr, code, wParam, lParam);
            }
            return 1;
        }
    }

    if (manager->winKeyDown_ && keyDown && key->vkCode == 'V') {
        manager->suppressWinV_ = true;
        manager->suppressVKeyUp_ = true;
        if (manager->owner_) PostMessageW(manager->owner_, WM_APP + 1, 0, 0);
        return 1;
    }
    if (manager->winKeyDown_ && keyDown) {
        manager->winKeyDown_ = false;
        manager->winKeyForwarded_ = forwardWinDown();
    }
    if (keyUp && manager->suppressVKeyUp_ && key->vkCode == 'V') {
        manager->suppressVKeyUp_ = false;
        return 1;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
