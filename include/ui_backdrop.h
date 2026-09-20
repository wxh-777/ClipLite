#ifndef CLIPLITE_UI_BACKDROP_H
#define CLIPLITE_UI_BACKDROP_H

#include <windows.h>

namespace cliplite {

// Apply the system backdrop when supported; callers keep their normal paint fallback.
bool applySystemBackdrop(HWND hwnd, bool darkMode, bool highContrast);

}  // namespace cliplite

#endif
