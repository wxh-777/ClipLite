#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>

#include "clip_store.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <windowsx.h>

namespace {

constexpr std::uint32_t kStoredHtmlMagic = 0x314D5448; // HTM1
constexpr std::size_t kMaxClipboardPayload = 32u * 1024u * 1024u;

#pragma pack(push, 1)
struct StoredHtmlHeader {
    std::uint32_t magic;
    std::uint32_t textSize;
    std::uint32_t htmlSize;
};
#pragma pack(pop)

constexpr int kHotkeyAltV = 1;
constexpr int kHotkeyWinV = 2;
constexpr int kSearchEdit = 10;
constexpr int kSettingWinV = 20;
constexpr int kSettingDark = 21;
constexpr int kSettingLanguage = 22;
constexpr int kSettingClear = 23;
constexpr int kSettingSave = 24;
constexpr int kMenuPaste = 100;
constexpr int kMenuPin = 101;
constexpr int kMenuDelete = 102;
constexpr int kMenuCategoryBase = 110;
constexpr UINT kShowPopupMessage = WM_APP + 1;
constexpr UINT kTrayMessage = WM_APP + 2;
constexpr UINT kShowSettingsMessage = WM_APP + 3;
constexpr UINT kTrayId = 1;
constexpr int kTrayOpen = 200;
constexpr int kTraySettings = 201;
constexpr int kTrayExit = 202;

struct Settings {
    bool winV = false;
    bool dark = false;
    int language = -1; // -1 system, 0 English, 1 Simplified Chinese
};

struct AppState {
    HWND hidden = nullptr;
    HWND popup = nullptr;
    HWND searchEdit = nullptr;
    HWND settings = nullptr;
    HWND targetWindow = nullptr;
    HHOOK keyboardHook = nullptr;
    bool winKeyDown = false;
    bool suppressWinV = false;
    WNDPROC oldEditProc = nullptr;
    POINT popupPoint{};
    int selected = 0;
    std::string query;
    Settings settingsData;
    ClipStore store;
    std::vector<std::size_t> visible;
    std::uint64_t ignoredClipboardHash = 0;
};

AppState* g_app = nullptr;

bool systemLanguageIsChinese() {
    ULONG languageCount = 0;
    ULONG bufferSize = 0;
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languageCount, nullptr, &bufferSize) &&
        GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return false;
    }
    if (bufferSize == 0) return false;
    std::vector<wchar_t> buffer(bufferSize);
    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &languageCount, buffer.data(), &bufferSize)) {
        return false;
    }
    const std::wstring language(buffer.data());
    return language.size() >= 2 && language[0] == L'z' && language[1] == L'h';
}

bool languageIsChinese() {
    if (!g_app || g_app->settingsData.language < 0) return systemLanguageIsChinese();
    return g_app->settingsData.language == 1;
}

const wchar_t* tr(const wchar_t* en, const wchar_t* zh) {
    return languageIsChinese() ? zh : en;
}

std::wstring settingsPath() {
    return clipLiteDataDirectory() + L"\\settings.ini";
}

void loadSettings(Settings& settings) {
    std::FILE* file = nullptr;
    _wfopen_s(&file, settingsPath().c_str(), L"rb");
    if (!file) return;
    char line[128]{};
    while (std::fgets(line, sizeof(line), file)) {
        if (std::strncmp(line, "winV=1", 6) == 0) settings.winV = true;
        if (std::strncmp(line, "dark=1", 6) == 0) settings.dark = true;
        if (std::strncmp(line, "language=0", 10) == 0) settings.language = 0;
        if (std::strncmp(line, "language=1", 10) == 0) settings.language = 1;
        if (std::strncmp(line, "language=-1", 11) == 0) settings.language = -1;
    }
    std::fclose(file);
}

void saveSettings(const Settings& settings) {
    std::FILE* file = nullptr;
    _wfopen_s(&file, settingsPath().c_str(), L"wb");
    if (!file) return;
    std::fprintf(file, "winV=%d\ndark=%d\nlanguage=%d\n", settings.winV ? 1 : 0,
                 settings.dark ? 1 : 0, settings.language);
    std::fclose(file);
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"[Invalid text]";
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string wideToUtf8(const wchar_t* value, std::size_t length) {
    if (!value || !length) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                          static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length),
                        result.data(), count, nullptr, nullptr);
    return result;
}

