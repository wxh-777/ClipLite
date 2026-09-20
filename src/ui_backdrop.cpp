#include "ui_backdrop.h"

#include <dwmapi.h>

namespace {

// These values are available in newer Windows SDKs, but the project also builds with
// older SDKs, so keep the runtime feature boundary explicit.
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmSystemBackdropType = 38;
constexpr DWORD kDwmSystemBackdropNone = 1;
constexpr DWORD kDwmSystemBackdropTransientWindow = 3;

bool setDwmAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD size) {
    return DwmSetWindowAttribute(hwnd, attribute, value, size) == S_OK;
}

}  // namespace

namespace cliplite {

bool applySystemBackdrop(HWND hwnd, bool darkMode, bool highContrast) {
    if (!hwnd) return false;

    const BOOL immersiveDarkMode = darkMode ? TRUE : FALSE;
    setDwmAttribute(hwnd, kDwmUseImmersiveDarkMode, &immersiveDarkMode,
                    sizeof(immersiveDarkMode));

    const DWORD backdropType = highContrast
        ? kDwmSystemBackdropNone : kDwmSystemBackdropTransientWindow;
    return setDwmAttribute(hwnd, kDwmSystemBackdropType, &backdropType,
                           sizeof(backdropType));
}

}  // namespace cliplite