UINT htmlClipboardFormat() {
    static const UINT format = RegisterClipboardFormatW(L"HTML Format");
    return format;
}

std::string makeStoredHtml(const std::string& text, const std::string& html) {
    if (text.size() > kMaxClipboardPayload || html.size() > kMaxClipboardPayload ||
        sizeof(StoredHtmlHeader) + text.size() + html.size() > kMaxClipboardPayload) {
        return {};
    }
    StoredHtmlHeader header{kStoredHtmlMagic,
                           static_cast<std::uint32_t>(text.size()),
                           static_cast<std::uint32_t>(html.size())};
    std::string result(reinterpret_cast<const char*>(&header), sizeof(header));
    result += text;
    result += html;
    return result;
}

bool splitStoredHtml(const std::string& payload, std::string& text, std::string& html) {
    if (payload.size() < sizeof(StoredHtmlHeader)) {
        html = payload;
        return !html.empty();
    }
    StoredHtmlHeader header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    const std::size_t dataOffset = sizeof(header);
    const std::size_t total = static_cast<std::size_t>(header.textSize) + header.htmlSize;
    if (header.magic != kStoredHtmlMagic || total != payload.size() - dataOffset) {
        html = payload;
        return !html.empty();
    }
    text.assign(payload.data() + dataOffset, header.textSize);
    html.assign(payload.data() + dataOffset + header.textSize, header.htmlSize);
    return !html.empty();
}

bool openClipboardWithRetry() {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(g_app->hidden)) return true;
        Sleep(5);
    }
    return false;
}

bool captureClipboard(ClipType& type, std::string& payload) {
    if (!openClipboardWithRetry()) return false;

    std::string htmlPayload;
    if (const UINT format = htmlClipboardFormat(); format != 0) {
        if (HANDLE handle = GetClipboardData(format)) {
            const SIZE_T size = GlobalSize(handle);
            if (size > 0 && size <= 32u * 1024u * 1024u) {
                const void* data = GlobalLock(handle);
                if (data) {
                    htmlPayload.assign(static_cast<const char*>(data), static_cast<std::size_t>(size));
                    GlobalUnlock(handle);
                }
            }
        }
    }

    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        const auto* text = static_cast<const wchar_t*>(GlobalLock(handle));
        if (text) {
            const std::size_t length = wcsnlen(text, 10u * 1024u * 1024u);
            payload = wideToUtf8(text, length);
            GlobalUnlock(handle);
            if (!payload.empty()) {
                if (!htmlPayload.empty()) {
                    std::string stored = makeStoredHtml(payload, htmlPayload);
                    if (!stored.empty()) {
                        payload = std::move(stored);
                        type = ClipType::Html;
                    } else {
                        type = ClipType::Text;
                    }
                } else {
                    type = ClipType::Text;
                }
                CloseClipboard();
                return true;
            }
        }
    }

    if (!htmlPayload.empty()) {
        payload = makeStoredHtml({}, htmlPayload);
        if (!payload.empty()) {
            type = ClipType::Html;
            CloseClipboard();
            return true;
        }
    }

    if (HANDLE handle = GetClipboardData(CF_HDROP)) {
        const auto count = DragQueryFileW(static_cast<HDROP>(handle), 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            const UINT length = DragQueryFileW(static_cast<HDROP>(handle), i, nullptr, 0);
            std::wstring path(length + 1, L'\0');
            DragQueryFileW(static_cast<HDROP>(handle), i, path.data(), length + 1);
            path.resize(length);
            if (!payload.empty()) payload.push_back('\n');
            payload += wideToUtf8(path.data(), path.size());
        }
        if (!payload.empty()) {
            type = ClipType::Files;
            CloseClipboard();
            return true;
        }
    }

    if (HANDLE handle = GetClipboardData(CF_DIBV5)) {
        const SIZE_T size = GlobalSize(handle);
        if (size >= sizeof(BITMAPV5HEADER) && size <= 32u * 1024u * 1024u) {
            const void* data = GlobalLock(handle);
            if (data) {
                payload.assign(static_cast<const char*>(data), static_cast<std::size_t>(size));
                GlobalUnlock(handle);
                type = ClipType::ImageV5;
                CloseClipboard();
                return true;
            }
        }
    }

    if (HANDLE handle = GetClipboardData(CF_DIB)) {
        const SIZE_T size = GlobalSize(handle);
        if (size > 0 && size <= 32u * 1024u * 1024u) {
            const void* data = GlobalLock(handle);
            if (data) {
                payload.assign(static_cast<const char*>(data), static_cast<std::size_t>(size));
                GlobalUnlock(handle);
                type = ClipType::Image;
                CloseClipboard();
                return true;
            }
        }
    }

    CloseClipboard();
    return false;
}

bool setClipboardDataForItem(const ClipItem& item, const std::string& payload) {
    if (!openClipboardWithRetry()) return false;
    EmptyClipboard();
    HGLOBAL memory = nullptr;
    bool ok = false;

    if (item.type == ClipType::Text) {
        const std::wstring text = utf8ToWide(payload);
        memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (memory) {
            void* destination = GlobalLock(memory);
            std::memcpy(destination, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory)) ok = true;
            else GlobalFree(memory);
        }
    } else if (item.type == ClipType::Image || item.type == ClipType::ImageV5) {
        memory = GlobalAlloc(GMEM_MOVEABLE, payload.size());
        if (memory) {
            void* destination = GlobalLock(memory);
            std::memcpy(destination, payload.data(), payload.size());
            GlobalUnlock(memory);
            const UINT format = item.type == ClipType::ImageV5 ? CF_DIBV5 : CF_DIB;
            if (SetClipboardData(format, memory)) ok = true;
            else GlobalFree(memory);
        }
    } else if (item.type == ClipType::Html) {
        const UINT format = htmlClipboardFormat();
        std::string plainText;
        std::string html;
        if (format != 0 && splitStoredHtml(payload, plainText, html)) {
            HGLOBAL htmlMemory = GlobalAlloc(GMEM_MOVEABLE, html.size());
            if (htmlMemory) {
                void* destination = GlobalLock(htmlMemory);
                std::memcpy(destination, html.data(), html.size());
                GlobalUnlock(htmlMemory);
                if (SetClipboardData(format, htmlMemory)) ok = true;
                else GlobalFree(htmlMemory);
            }
            if (!plainText.empty()) {
                const std::wstring text = utf8ToWide(plainText);
                HGLOBAL textMemory = GlobalAlloc(GMEM_MOVEABLE,
                                                  (text.size() + 1) * sizeof(wchar_t));
                if (textMemory) {
                    void* destination = GlobalLock(textMemory);
                    std::memcpy(destination, text.c_str(),
                                (text.size() + 1) * sizeof(wchar_t));
                    GlobalUnlock(textMemory);
                    if (SetClipboardData(CF_UNICODETEXT, textMemory)) ok = true;
                    else GlobalFree(textMemory);
                }
            }
        }
    } else {
        std::wstring paths = utf8ToWide(payload);
        std::replace(paths.begin(), paths.end(), L'\n', L'\0');
        const std::size_t bytes = sizeof(DROPFILES) + (paths.size() + 2) * sizeof(wchar_t);
        memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (memory) {
            auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;
            std::memcpy(reinterpret_cast<char*>(drop) + sizeof(DROPFILES), paths.c_str(),
                        (paths.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            if (SetClipboardData(CF_HDROP, memory)) ok = true;
            else GlobalFree(memory);
        }
    }
    CloseClipboard();
    return ok;
}

void refreshVisible() {
    if (!g_app->popup) return;
    g_app->visible = g_app->store.search(g_app->query);
    if (g_app->visible.empty()) g_app->selected = 0;
    else g_app->selected = std::clamp(g_app->selected, 0, static_cast<int>(g_app->visible.size()) - 1);
    InvalidateRect(g_app->popup, nullptr, FALSE);
}

void sendPaste() {
    if (!g_app->popup || g_app->visible.empty()) return;
    const int selected = std::clamp(g_app->selected, 0, static_cast<int>(g_app->visible.size()) - 1);
    const std::size_t index = g_app->visible[static_cast<std::size_t>(selected)];
    std::string payload;
    if (!g_app->store.readPayload(index, payload)) return;
    const std::uint64_t hash = g_app->store.items()[index].hash;
    if (!setClipboardDataForItem(g_app->store.items()[index], payload)) return;
    g_app->ignoredClipboardHash = hash;
    HWND target = g_app->targetWindow;
    DestroyWindow(g_app->popup);
    g_app->popup = nullptr;
    g_app->searchEdit = nullptr;
    if (!IsWindow(target)) return;
    SetForegroundWindow(target);
    Sleep(25);
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2] = inputs[1];
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3] = inputs[0];
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

void showPopup() {
    if (g_app->popup) {
        SetForegroundWindow(g_app->popup);
        return;
    }
    g_app->targetWindow = GetForegroundWindow();
    GetCursorPos(&g_app->popupPoint);
    g_app->query.clear();
    g_app->selected = 0;
    g_app->visible = g_app->store.search({});

    const int width = 480;
    const int height = std::min(600, 86 + std::max(1, static_cast<int>(g_app->visible.size())) * 62);
    HMONITOR monitor = MonitorFromPoint(g_app->popupPoint, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    int x = g_app->popupPoint.x + 12;
    int y = g_app->popupPoint.y + 12;
    if (x + width > info.rcWork.right) x = info.rcWork.right - width;
    if (y + height > info.rcWork.bottom) y = info.rcWork.bottom - height;
    x = std::max(x, static_cast<int>(info.rcWork.left));
    y = std::max(y, static_cast<int>(info.rcWork.top));
    g_app->popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"ClipLitePopup",
                                   L"ClipLite", WS_POPUP | WS_BORDER, x, y, width, height,
                                   nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_app->popup) return;
    ShowWindow(g_app->popup, SW_SHOWNOACTIVATE);
    SetForegroundWindow(g_app->popup);
}

void closePopup() {
    if (g_app->popup) DestroyWindow(g_app->popup);
    g_app->popup = nullptr;
    g_app->searchEdit = nullptr;
}

void openSettings();

bool addTrayIcon() {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_app->hidden;
    icon.uID = kTrayId;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = kTrayMessage;
    icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(icon.szTip, L"ClipLite");
    return Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

void removeTrayIcon() {
    if (!g_app || !g_app->hidden) return;
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_app->hidden;
    icon.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &icon);
}

void showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kTrayOpen, tr(L"Open history", L"打开历史"));
    AppendMenuW(menu, MF_STRING, kTraySettings, tr(L"Settings", L"设置"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, tr(L"Exit", L"退出"));
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(g_app->hidden);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                       point.x, point.y, 0, g_app->hidden, nullptr);
    DestroyMenu(menu);
    if (command == kTrayOpen) showPopup();
    else if (command == kTraySettings) openSettings();
    else if (command == kTrayExit) PostQuitMessage(0);
}

LRESULT CALLBACK lowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_app && g_app->settingsData.winV) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
        const bool injected = (key->flags & LLKHF_INJECTED) != 0;
        if (!injected && (key->vkCode == VK_LWIN || key->vkCode == VK_RWIN)) {
            g_app->winKeyDown = keyDown || (g_app->winKeyDown && !keyUp);
        }
        if (!injected && key->vkCode == 'V' && g_app->winKeyDown) {
            if (keyDown && !g_app->suppressWinV) {
                g_app->suppressWinV = true;
                PostMessageW(g_app->hidden, kShowPopupMessage, 0, 0);
                return 1;
            }
            if (keyUp && g_app->suppressWinV) {
                g_app->suppressWinV = false;
                return 1;
            }
        }
        if (!injected && key->vkCode == 'V' && keyUp && g_app->suppressWinV) {
            g_app->suppressWinV = false;
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void registerHotkeys() {
    UnregisterHotKey(g_app->hidden, kHotkeyAltV);
    UnregisterHotKey(g_app->hidden, kHotkeyWinV);
    if (g_app->keyboardHook) {
        UnhookWindowsHookEx(g_app->keyboardHook);
        g_app->keyboardHook = nullptr;
    }
    RegisterHotKey(g_app->hidden, kHotkeyAltV, MOD_ALT | MOD_NOREPEAT, 'V');
    if (g_app->settingsData.winV) {
        if (!RegisterHotKey(g_app->hidden, kHotkeyWinV, MOD_WIN | MOD_NOREPEAT, 'V')) {
            g_app->keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboardProc,
                                                     GetModuleHandleW(nullptr), 0);
            if (!g_app->keyboardHook) {
                MessageBoxW(g_app->hidden, tr(L"Unable to replace Win+V.", L"无法替换 Win+V。"),
                            L"ClipLite", MB_OK | MB_ICONWARNING);
                g_app->settingsData.winV = false;
                saveSettings(g_app->settingsData);
            }
        }
    }
}

LRESULT CALLBACK editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            sendPaste();
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            closePopup();
            return 0;
        }
        if (wParam == VK_DOWN) {
            SetFocus(g_app->popup);
            return 0;
        }
        if (wParam == VK_F10) {
            openSettings();
            return 0;
        }
    }
    return CallWindowProcW(g_app->oldEditProc, hwnd, message, wParam, lParam);
}

void paintPopup(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const bool dark = g_app->settingsData.dark;
    const COLORREF background = dark ? RGB(28, 30, 34) : RGB(247, 249, 252);
    const COLORREF text = dark ? RGB(240, 243, 247) : RGB(26, 32, 42);
    const COLORREF secondary = dark ? RGB(170, 178, 190) : RGB(90, 104, 124);
    const COLORREF selected = dark ? RGB(53, 74, 105) : RGB(220, 235, 255);
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(dc, &client, brush);
    DeleteObject(brush);

    SetBkMode(dc, TRANSPARENT);
    HFONT titleFont = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT rowFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, titleFont);
    SetTextColor(dc, text);
    TextOutW(dc, 16, 54, tr(L"Clipboard history", L"剪贴板历史"),
             static_cast<int>(wcslen(tr(L"Clipboard history", L"剪贴板历史"))));
    SelectObject(dc, rowFont);

    int y = 78;
    for (int row = 0; row < static_cast<int>(g_app->visible.size()) && y < client.bottom; ++row) {
        const ClipItem& item = g_app->store.items()[g_app->visible[static_cast<std::size_t>(row)]];
        RECT rowRect{8, y, client.right - 8, y + 56};
        if (row == g_app->selected) {
            HBRUSH selectedBrush = CreateSolidBrush(selected);
            FillRect(dc, &rowRect, selectedBrush);
            DeleteObject(selectedBrush);
        }
        std::wstring preview = utf8ToWide(item.preview);
        if (preview.empty()) preview = L"[Empty]";
        if (preview.size() > 72) preview.resize(72), preview += L"...";
        SetTextColor(dc, text);
        TextOutW(dc, 20, y + 10, preview.c_str(), static_cast<int>(preview.size()));
        std::wstring kind = (item.type == ClipType::Image || item.type == ClipType::ImageV5) ? L"Image" :
                            item.type == ClipType::Html ? L"HTML" :
                            item.type == ClipType::Files ? L"Files" : L"Text";
        SetTextColor(dc, secondary);
        TextOutW(dc, 20, y + 34, kind.c_str(), static_cast<int>(kind.size()));
        if (item.pinned) {
            const wchar_t* pin = tr(L"Pinned", L"置顶");
            TextOutW(dc, client.right - 70, y + 34, pin, static_cast<int>(wcslen(pin)));
        }
        y += 62;
    }
    if (g_app->visible.empty()) {
        const wchar_t* empty = tr(L"No clipboard history", L"暂无剪贴板记录");
        SetTextColor(dc, secondary);
        TextOutW(dc, 20, 100, empty, static_cast<int>(wcslen(empty)));
    }
    SelectObject(dc, old);
    DeleteObject(titleFont);
    DeleteObject(rowFont);
}

void createSettingsControls(HWND hwnd) {
    const bool zh = languageIsChinese();
    CreateWindowW(L"STATIC", zh ? L"ClipLite 设置" : L"ClipLite Settings",
                  WS_CHILD | WS_VISIBLE, 20, 18, 360, 28, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    HWND win = CreateWindowW(L"BUTTON", zh ? L"强制替换 Win+V" : L"Force replace Win+V",
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 62, 260, 26,
                             hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingWinV)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(win, BM_SETCHECK, g_app->settingsData.winV ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND dark = CreateWindowW(L"BUTTON", zh ? L"深色主题" : L"Dark theme",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 94, 260, 26,
                              hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingDark)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(dark, BM_SETCHECK, g_app->settingsData.dark ? BST_CHECKED : BST_UNCHECKED, 0);
    CreateWindowW(L"STATIC", zh ? L"语言" : L"Language", WS_CHILD | WS_VISIBLE,
                  20, 132, 80, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND language = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  100, 128, 180, 120, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingLanguage)),
                                  GetModuleHandleW(nullptr), nullptr);
    SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(zh ? L"自动" : L"Auto"));
    SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
    SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"简体中文"));
    const int languageSelection = g_app->settingsData.language < 0
        ? 0 : g_app->settingsData.language + 1;
    SendMessageW(language, CB_SETCURSEL, languageSelection, 0);
    CreateWindowW(L"STATIC", zh ? L"历史记录" : L"History",
                  WS_CHILD | WS_VISIBLE, 20, 176, 90, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    wchar_t count[128]{};
    swprintf_s(count, zh ? L"%zu 条记录，%llu 字节" : L"%zu records, %llu bytes",
               g_app->store.activeCount(), g_app->store.diskBytes());
    CreateWindowW(L"STATIC", count, WS_CHILD | WS_VISIBLE, 110, 176, 250, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", zh ? L"清空历史" : L"Clear history",
                  WS_CHILD | WS_VISIBLE, 20, 216, 120, 28, hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingClear)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", zh ? L"保存" : L"Save",
                  WS_CHILD | WS_VISIBLE, 270, 216, 90, 28, hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingSave)), GetModuleHandleW(nullptr), nullptr);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        wchar_t className[64]{};
        GetClassNameW(hwnd, className, 64);
        if (std::wcscmp(className, L"ClipLiteSettings") == 0) {
            createSettingsControls(hwnd);
            return 0;
        }
        if (std::wcscmp(className, L"ClipLitePopup") == 0) {
            g_app->searchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                                12, 12, 456, 30, hwnd,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEdit)),
                                                GetModuleHandleW(nullptr), nullptr);
            g_app->oldEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                g_app->searchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editProc)));
            SetFocus(g_app->searchEdit);
            return 0;
        }
    }
    if (hwnd == g_app->hidden) {
        if (message == kTrayMessage) {
            if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) showPopup();
            else if (lParam == WM_RBUTTONUP) showTrayMenu();
            return 0;
        }
        if (message == WM_HOTKEY) {
            showPopup();
            return 0;
        }
        if (message == kShowPopupMessage) {
            showPopup();
            return 0;
        }
        if (message == kShowSettingsMessage) {
            openSettings();
            return 0;
        }
        if (message == WM_CLIPBOARDUPDATE) {
            ClipType type{};
            std::string payload;
            if (captureClipboard(type, payload)) {
                const auto hash = clipLiteHash(payload);
                if (hash != g_app->ignoredClipboardHash) {
                    g_app->store.append(type, payload, hash);
                } else {
                    g_app->ignoredClipboardHash = 0;
                }
            }
            return 0;
        }
    }

    if (hwnd == g_app->settings) {
        if (message == WM_COMMAND) {
            switch (LOWORD(wParam)) {
            case kSettingClear:
                if (MessageBoxW(hwnd, tr(L"Clear all clipboard history?", L"清空全部剪贴板历史？"),
                                L"ClipLite", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    g_app->store.clear();
                }
                break;
            case kSettingSave: {
                HWND win = GetDlgItem(hwnd, kSettingWinV);
                HWND dark = GetDlgItem(hwnd, kSettingDark);
                HWND language = GetDlgItem(hwnd, kSettingLanguage);
                g_app->settingsData.winV = SendMessageW(win, BM_GETCHECK, 0, 0) == BST_CHECKED;
                g_app->settingsData.dark = SendMessageW(dark, BM_GETCHECK, 0, 0) == BST_CHECKED;
                const int languageSelection = static_cast<int>(SendMessageW(language, CB_GETCURSEL, 0, 0));
                g_app->settingsData.language = languageSelection <= 0 ? -1 : languageSelection - 1;
                saveSettings(g_app->settingsData);
                registerHotkeys();
                DestroyWindow(hwnd);
                g_app->settings = nullptr;
                break;
            }
            }
            return 0;
        }
        if (message == WM_CLOSE) {
            DestroyWindow(hwnd);
            g_app->settings = nullptr;
            return 0;
        }
    }

    if (hwnd == g_app->popup) {
        if (message == WM_COMMAND && LOWORD(wParam) == kSearchEdit &&
            HIWORD(wParam) == EN_CHANGE) {
            wchar_t value[512]{};
            GetWindowTextW(g_app->searchEdit, value, 512);
            g_app->query = wideToUtf8(value, std::wcslen(value));
            refreshVisible();
            return 0;
        }
        if (message == WM_KEYDOWN) {
            if (wParam == VK_ESCAPE) closePopup();
            else if (wParam == VK_UP) { --g_app->selected; refreshVisible(); }
            else if (wParam == VK_DOWN) { ++g_app->selected; refreshVisible(); }
            else if (wParam == VK_RETURN) sendPaste();
            else if (wParam == VK_DELETE && !g_app->visible.empty()) {
                g_app->store.remove(g_app->visible[static_cast<std::size_t>(g_app->selected)]);
                refreshVisible();
            } else if (wParam == VK_F10) openSettings();
            return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            const int row = (GET_Y_LPARAM(lParam) - 78) / 62;
            if (row >= 0 && row < static_cast<int>(g_app->visible.size())) {
                g_app->selected = row;
                sendPaste();
            }
            return 0;
        }
        if (message == WM_RBUTTONUP) {
            const int row = (GET_Y_LPARAM(lParam) - 78) / 62;
            if (row >= 0 && row < static_cast<int>(g_app->visible.size())) {
                g_app->selected = row;
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, kMenuPaste, tr(L"Paste", L"粘贴"));
                AppendMenuW(menu, MF_STRING, kMenuPin, tr(L"Toggle pin", L"切换置顶"));
                AppendMenuW(menu, MF_STRING, kMenuDelete, tr(L"Delete", L"删除"));
                HMENU categories = CreatePopupMenu();
                AppendMenuW(categories, MF_STRING, kMenuCategoryBase,
                            tr(L"General", L"常规"));
                AppendMenuW(categories, MF_STRING, kMenuCategoryBase + 1,
                            tr(L"Work", L"工作"));
                AppendMenuW(categories, MF_STRING, kMenuCategoryBase + 2,
                            tr(L"Code", L"代码"));
                AppendMenuW(categories, MF_STRING, kMenuCategoryBase + 3,
                            tr(L"Links", L"链接"));
                AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(categories),
                            tr(L"Category", L"分类"));
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd, &point);
                const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                   point.x, point.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                const std::size_t index = g_app->visible[static_cast<std::size_t>(g_app->selected)];
                if (command == kMenuPaste) sendPaste();
                else if (command == kMenuPin) g_app->store.togglePinned(index);
                else if (command == kMenuDelete) g_app->store.remove(index);
                else if (command >= kMenuCategoryBase && command < kMenuCategoryBase + 4) {
                    g_app->store.setCategory(index, static_cast<std::uint32_t>(command - kMenuCategoryBase));
                }
                refreshVisible();
            }
            return 0;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            paintPopup(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
            closePopup();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void openSettings() {
    if (g_app->settings) {
        SetForegroundWindow(g_app->settings);
        return;
    }
    g_app->settings = CreateWindowExW(WS_EX_TOOLWINDOW, L"ClipLiteSettings",
                                      tr(L"ClipLite Settings", L"ClipLite 设置"),
                                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                      CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(g_app->settings, SW_SHOW);
    UpdateWindow(g_app->settings);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    AppState app;
    g_app = &app;
    loadSettings(app.settingsData);
    if (!app.store.open()) return 1;

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW popupClass{};
    popupClass.lpfnWndProc = windowProc;
    popupClass.hInstance = instance;
    popupClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popupClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    popupClass.lpszClassName = L"ClipLitePopup";
    RegisterClassW(&popupClass);
    WNDCLASSW settingsClass = popupClass;
    settingsClass.lpszClassName = L"ClipLiteSettings";
    RegisterClassW(&settingsClass);
    WNDCLASSW hiddenClass = popupClass;
    hiddenClass.lpszClassName = L"ClipLiteHidden";
    RegisterClassW(&hiddenClass);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"ClipLite.SingleInstance");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"ClipLiteHidden", nullptr);
        if (existing) PostMessageW(existing, kShowSettingsMessage, 0, 0);
        CloseHandle(mutex);
        return 0;
    }
    app.hidden = CreateWindowExW(0, L"ClipLiteHidden", L"ClipLite", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, instance, nullptr);
    if (!app.hidden) return 1;
    AddClipboardFormatListener(app.hidden);
    registerHotkeys();
    addTrayIcon();
    PostMessageW(app.hidden, kShowSettingsMessage, 0, 0);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    RemoveClipboardFormatListener(app.hidden);
    removeTrayIcon();
    UnregisterHotKey(app.hidden, kHotkeyAltV);
    UnregisterHotKey(app.hidden, kHotkeyWinV);
    if (app.keyboardHook) UnhookWindowsHookEx(app.keyboardHook);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
