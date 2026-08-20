#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shellscalingapi.h>
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
constexpr int kSettingMaxItems = 25;
constexpr int kSettingRetentionDays = 26;
constexpr int kSettingMaxDiskMb = 27;
constexpr int kSettingPause = 28;
constexpr int kSettingStartup = 29;
constexpr int kSettingEncrypt = 30;
constexpr int kSettingCategoryBase = 31;
constexpr int kMenuPaste = 100;
constexpr int kMenuPin = 101;
constexpr int kMenuDelete = 102;
constexpr int kFilterAll = 130;
constexpr int kFilterText = 131;
constexpr int kFilterFiles = 132;
constexpr int kFilterImage = 133;
constexpr int kFilterHtml = 134;
constexpr int kFilterPinned = 135;
constexpr int kFilterOther = 136;
constexpr UINT kShowPopupMessage = WM_APP + 1;
constexpr UINT kTrayMessage = WM_APP + 2;
constexpr UINT kShowSettingsMessage = WM_APP + 3;
constexpr UINT kExitMessage = WM_APP + 4;
constexpr UINT kTrayId = 1;
constexpr int kPopupFilterTop = 58;
constexpr int kPopupFilterBottom = 82;
constexpr int kPopupWidth = 320;
constexpr int kPopupHeight = 500;
constexpr int kPopupListTop = 108;
constexpr int kPopupBottomPadding = 14;
constexpr int kPopupCardHeight = 92;
constexpr int kPopupCardGap = 8;
constexpr int kTrayOpen = 200;
constexpr int kTraySettings = 201;
constexpr int kTrayExit = 202;

struct Settings {
    bool winV = false;
    bool dark = false;
    bool pauseMonitoring = false;
    bool startWithWindows = false;
    bool encryptData = false;
    int maxItems = 1000;
    int retentionDays = 30;
    int maxDiskMb = 256;
    int language = -1; // -1 system, 0 English, 1 Simplified Chinese
    std::vector<std::string> categories{"General", "Work", "Code", "Links"};
};

struct AppState {
    HWND hidden = nullptr;
    HWND popup = nullptr;
    HWND searchEdit = nullptr;
    HWND settings = nullptr;
    HWND targetWindow = nullptr;
    HFONT popupFont = nullptr;
    HFONT settingsFont = nullptr;
    HBRUSH popupInputBrush = nullptr;
    HBRUSH settingsBackgroundBrush = nullptr;
    HBRUSH settingsCardBrush = nullptr;
    HBRUSH settingsInputBrush = nullptr;
    HHOOK keyboardHook = nullptr;
    bool winKeyDown = false;
    bool suppressWinV = false;
    bool suppressWinKey = false;
    WNDPROC oldEditProc = nullptr;
    POINT popupPoint{};
    int selected = 0;
    int scrollOffset = 0;
    int filterScrollOffset = 0;
    bool filterDragging = false;
    int filterDragStartX = 0;
    int filterDragStartOffset = 0;
    int hoveredRow = -1;
    int filterType = 0;
    bool pinnedOnly = false;
    std::string query;
    Settings settingsData;
    ClipStore store;
    std::vector<std::size_t> visible;
    std::uint64_t ignoredClipboardHash = 0;
};

AppState* g_app = nullptr;
UINT g_taskbarCreated = 0;
UINT g_uiDpi = 96;

int ui(int value) {
    return MulDiv(value, static_cast<int>(g_uiDpi), 96);
}

UINT monitorDpi(HMONITOR monitor) {
    UINT dpiX = 96;
    UINT dpiY = 96;
    if (monitor && SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        return dpiX;
    }
    const UINT systemDpi = GetDpiForSystem();
    return systemDpi == 0 ? 96 : systemDpi;
}

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

std::uint64_t nowUnix() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

std::wstring formatRelativeTime(std::uint64_t timestamp) {
    if (timestamp == 0) return {};
    const std::uint64_t current = nowUnix();
    const std::uint64_t elapsed = current > timestamp ? current - timestamp : 0;
    if (elapsed < 60) return tr(L"Just now", L"刚刚");
    if (elapsed < 3600) return std::to_wstring(elapsed / 60) + tr(L" min ago", L"分钟前");
    if (elapsed < 86400) return std::to_wstring(elapsed / 3600) + tr(L" hr ago", L"小时前");
    return std::to_wstring(elapsed / 86400) + tr(L" d ago", L"天前");
}

void loadSettings(Settings& settings) {
    std::FILE* file = nullptr;
    _wfopen_s(&file, settingsPath().c_str(), L"rb");
    if (!file) return;
    char line[128]{};
    while (std::fgets(line, sizeof(line), file)) {
        if (std::strncmp(line, "winV=1", 6) == 0) settings.winV = true;
        if (std::strncmp(line, "dark=1", 6) == 0) settings.dark = true;
        if (std::strncmp(line, "pauseMonitoring=1", 17) == 0) settings.pauseMonitoring = true;
        if (std::strncmp(line, "startWithWindows=1", 18) == 0) settings.startWithWindows = true;
        if (std::strncmp(line, "encryptData=1", 13) == 0) settings.encryptData = true;
        if (std::strncmp(line, "maxItems=", 9) == 0) settings.maxItems = std::clamp(std::atoi(line + 9), 0, 100000);
        if (std::strncmp(line, "retentionDays=", 14) == 0) settings.retentionDays = std::clamp(std::atoi(line + 14), 0, 36500);
        if (std::strncmp(line, "maxDiskMb=", 10) == 0) settings.maxDiskMb = std::clamp(std::atoi(line + 10), 0, 102400);
        for (int i = 0; i < 4; ++i) {
            const std::string prefix = "category" + std::to_string(i) + "=";
            if (std::strncmp(line, prefix.c_str(), prefix.size()) == 0) {
                settings.categories[static_cast<std::size_t>(i)] = line + prefix.size();
                while (!settings.categories[static_cast<std::size_t>(i)].empty() &&
                       (settings.categories[static_cast<std::size_t>(i)].back() == '\r' ||
                        settings.categories[static_cast<std::size_t>(i)].back() == '\n')) {
                    settings.categories[static_cast<std::size_t>(i)].pop_back();
                }
            }
        }
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
    std::fprintf(file, "winV=%d\ndark=%d\npauseMonitoring=%d\nstartWithWindows=%d\n"
                      "encryptData=%d\n"
                      "maxItems=%d\nretentionDays=%d\nmaxDiskMb=%d\nlanguage=%d\n",
                 settings.winV ? 1 : 0, settings.dark ? 1 : 0,
                 settings.pauseMonitoring ? 1 : 0, settings.startWithWindows ? 1 : 0,
                 settings.encryptData ? 1 : 0,
                 settings.maxItems,
                 settings.retentionDays, settings.maxDiskMb, settings.language);
    for (int i = 0; i < 4; ++i) {
        std::fprintf(file, "category%d=%s\n", i, settings.categories[static_cast<std::size_t>(i)].c_str());
    }
    std::fclose(file);
}

void updateStartupRegistration(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    constexpr wchar_t valueName[] = L"ClipLite";
    if (enabled) {
        wchar_t executable[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            std::wstring command = L"\"";
            command += executable;
            command += L"\"";
            RegSetValueExW(key, valueName, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(command.c_str()),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        RegDeleteValueW(key, valueName);
    }
    RegCloseKey(key);
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

std::string clipboardSource() {
    const HWND owner = GetClipboardOwner();
    if (!owner) return {};
    DWORD processId = 0;
    GetWindowThreadProcessId(owner, &processId);
    if (processId == 0) return {};

    std::wstring executablePath;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process) {
        wchar_t buffer[1024]{};
        DWORD length = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
        if (QueryFullProcessImageNameW(process, 0, buffer, &length)) {
            executablePath.assign(buffer, length);
        }
        CloseHandle(process);
    }
    const std::size_t separator = executablePath.find_last_of(L"\\/");
    const std::wstring executable = separator == std::wstring::npos
        ? executablePath : executablePath.substr(separator + 1);
    const std::size_t extension = executable.find_last_of(L'.');
    const std::wstring stem = extension == std::wstring::npos
        ? executable : executable.substr(0, extension);
    if (_wcsicmp(stem.c_str(), L"chrome") == 0) return "Chrome";
    if (_wcsicmp(stem.c_str(), L"msedge") == 0) return "Edge";
    if (_wcsicmp(stem.c_str(), L"firefox") == 0) return "Firefox";
    if (_wcsicmp(stem.c_str(), L"code") == 0) return "VS Code";
    if (_wcsicmp(stem.c_str(), L"devenv") == 0) return "Visual Studio";
    if (_wcsicmp(stem.c_str(), L"wechat") == 0 || _wcsicmp(stem.c_str(), L"weixin") == 0) return "WeChat";
    if (_wcsicmp(stem.c_str(), L"winword") == 0) return "Word";
    if (_wcsicmp(stem.c_str(), L"excel") == 0) return "Excel";
    if (_wcsicmp(stem.c_str(), L"powerpnt") == 0) return "PowerPoint";
    if (_wcsicmp(stem.c_str(), L"photoshop") == 0) return "Photoshop";
    if (_wcsicmp(stem.c_str(), L"explorer") == 0) return "File Explorer";
    if (!stem.empty()) return wideToUtf8(stem.c_str(), stem.size());

    wchar_t title[256]{};
    const int length = GetWindowTextW(owner, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
    return length > 0 ? wideToUtf8(title, static_cast<std::size_t>(length)) : std::string{};
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

bool isValidDibPayload(const std::string& payload) {
    if (payload.size() < sizeof(BITMAPINFOHEADER)) return false;
    BITMAPINFOHEADER header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > payload.size() ||
        header.biWidth <= 0 || header.biHeight == 0 || header.biPlanes != 1 ||
        header.biBitCount == 0 || header.biBitCount > 32 ||
        header.biWidth > 10000 || header.biHeight < -10000 || header.biHeight > 10000) {
        return false;
    }
    std::size_t colorEntries = header.biBitCount <= 8
        ? (std::size_t{1} << header.biBitCount) : 0;
    if (header.biCompression == BI_BITFIELDS && header.biSize == sizeof(BITMAPINFOHEADER)) {
        colorEntries = 3;
    }
    const std::size_t bitsOffset = header.biSize + colorEntries * sizeof(RGBQUAD);
    return bitsOffset < payload.size();
}

bool captureClipboard(ClipType& type, std::string& payload, std::string& source) {
    source = clipboardSource();
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
                if (isValidDibPayload(payload)) {
                    type = ClipType::ImageV5;
                    CloseClipboard();
                    return true;
                }
                payload.clear();
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
                if (isValidDibPayload(payload)) {
                    type = ClipType::Image;
                    CloseClipboard();
                    return true;
                }
                payload.clear();
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

bool isImageType(ClipType type) {
    return type == ClipType::Image || type == ClipType::ImageV5;
}

bool isAutomaticTypeMatch(int filterType, ClipType type) {
    if (filterType == 0) return true;
    if (filterType == 1) return type == ClipType::Text;
    if (filterType == 2) return isImageType(type);
    if (filterType == 3) return type == ClipType::Files;
    if (filterType == 4) return type == ClipType::Html;
    return type != ClipType::Text && !isImageType(type) &&
           type != ClipType::Files && type != ClipType::Html;
}

const wchar_t* automaticTypeLabel(ClipType type) {
    if (type == ClipType::Text) return tr(L"Text", L"文本");
    if (isImageType(type)) return tr(L"Image", L"图片");
    if (type == ClipType::Files) return tr(L"Files", L"文件");
    if (type == ClipType::Html) return tr(L"Rich text", L"富文本");
    return tr(L"Other", L"其他");
}

int popupVisibleRows() {
    if (!g_app || !g_app->popup) return 1;
    RECT client{};
    GetClientRect(g_app->popup, &client);
    const int available = client.bottom - ui(kPopupListTop) - ui(kPopupBottomPadding);
    return std::max(1, (available + ui(kPopupCardGap)) /
                           (ui(kPopupCardHeight) + ui(kPopupCardGap)));
}

int popupCardHeight(const ClipItem& item) {
    (void)item;
    return ui(kPopupCardHeight);
}

int popupRowAt(int clickY) {
    if (!g_app || !g_app->popup) return -1;
    RECT client{};
    GetClientRect(g_app->popup, &client);
    if (clickY < ui(kPopupListTop) || clickY >= client.bottom - ui(kPopupBottomPadding)) return -1;
    int y = ui(kPopupListTop);
    for (int row = g_app->scrollOffset;
         row < static_cast<int>(g_app->visible.size()); ++row) {
        const ClipItem& item = g_app->store.items()[g_app->visible[static_cast<std::size_t>(row)]];
        const int height = popupCardHeight(item);
        if (clickY >= y && clickY < y + height) return row;
        y += height + ui(kPopupCardGap);
        if (y >= client.bottom) break;
    }
    return -1;
}

int popupRowTop(int row) {
    if (!g_app || row < g_app->scrollOffset) return -1;
    int y = ui(kPopupListTop);
    for (int index = g_app->scrollOffset; index < row; ++index) {
        const ClipItem& item = g_app->store.items()[g_app->visible[static_cast<std::size_t>(index)]];
        y += popupCardHeight(item) + ui(kPopupCardGap);
    }
    return y;
}

void drawPinIcon(HDC dc, int x, int y, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, ui(1), color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    POINT head[] = {{x + ui(2), y + ui(2)}, {x + ui(10), y + ui(2)},
                    {x + ui(8), y + ui(8)}, {x + ui(4), y + ui(8)}};
    Polygon(dc, head, 4);
    MoveToEx(dc, x + ui(6), y + ui(8), nullptr);
    LineTo(dc, x + ui(2), y + ui(14));
    MoveToEx(dc, x + ui(4), y + ui(8), nullptr);
    LineTo(dc, x + ui(9), y + ui(8));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawEmptyClipboardIcon(HDC dc, int centerX, int top, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, ui(1), color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, centerX - ui(14), top + ui(6), centerX + ui(14), top + ui(38), ui(4), ui(4));
    RoundRect(dc, centerX - ui(7), top, centerX + ui(7), top + ui(10), ui(3), ui(3));
    MoveToEx(dc, centerX - ui(7), top + ui(20), nullptr);
    LineTo(dc, centerX + ui(7), top + ui(20));
    MoveToEx(dc, centerX - ui(7), top + ui(27), nullptr);
    LineTo(dc, centerX + ui(3), top + ui(27));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawDeleteIcon(HDC dc, int x, int y, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, ui(1), color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, x, y, nullptr);
    LineTo(dc, x + ui(10), y + ui(10));
    MoveToEx(dc, x + ui(10), y, nullptr);
    LineTo(dc, x, y + ui(10));
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void drawMetadataTag(HDC dc, const RECT& rect, const std::wstring& value,
                     COLORREF background, COLORREF border, COLORREF text, int radius) {
    HBRUSH brush = CreateSolidBrush(background);
    HPEN pen = CreatePen(PS_SOLID, ui(1), border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, ui(radius), ui(radius));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    SetTextColor(dc, text);
    DrawTextW(dc, value.c_str(), -1, const_cast<RECT*>(&rect),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void refreshVisible() {
    if (!g_app->popup) return;
    const std::vector<std::size_t> candidates = g_app->store.search(g_app->query);
    g_app->visible.clear();
    for (const std::size_t index : candidates) {
        const ClipItem& item = g_app->store.items()[index];
        const bool typeMatches = isAutomaticTypeMatch(g_app->filterType, item.type);
        if (typeMatches && (!g_app->pinnedOnly || item.pinned)) {
            g_app->visible.push_back(index);
        }
    }
    if (g_app->visible.empty()) g_app->selected = 0;
    else g_app->selected = std::clamp(g_app->selected, 0, static_cast<int>(g_app->visible.size()) - 1);
    const int visibleRows = popupVisibleRows();
    const int maxOffset = std::max(0, static_cast<int>(g_app->visible.size()) - visibleRows);
    g_app->scrollOffset = std::clamp(g_app->scrollOffset, 0, maxOffset);
    if (g_app->selected < g_app->scrollOffset) g_app->scrollOffset = g_app->selected;
    if (g_app->selected >= g_app->scrollOffset + visibleRows) {
        g_app->scrollOffset = g_app->selected - visibleRows + 1;
    }
    InvalidateRect(g_app->popup, nullptr, FALSE);
}

void scrollPopup(int delta) {
    if (!g_app->popup || g_app->visible.empty()) return;
    const int maxOffset = std::max(0, static_cast<int>(g_app->visible.size()) - popupVisibleRows());
    const int nextOffset = std::clamp(g_app->scrollOffset + delta, 0, maxOffset);
    if (nextOffset == g_app->scrollOffset) return;
    g_app->scrollOffset = nextOffset;
    InvalidateRect(g_app->popup, nullptr, FALSE);
}

void notifyPasteFailure() {
    MessageBeep(MB_ICONWARNING);
}

bool drawImagePreview(HDC dc, const ClipItem& item, const RECT& rowRect) {
    std::string payload;
    if (!g_app->store.readPayload(&item - g_app->store.items().data(), payload)) return false;
    if (payload.size() < sizeof(BITMAPINFOHEADER)) return false;

    BITMAPINFOHEADER header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > payload.size() ||
        header.biWidth <= 0 || header.biHeight == 0 || header.biPlanes != 1 ||
        header.biBitCount == 0 || header.biBitCount > 32 ||
        header.biWidth > 10000 || header.biHeight < -10000 || header.biHeight > 10000) {
        return false;
    }
    std::size_t colorEntries = 0;
    if (header.biBitCount <= 8) colorEntries = std::size_t{1} << header.biBitCount;
    if (header.biCompression == BI_BITFIELDS && header.biSize == sizeof(BITMAPINFOHEADER)) {
        colorEntries = 3;
    }
    const std::size_t bitsOffset = header.biSize + colorEntries * sizeof(RGBQUAD);
    if (bitsOffset >= payload.size()) return false;

    const auto* bitmapInfo = reinterpret_cast<const BITMAPINFO*>(payload.data());
    const int width = rowRect.right - rowRect.left;
    const int height = rowRect.bottom - rowRect.top;
    const int result = StretchDIBits(dc, rowRect.left, rowRect.top, width, height,
                                     0, 0, header.biWidth, std::abs(header.biHeight),
                                     payload.data() + bitsOffset, bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
    return result != GDI_ERROR;
}

void sendPaste() {
    if (!g_app->popup || g_app->visible.empty()) return;
    const int selected = std::clamp(g_app->selected, 0, static_cast<int>(g_app->visible.size()) - 1);
    const std::size_t index = g_app->visible[static_cast<std::size_t>(selected)];
    std::string payload;
    if (!g_app->store.readPayload(index, payload)) {
        notifyPasteFailure();
        return;
    }
    const std::uint64_t hash = g_app->store.items()[index].hash;
    if (!setClipboardDataForItem(g_app->store.items()[index], payload)) {
        notifyPasteFailure();
        return;
    }
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
    const HMONITOR monitor = MonitorFromPoint(g_app->popupPoint, MONITOR_DEFAULTTONEAREST);
    g_uiDpi = monitorDpi(monitor);
    g_app->query.clear();
    g_app->selected = 0;
    g_app->scrollOffset = 0;
    g_app->filterScrollOffset = 0;
    g_app->filterType = 0;
    g_app->pinnedOnly = false;
    g_app->visible = g_app->store.search({});

    const int width = ui(kPopupWidth);
    int height = ui(kPopupHeight);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    height = std::min(height, static_cast<int>(info.rcWork.bottom - info.rcWork.top - 20));
    int x = g_app->popupPoint.x + 12;
    int y = g_app->popupPoint.y + 12;
    if (x + width > info.rcWork.right) x = info.rcWork.right - width;
    if (y + height > info.rcWork.bottom) y = info.rcWork.bottom - height;
    x = std::max(x, static_cast<int>(info.rcWork.left));
    y = std::max(y, static_cast<int>(info.rcWork.top));
    g_app->popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"ClipLitePopup",
                                   L"ClipLite", WS_POPUP, x, y, width, height,
                                   nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_app->popup) return;
    SetWindowRgn(g_app->popup, CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                                  ui(8), ui(8)), TRUE);
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
        const bool winKey = key->vkCode == VK_LWIN || key->vkCode == VK_RWIN;
        if (!injected && winKey) {
            if (keyUp && g_app->suppressWinKey) {
                g_app->suppressWinKey = false;
                g_app->winKeyDown = false;
                return 1;
            }
            g_app->winKeyDown = keyDown || (g_app->winKeyDown && !keyUp);
        }
        if (!injected && key->vkCode == 'V') {
            if (keyDown && g_app->winKeyDown && !g_app->suppressWinV) {
                g_app->suppressWinV = true;
                g_app->suppressWinKey = true;
                PostMessageW(g_app->hidden, kShowPopupMessage, 0, 0);
                return 1;
            }
            if (keyUp && g_app->suppressWinV) {
                g_app->suppressWinV = false;
                return 1;
            }
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
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        const LRESULT result = CallWindowProcW(g_app->oldEditProc, hwnd, message, wParam, lParam);
        InvalidateRect(hwnd, nullptr, TRUE);
        if (g_app->popup) InvalidateRect(g_app->popup, nullptr, FALSE);
        return result;
    }
    if (message == WM_PAINT) {
        const LRESULT result = CallWindowProcW(g_app->oldEditProc, hwnd, message, wParam, lParam);
        if (GetWindowTextLengthW(hwnd) == 0 && GetFocus() != hwnd) {
            HDC dc = GetDC(hwnd);
            if (dc) {
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, g_app->settingsData.dark ? RGB(150, 158, 168) : RGB(160, 160, 160));
                RECT client{};
                GetClientRect(hwnd, &client);
                client.left = ui(2);
                client.top = ui(2);
                client.bottom -= ui(2);
                DrawTextW(dc, tr(L"Search content...", L"搜索内容..."), -1, &client,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                ReleaseDC(hwnd, dc);
            }
        }
        return result;
    }
    if (message == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            sendPaste();
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            closePopup();
            return 0;
        }
        if (wParam == VK_DOWN || wParam == VK_UP || wParam == VK_TAB) {
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

int automaticFilterCommand(int slot) {
    if (slot == 0) return kFilterAll;
    if (slot == 1) return kFilterPinned;
    if (slot == 2) return kFilterText;
    if (slot == 3) return kFilterImage;
    if (slot == 4) return kFilterFiles;
    if (slot == 5) return kFilterHtml;
    return kFilterOther;
}

RECT automaticFilterRect(int slot, int scrollOffset = 0) {
    static const int widths[] = {44, 44, 44, 46, 56, 44, 44};
    int left = ui(16);
    for (int i = 0; i < slot; ++i) left += ui(widths[i] + 6);
    left -= scrollOffset;
    return RECT{left, ui(kPopupFilterTop), left + ui(widths[slot]), ui(kPopupFilterBottom)};
}

int filterContentWidth() {
    static const int widths[] = {44, 44, 44, 46, 56, 44, 44};
    int width = 0;
    for (const int value : widths) width += ui(value + 6);
    return width - ui(6);
}

void clampFilterScroll() {
    if (!g_app || !g_app->popup) return;
    RECT client{};
    GetClientRect(g_app->popup, &client);
    const int viewportWidth = client.right - ui(32);
    const int maxOffset = std::max(0, filterContentWidth() - viewportWidth);
    g_app->filterScrollOffset = std::clamp(g_app->filterScrollOffset, 0, maxOffset);
}

void invalidateFilterBar(HWND hwnd) {
    RECT filterRect{};
    GetClientRect(hwnd, &filterRect);
    filterRect.top = ui(kPopupFilterTop - 2);
    filterRect.bottom = ui(kPopupFilterBottom + 2);
    InvalidateRect(hwnd, &filterRect, FALSE);
}

bool isAutomaticFilterActive(int slot) {
    const int command = automaticFilterCommand(slot);
    if (command == kFilterPinned) return g_app->pinnedOnly;
    if (command == kFilterAll) return g_app->filterType == 0 && !g_app->pinnedOnly;
    if (command == kFilterOther) return g_app->filterType == 5 && !g_app->pinnedOnly;
    return g_app->filterType == command - kFilterText + 1 && !g_app->pinnedOnly;
}

void paintSettings(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const bool dark = g_app->settingsData.dark;
    const COLORREF background = dark ? RGB(25, 29, 34) : RGB(239, 246, 244);
    const COLORREF card = dark ? RGB(34, 39, 46) : RGB(250, 253, 252);
    const COLORREF border = dark ? RGB(55, 62, 70) : RGB(225, 234, 231);
    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(dc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);
    HBRUSH cardBrush = CreateSolidBrush(card);
    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, cardBrush);
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    RoundRect(dc, 12, 8, client.right - 12, 166, 16, 16);
    RoundRect(dc, 12, 182, client.right - 12, 382, 16, 16);
    RoundRect(dc, 12, 398, client.right - 12, 596, 16, 16);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(cardBrush);
}

void paintPopupContent(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const bool dark = g_app->settingsData.dark;
    const COLORREF background = dark ? RGB(38, 42, 48) : RGB(242, 244, 247);
    const COLORREF text = dark ? RGB(240, 243, 247) : RGB(26, 26, 26);
    const COLORREF secondary = dark ? RGB(175, 183, 193) : RGB(102, 102, 102);
    const COLORREF border = dark ? RGB(75, 83, 92) : RGB(209, 213, 219);
    const COLORREF card = dark ? RGB(48, 53, 60) : RGB(255, 255, 255);
    const COLORREF cardBorder = dark ? RGB(67, 74, 82) : RGB(234, 234, 234);
    const COLORREF chip = dark ? RGB(55, 61, 69) : RGB(255, 255, 255);
    const COLORREF accent = dark ? RGB(53, 137, 107) : RGB(39, 124, 97);
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(dc, &client, brush);
    DeleteObject(brush);
    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBorderPen = SelectObject(dc, borderPen);
    HGDIOBJ oldBorderBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, 0, 0, client.right, client.bottom, ui(8), ui(8));
    SelectObject(dc, oldBorderBrush);
    SelectObject(dc, oldBorderPen);
    DeleteObject(borderPen);

    SetBkMode(dc, TRANSPARENT);
    HFONT titleFont = CreateFontW(-ui(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    HFONT filterFont = CreateFontW(-ui(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    HFONT previewFont = CreateFontW(-ui(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    HFONT metaFont = CreateFontW(-ui(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    HGDIOBJ old = SelectObject(dc, titleFont);
    SetTextColor(dc, text);
    RECT titleRect{ui(16), ui(14), ui(82), ui(46)};
    DrawTextW(dc, tr(L"Clipboard history", L"剪贴板历史"), -1, &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    HBRUSH searchBrush = CreateSolidBrush(dark ? RGB(48, 53, 60) : RGB(255, 255, 255));
    const COLORREF searchBorder = g_app->searchEdit && GetFocus() == g_app->searchEdit
        ? accent : border;
    HPEN searchPen = CreatePen(PS_SOLID, ui(1), searchBorder);
    HGDIOBJ oldSearchBrush = SelectObject(dc, searchBrush);
    HGDIOBJ oldSearchPen = SelectObject(dc, searchPen);
    RoundRect(dc, ui(90), ui(14), ui(266), ui(46), ui(4), ui(4));
    SelectObject(dc, oldSearchPen);
    SelectObject(dc, oldSearchBrush);
    DeleteObject(searchPen);
    DeleteObject(searchBrush);
    const wchar_t* clearLabel = tr(L"Clear", L"清空");
    SelectObject(dc, filterFont);
    SetTextColor(dc, secondary);
    RECT clearRect{ui(270), ui(14), client.right - ui(16), ui(46)};
    DrawTextW(dc, clearLabel, -1, &clearRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const wchar_t* filterLabels[] = {
        tr(L"All", L"全部"), tr(L"Pinned", L"置顶"), tr(L"Text", L"文本"),
        tr(L"Images", L"图片"), tr(L"Files", L"文件"), tr(L"Rich", L"富文本"),
        tr(L"Other", L"其他")
    };
    SelectObject(dc, filterFont);
    const int filterClip = SaveDC(dc);
    IntersectClipRect(dc, ui(16), ui(kPopupFilterTop),
                      client.right - ui(16), ui(kPopupFilterBottom));
    for (int slot = 0; slot < 7; ++slot) {
        const RECT chipRect = automaticFilterRect(slot, g_app->filterScrollOffset);
        const bool active = isAutomaticFilterActive(slot);
        HBRUSH chipBrush = CreateSolidBrush(active ? accent : chip);
        HPEN chipPen = CreatePen(PS_SOLID, 1, active ? accent : border);
        HGDIOBJ oldChipBrush = SelectObject(dc, chipBrush);
        HGDIOBJ oldChipPen = SelectObject(dc, chipPen);
        RoundRect(dc, chipRect.left, chipRect.top, chipRect.right, chipRect.bottom, ui(4), ui(4));
        SelectObject(dc, oldChipPen);
        SelectObject(dc, oldChipBrush);
        DeleteObject(chipPen);
        DeleteObject(chipBrush);
        SetTextColor(dc, active ? RGB(255, 255, 255) : secondary);
        DrawTextW(dc, filterLabels[slot], -1, const_cast<RECT*>(&chipRect),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    RestoreDC(dc, filterClip);

    const int listBottom = client.bottom - ui(kPopupBottomPadding);
    int y = ui(kPopupListTop);
    for (int row = g_app->scrollOffset;
         row < static_cast<int>(g_app->visible.size()); ++row) {
        const ClipItem& item = g_app->store.items()[g_app->visible[static_cast<std::size_t>(row)]];
        const int height = popupCardHeight(item);
        if (y >= listBottom) break;
        RECT rowRect{ui(16), y, client.right - ui(16), y + height};
        HBRUSH cardBrush = CreateSolidBrush(card);
        HPEN cardPen = CreatePen(PS_SOLID, 1, cardBorder);
        HGDIOBJ oldCardBrush = SelectObject(dc, cardBrush);
        HGDIOBJ oldCardPen = SelectObject(dc, cardPen);
        RoundRect(dc, rowRect.left, rowRect.top, rowRect.right, rowRect.bottom, ui(6), ui(6));
        SelectObject(dc, oldCardPen);
        SelectObject(dc, oldCardBrush);
        DeleteObject(cardPen);
        DeleteObject(cardBrush);
        const bool image = isImageType(item.type);
            if (image && drawImagePreview(dc, item, RECT{rowRect.left + ui(12), y + ui(12),
                                                     rowRect.left + ui(56), y + ui(56)})) {
        } else {
            std::wstring preview = utf8ToWide(item.preview);
            if (preview.empty()) preview = L"[Empty]";
            SelectObject(dc, previewFont);
            SetTextColor(dc, text);
            RECT previewRect{rowRect.left + ui(12), y + ui(10), rowRect.right - ui(12), y + ui(48)};
            DrawTextW(dc, preview.c_str(), -1, &previewRect,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }
        SelectObject(dc, metaFont);
        const wchar_t* kind = automaticTypeLabel(item.type);
        SetTextColor(dc, secondary);
        const std::wstring source = item.source.empty()
            ? std::wstring(tr(L"Clipboard", L"剪贴板")) : utf8ToWide(item.source);
        const int metadataTop = y + height - ui(24);
        RECT sourceRect{rowRect.left + ui(12), metadataTop,
                        rowRect.left + ui(84), metadataTop + ui(16)};
        drawMetadataTag(dc, sourceRect, source, RGB(240, 244, 248), RGB(229, 231, 235),
                        RGB(75, 85, 99), 3);
        RECT typeRect{rowRect.left + ui(92), metadataTop,
                      rowRect.left + ui(140), metadataTop + ui(16)};
        drawMetadataTag(dc, typeRect, kind, RGB(239, 241, 243), RGB(239, 241, 243),
                        RGB(85, 85, 85), 2);
        const std::wstring time = formatRelativeTime(item.timestamp);
        RECT timeRect{rowRect.right - ui(92), metadataTop,
                      rowRect.right - ui(44), metadataTop + ui(16)};
        SetTextColor(dc, RGB(136, 136, 136));
        DrawTextW(dc, time.c_str(), -1, &timeRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        RECT pinRect{rowRect.right - ui(38), metadataTop - ui(2),
                     rowRect.right - ui(12), metadataTop + ui(18)};
        if (item.pinned) {
            drawMetadataTag(dc, pinRect, L"", RGB(255, 243, 224), RGB(255, 243, 224),
                            RGB(245, 158, 11), 4);
            drawPinIcon(dc, rowRect.right - ui(31), metadataTop, RGB(245, 158, 11));
        } else {
            drawPinIcon(dc, rowRect.right - ui(31), metadataTop, RGB(176, 176, 176));
        }
        if (row == g_app->hoveredRow) {
            drawDeleteIcon(dc, rowRect.right - ui(20), y + ui(9), RGB(176, 176, 176));
        }
        y += height + ui(kPopupCardGap);
    }
    if (g_app->visible.empty()) {
        const wchar_t* empty = tr(L"No clipboard history", L"暂无剪贴板记录");
        SetTextColor(dc, secondary);
        const int centerX = client.right / 2;
        const int emptyTop = ui(kPopupListTop + 54);
        drawEmptyClipboardIcon(dc, centerX, emptyTop,
                               dark ? RGB(115, 126, 138) : RGB(178, 187, 194));
        RECT emptyRect{ui(24), emptyTop + ui(48), client.right - ui(24), emptyTop + ui(72)};
        DrawTextW(dc, empty, -1, &emptyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (g_app->visible.size() > static_cast<std::size_t>(popupVisibleRows())) {
        const int trackTop = ui(kPopupListTop);
        const int trackBottom = listBottom;
        const int trackHeight = trackBottom - trackTop;
        const int thumbHeight = std::max(ui(24), trackHeight * popupVisibleRows() /
                                                   static_cast<int>(g_app->visible.size()));
        const int maxOffset = static_cast<int>(g_app->visible.size()) - popupVisibleRows();
        const int thumbTop = trackTop + (trackHeight - thumbHeight) * g_app->scrollOffset /
                                       std::max(1, maxOffset);
        HBRUSH scrollBrush = CreateSolidBrush(dark ? RGB(103, 113, 124) : RGB(192, 192, 192));
        HGDIOBJ oldScrollBrush = SelectObject(dc, scrollBrush);
        RoundRect(dc, client.right - ui(5), thumbTop, client.right - ui(2),
                  thumbTop + thumbHeight, ui(3), ui(3));
        SelectObject(dc, oldScrollBrush);
        DeleteObject(scrollBrush);
    }
    SelectObject(dc, old);
    DeleteObject(titleFont);
    DeleteObject(filterFont);
    DeleteObject(previewFont);
    DeleteObject(metaFont);
}

void paintPopup(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);
    paintPopupContent(hwnd, buffer);
    BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
}

bool settingsToggleValue(HWND hwnd);
void setSettingsToggleValue(HWND hwnd, bool enabled);

void createSettingsControls(HWND hwnd) {
    const bool zh = languageIsChinese();
    CreateWindowW(L"STATIC", zh ? L"ClipLite 设置" : L"ClipLite Settings",
                  WS_CHILD | WS_VISIBLE, 20, 18, 420, 28, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    HWND win = CreateWindowW(L"BUTTON", zh ? L"强制替换 Win+V" : L"Force replace Win+V",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_OWNERDRAW,
                              20, 62, 400, 30,
                             hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingWinV)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(win, BM_SETCHECK, g_app->settingsData.winV ? BST_CHECKED : BST_UNCHECKED, 0);
    setSettingsToggleValue(win, g_app->settingsData.winV);
    HWND dark = CreateWindowW(L"BUTTON", zh ? L"深色主题" : L"Dark theme",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_OWNERDRAW,
                               20, 94, 400, 30,
                              hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingDark)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(dark, BM_SETCHECK, g_app->settingsData.dark ? BST_CHECKED : BST_UNCHECKED, 0);
    setSettingsToggleValue(dark, g_app->settingsData.dark);
    CreateWindowW(L"STATIC", zh ? L"语言" : L"Language", WS_CHILD | WS_VISIBLE,
                  20, 132, 80, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    HWND language = CreateWindowW(L"COMBOBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST |
                                      CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                                  100, 128, 180, 120, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingLanguage)),
                                  GetModuleHandleW(nullptr), nullptr);
    SendMessageW(language, CB_SETMINVISIBLE, 3, 0);
    SendMessageW(language, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 26);
    SendMessageW(language, CB_SETITEMHEIGHT, 0, 24);
    SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(zh ? L"自动" : L"Auto"));
    SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
    SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"简体中文"));
    const int languageSelection = g_app->settingsData.language < 0
        ? 0 : g_app->settingsData.language + 1;
    SendMessageW(language, CB_SETCURSEL, languageSelection, 0);
    CreateWindowW(L"STATIC", zh ? L"数据保留" : L"Data retention",
                  WS_CHILD | WS_VISIBLE, 20, 176, 180, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", zh ? L"最大记录数" : L"Maximum records",
                  WS_CHILD | WS_VISIBLE, 20, 208, 220, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    wchar_t value[32]{};
    swprintf_s(value, L"%d", g_app->settingsData.maxItems);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value, WS_CHILD | WS_VISIBLE | ES_NUMBER,
                    250, 204, 150, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingMaxItems)),
                    GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", zh ? L"保留天数（0 = 永久）" : L"Retention days (0 = forever)",
                  WS_CHILD | WS_VISIBLE, 20, 242, 220, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    swprintf_s(value, L"%d", g_app->settingsData.retentionDays);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value, WS_CHILD | WS_VISIBLE | ES_NUMBER,
                    250, 238, 150, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingRetentionDays)),
                    GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", zh ? L"最大磁盘空间（MB）" : L"Max disk space (MB)",
                  WS_CHILD | WS_VISIBLE, 20, 276, 220, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    swprintf_s(value, L"%d", g_app->settingsData.maxDiskMb);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value, WS_CHILD | WS_VISIBLE | ES_NUMBER,
                    250, 272, 150, 26, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingMaxDiskMb)),
                    GetModuleHandleW(nullptr), nullptr);
    HWND pause = CreateWindowW(L"BUTTON", zh ? L"暂停剪贴板监听" : L"Pause clipboard monitoring",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_OWNERDRAW,
                               20, 308, 400, 30, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingPause)),
                               GetModuleHandleW(nullptr), nullptr);
    SendMessageW(pause, BM_SETCHECK, g_app->settingsData.pauseMonitoring ? BST_CHECKED : BST_UNCHECKED, 0);
    setSettingsToggleValue(pause, g_app->settingsData.pauseMonitoring);
    HWND startup = CreateWindowW(L"BUTTON", zh ? L"随 Windows 启动" : L"Start with Windows",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_OWNERDRAW,
                                  20, 334, 400, 30, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingStartup)),
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(startup, BM_SETCHECK, g_app->settingsData.startWithWindows ? BST_CHECKED : BST_UNCHECKED, 0);
    setSettingsToggleValue(startup, g_app->settingsData.startWithWindows);
    HWND encrypt = CreateWindowW(L"BUTTON", zh ? L"使用 Windows 用户加密保护历史" : L"Protect history with Windows encryption",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_OWNERDRAW,
                                  20, 360, 400, 30, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingEncrypt)),
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(encrypt, BM_SETCHECK, g_app->settingsData.encryptData ? BST_CHECKED : BST_UNCHECKED, 0);
    setSettingsToggleValue(encrypt, g_app->settingsData.encryptData);
    CreateWindowW(L"STATIC", zh ? L"当前历史" : L"Current history",
                  WS_CHILD | WS_VISIBLE, 20, 390, 100, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    wchar_t count[128]{};
    swprintf_s(count, zh ? L"%zu 条记录，%llu 字节" : L"%zu records, %llu bytes",
               g_app->store.activeCount(), g_app->store.diskBytes());
    CreateWindowW(L"STATIC", count, WS_CHILD | WS_VISIBLE, 120, 390, 250, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    std::size_t textCount = 0, htmlCount = 0, imageCount = 0, fileCount = 0, otherCount = 0;
    std::size_t pinnedCount = 0;
    for (const ClipItem& item : g_app->store.items()) {
        if (item.type == ClipType::Text) ++textCount;
        else if (item.type == ClipType::Html) ++htmlCount;
        else if (item.type == ClipType::Files) ++fileCount;
        else if (isImageType(item.type)) ++imageCount;
        else ++otherCount;
        if (item.pinned) ++pinnedCount;
    }
    wchar_t details[256]{};
    swprintf_s(details, zh ? L"文本 %zu  图片 %zu  文件 %zu  HTML %zu  其他 %zu  置顶 %zu"
                           : L"Text %zu  Images %zu  Files %zu  HTML %zu  Other %zu  Pinned %zu",
               textCount, imageCount, fileCount, htmlCount, otherCount, pinnedCount);
    CreateWindowW(L"STATIC", details, WS_CHILD | WS_VISIBLE, 20, 412, 420, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"STATIC", zh ? L"自动分类" : L"Automatic categories",
                  WS_CHILD | WS_VISIBLE, 20, 458, 180, 24, hwnd, nullptr,
                  GetModuleHandleW(nullptr), nullptr);
    const wchar_t* automaticCategories[] = {
        zh ? L"文本" : L"Text", zh ? L"图片" : L"Images",
        zh ? L"文件" : L"Files", L"HTML", zh ? L"其他" : L"Other"
    };
    const std::size_t automaticCounts[] = {textCount, imageCount, fileCount, htmlCount, otherCount};
    for (int i = 0; i < 5; ++i) {
        wchar_t categoryValue[64]{};
        swprintf_s(categoryValue, L"%ls  %zu", automaticCategories[i], automaticCounts[i]);
        CreateWindowW(L"STATIC", categoryValue, WS_CHILD | WS_VISIBLE,
                      20 + (i % 2) * 200, 484 + (i / 2) * 32, 180, 24, hwnd, nullptr,
                      GetModuleHandleW(nullptr), nullptr);
    }
    CreateWindowW(L"BUTTON", zh ? L"清空历史" : L"Clear history",
                  WS_CHILD | WS_VISIBLE, 20, 630, 120, 28, hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingClear)), GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", zh ? L"保存" : L"Save",
                  WS_CHILD | WS_VISIBLE, 330, 630, 90, 28, hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingSave)), GetModuleHandleW(nullptr), nullptr);
}

void appendFilterMenu(HMENU menu) {
    HMENU filters = CreatePopupMenu();
    AppendMenuW(filters, MF_STRING, kFilterAll, tr(L"All", L"全部"));
    AppendMenuW(filters, MF_STRING, kFilterPinned, tr(L"Pinned only", L"仅置顶"));
    AppendMenuW(filters, MF_STRING, kFilterText, tr(L"Text", L"文本"));
    AppendMenuW(filters, MF_STRING, kFilterImage, tr(L"Images", L"图片"));
    AppendMenuW(filters, MF_STRING, kFilterFiles, tr(L"Files", L"文件"));
    AppendMenuW(filters, MF_STRING, kFilterHtml, tr(L"Rich text", L"富文本"));
    AppendMenuW(filters, MF_STRING, kFilterOther, tr(L"Other", L"其他"));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(filters),
                tr(L"Filter", L"筛选"));
}

void applyFilterCommand(int command) {
    if (command == kFilterAll) {
        g_app->filterType = 0;
        g_app->pinnedOnly = false;
    } else if (command >= kFilterText && command <= kFilterHtml) {
        g_app->filterType = command - kFilterText + 1;
        g_app->pinnedOnly = false;
    } else if (command == kFilterPinned) {
        g_app->filterType = 0;
        g_app->pinnedOnly = true;
    } else if (command == kFilterOther) {
        g_app->filterType = 5;
        g_app->pinnedOnly = false;
    }
}

void applyFontToChildren(HWND parent, HFONT font) {
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

bool isSettingsToggle(int id) {
    return id == kSettingWinV || id == kSettingDark || id == kSettingPause ||
           id == kSettingStartup || id == kSettingEncrypt;
}

bool settingsToggleValue(HWND hwnd) {
    return GetWindowLongPtrW(hwnd, GWLP_USERDATA) != 0;
}

void setSettingsToggleValue(HWND hwnd, bool enabled) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, enabled ? 1 : 0);
}

void drawSettingsToggle(const DRAWITEMSTRUCT& item) {
    const bool dark = g_app->settingsData.dark;
    const COLORREF card = dark ? RGB(34, 39, 46) : RGB(250, 253, 252);
    const COLORREF text = dark ? RGB(238, 241, 245) : RGB(30, 36, 46);
    const COLORREF accent = dark ? RGB(64, 157, 156) : RGB(63, 154, 153);
    const bool checked = settingsToggleValue(item.hwndItem);
    HBRUSH backgroundBrush = CreateSolidBrush(card);
    FillRect(item.hDC, &item.rcItem, backgroundBrush);
    DeleteObject(backgroundBrush);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    wchar_t label[256]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(sizeof(label) / sizeof(label[0])));
    RECT labelRect = item.rcItem;
    labelRect.right -= 64;
    DrawTextW(item.hDC, label, -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const int centerY = (item.rcItem.top + item.rcItem.bottom) / 2;
    RECT toggleRect{item.rcItem.right - 48, centerY - 10, item.rcItem.right - 8, centerY + 10};
    HBRUSH toggleBrush = CreateSolidBrush(checked ? accent : (dark ? RGB(77, 84, 93) : RGB(197, 208, 208)));
    HGDIOBJ oldBrush = SelectObject(item.hDC, toggleBrush);
    RoundRect(item.hDC, toggleRect.left, toggleRect.top, toggleRect.right, toggleRect.bottom, 20, 20);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(toggleBrush);
    HBRUSH knobBrush = CreateSolidBrush(RGB(255, 255, 255));
    oldBrush = SelectObject(item.hDC, knobBrush);
    const int knobX = checked ? toggleRect.right - 18 : toggleRect.left + 2;
    Ellipse(item.hDC, knobX, centerY - 8, knobX + 16, centerY + 8);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(knobBrush);
}

void drawSettingsLanguage(const DRAWITEMSTRUCT& item) {
    const bool dark = g_app->settingsData.dark;
    const COLORREF input = dark ? RGB(43, 47, 54) : RGB(255, 255, 255);
    const COLORREF selected = dark ? RGB(48, 84, 86) : RGB(224, 243, 241);
    const COLORREF text = dark ? RGB(238, 241, 245) : RGB(30, 36, 46);
    HBRUSH brush = CreateSolidBrush((item.itemState & ODS_SELECTED) ? selected : input);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    wchar_t value[64]{};
    const int index = item.itemID == static_cast<UINT>(-1)
        ? static_cast<int>(SendMessageW(item.hwndItem, CB_GETCURSEL, 0, 0))
        : static_cast<int>(item.itemID);
    if (index >= 0) SendMessageW(item.hwndItem, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    RECT textRect = item.rcItem;
    textRect.left += 8;
    DrawTextW(item.hDC, value, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (hwnd == g_app->settings && message == WM_DRAWITEM) {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_BUTTON && isSettingsToggle(GetDlgCtrlID(item->hwndItem))) {
            drawSettingsToggle(*item);
            return TRUE;
        }
        if (item && item->CtlType == ODT_COMBOBOX && GetDlgCtrlID(item->hwndItem) == kSettingLanguage) {
            drawSettingsLanguage(*item);
            return TRUE;
        }
    }
    if (hwnd == g_app->settings && message == WM_MEASUREITEM) {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure && measure->CtlType == ODT_COMBOBOX && measure->CtlID == kSettingLanguage) {
            measure->itemHeight = 24;
            return TRUE;
        }
    }
    if (message == WM_CREATE) {
        wchar_t className[64]{};
        GetClassNameW(hwnd, className, 64);
        if (std::wcscmp(className, L"ClipLiteSettings") == 0) {
            g_app->settingsFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            const COLORREF background = g_app->settingsData.dark ? RGB(25, 29, 34) : RGB(239, 246, 244);
            const COLORREF card = g_app->settingsData.dark ? RGB(34, 39, 46) : RGB(250, 253, 252);
            const COLORREF input = g_app->settingsData.dark ? RGB(43, 47, 54) : RGB(255, 255, 255);
            g_app->settingsBackgroundBrush = CreateSolidBrush(background);
            g_app->settingsCardBrush = CreateSolidBrush(card);
            g_app->settingsInputBrush = CreateSolidBrush(input);
            createSettingsControls(hwnd);
            applyFontToChildren(hwnd, g_app->settingsFont);
            return 0;
        }
        if (std::wcscmp(className, L"ClipLitePopup") == 0) {
            g_app->popupFont = CreateFontW(-ui(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_app->popupInputBrush = CreateSolidBrush(
                g_app->settingsData.dark ? RGB(43, 47, 54) : RGB(255, 255, 255));
            g_app->searchEdit = CreateWindowExW(0, L"EDIT", L"",
                                                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOHSCROLL,
                                                ui(96), ui(16), ui(164), ui(28), hwnd,
                                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEdit)),
                                                GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_app->searchEdit, WM_SETFONT,
                  reinterpret_cast<WPARAM>(g_app->popupFont), TRUE);
            SendMessageW(g_app->searchEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(ui(4), ui(4)));
            RECT editFormat{ui(4), ui(5), ui(160), ui(23)};
            SendMessageW(g_app->searchEdit, EM_SETRECTNP, 0,
                         reinterpret_cast<LPARAM>(&editFormat));
            SendMessageW(g_app->searchEdit, EM_SETCUEBANNER, FALSE,
                         reinterpret_cast<LPARAM>(tr(L"Search clipboard history", L"搜索剪贴板历史")));
            g_app->oldEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                g_app->searchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editProc)));
            SetFocus(hwnd);
            return 0;
        }
    }
    if (message == WM_ERASEBKGND && (hwnd == g_app->settings || hwnd == g_app->popup)) {
        if (hwnd == g_app->popup) return 1;
        HBRUSH brush = hwnd == g_app->settings ? g_app->settingsBackgroundBrush :
                                                   g_app->popupInputBrush;
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
        return 1;
    }
    if ((message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORBTN ||
         message == WM_CTLCOLORLISTBOX) &&
        (hwnd == g_app->settings || hwnd == g_app->popup)) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const bool dark = g_app->settingsData.dark;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark ? RGB(238, 241, 245) : RGB(30, 36, 46));
        if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, dark ? RGB(43, 47, 54) : RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(hwnd == g_app->popup
                ? g_app->popupInputBrush : g_app->settingsInputBrush);
        }
        return reinterpret_cast<LRESULT>(hwnd == g_app->settings
            ? g_app->settingsCardBrush : g_app->popupInputBrush);
    }
    if (hwnd == g_app->settings && message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        paintSettings(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_DESTROY && hwnd == g_app->popup) {
        if (g_app->popupFont) DeleteObject(g_app->popupFont);
        if (g_app->popupInputBrush) DeleteObject(g_app->popupInputBrush);
        g_app->popupFont = nullptr;
        g_app->popupInputBrush = nullptr;
    }
    if (message == WM_DESTROY && hwnd == g_app->settings) {
        if (g_app->settingsFont) DeleteObject(g_app->settingsFont);
        if (g_app->settingsBackgroundBrush) DeleteObject(g_app->settingsBackgroundBrush);
        if (g_app->settingsCardBrush) DeleteObject(g_app->settingsCardBrush);
        if (g_app->settingsInputBrush) DeleteObject(g_app->settingsInputBrush);
        g_app->settingsFont = nullptr;
        g_app->settingsBackgroundBrush = nullptr;
        g_app->settingsCardBrush = nullptr;
        g_app->settingsInputBrush = nullptr;
    }
    if (hwnd == g_app->hidden) {
        if (g_taskbarCreated != 0 && message == g_taskbarCreated) {
            addTrayIcon();
            return 0;
        }
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
        if (message == kExitMessage) {
            PostQuitMessage(0);
            return 0;
        }
        if (message == WM_CLIPBOARDUPDATE) {
            if (g_app->settingsData.pauseMonitoring) return 0;
            ClipType type{};
            std::string payload;
            std::string source;
            if (captureClipboard(type, payload, source)) {
                const auto hash = clipLiteHash(payload);
                if (hash != g_app->ignoredClipboardHash) {
                    g_app->store.append(type, payload, hash, source);
                } else {
                    g_app->ignoredClipboardHash = 0;
                }
            }
            return 0;
        }
    }

    if (hwnd == g_app->settings) {
        if (message == WM_COMMAND) {
            if (isSettingsToggle(LOWORD(wParam)) && HIWORD(wParam) == BN_CLICKED) {
                HWND toggle = GetDlgItem(hwnd, LOWORD(wParam));
                setSettingsToggleValue(toggle, !settingsToggleValue(toggle));
                InvalidateRect(toggle, nullptr, TRUE);
                return 0;
            }
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
                HWND pause = GetDlgItem(hwnd, kSettingPause);
                HWND maxItems = GetDlgItem(hwnd, kSettingMaxItems);
                HWND retentionDays = GetDlgItem(hwnd, kSettingRetentionDays);
                HWND maxDiskMb = GetDlgItem(hwnd, kSettingMaxDiskMb);
                HWND startup = GetDlgItem(hwnd, kSettingStartup);
                HWND encrypt = GetDlgItem(hwnd, kSettingEncrypt);
                const bool previousEncryption = g_app->store.encryptionEnabled();
                g_app->settingsData.winV = settingsToggleValue(win);
                g_app->settingsData.dark = settingsToggleValue(dark);
                wchar_t value[32]{};
                GetWindowTextW(maxItems, value, 32);
                g_app->settingsData.maxItems = std::clamp(std::wcstol(value, nullptr, 10), 0L, 100000L);
                GetWindowTextW(retentionDays, value, 32);
                g_app->settingsData.retentionDays = std::clamp(std::wcstol(value, nullptr, 10), 0L, 36500L);
                GetWindowTextW(maxDiskMb, value, 32);
                g_app->settingsData.maxDiskMb = std::clamp(std::wcstol(value, nullptr, 10), 0L, 102400L);
                g_app->settingsData.pauseMonitoring = settingsToggleValue(pause);
                g_app->settingsData.startWithWindows = settingsToggleValue(startup);
                const bool desiredEncryption = settingsToggleValue(encrypt);
                const int languageSelection = static_cast<int>(SendMessageW(language, CB_GETCURSEL, 0, 0));
                g_app->settingsData.language = languageSelection <= 0 ? -1 : languageSelection - 1;
                if (desiredEncryption != previousEncryption && !g_app->store.rekey(desiredEncryption)) {
                    g_app->settingsData.encryptData = previousEncryption;
                    MessageBoxW(hwnd, tr(L"Unable to change history encryption.", L"无法更改历史加密设置。"),
                                L"ClipLite", MB_OK | MB_ICONWARNING);
                } else {
                    g_app->settingsData.encryptData = desiredEncryption;
                }
                saveSettings(g_app->settingsData);
                updateStartupRegistration(g_app->settingsData.startWithWindows);
                const std::uint64_t cutoff = g_app->settingsData.retentionDays > 0
                    ? nowUnix() - static_cast<std::uint64_t>(g_app->settingsData.retentionDays) * 86400ULL
                    : 0;
                g_app->store.prune(g_app->settingsData.maxItems,
                                   static_cast<std::uint64_t>(g_app->settingsData.maxDiskMb) * 1024ULL * 1024ULL,
                                   cutoff);
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
        if (message == WM_ACTIVATEAPP && !wParam) {
            if (g_app->filterDragging) return 0;
            closePopup();
            return 0;
        }
        if (message == WM_NCACTIVATE && !wParam) {
            if (g_app->filterDragging) return 0;
            closePopup();
            return 0;
        }
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
            else if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == '0') {
                applyFilterCommand(kFilterAll);
                refreshVisible();
            }
            else if (wParam == VK_UP) { --g_app->selected; refreshVisible(); }
            else if (wParam == VK_DOWN) { ++g_app->selected; refreshVisible(); }
            else if (wParam == VK_RETURN) sendPaste();
            else if (wParam == VK_DELETE && !g_app->visible.empty()) {
                g_app->store.remove(g_app->visible[static_cast<std::size_t>(g_app->selected)]);
                refreshVisible();
            } else if (wParam == VK_F10) openSettings();
            return 0;
        }
        if (message == WM_MOUSEWHEEL) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);
            if (point.y >= ui(kPopupFilterTop) && point.y < ui(kPopupFilterBottom)) {
                const int direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -ui(48) : ui(48);
                g_app->filterScrollOffset += direction;
                clampFilterScroll();
                invalidateFilterBar(hwnd);
                return 0;
            }
            const int direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -3 : 3;
            scrollPopup(direction);
            return 0;
        }
        if (message == WM_MOUSEHWHEEL) {
            const int direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -ui(48) : ui(48);
            g_app->filterScrollOffset += direction;
            clampFilterScroll();
            invalidateFilterBar(hwnd);
            return 0;
        }
        if (message == WM_MOUSEMOVE && g_app->filterDragging) {
            g_app->filterScrollOffset = g_app->filterDragStartOffset +
                (g_app->filterDragStartX - GET_X_LPARAM(lParam));
            clampFilterScroll();
            invalidateFilterBar(hwnd);
            return 0;
        }
        if (message == WM_MOUSEMOVE) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            const int row = popupRowAt(GET_Y_LPARAM(lParam));
            if (row != g_app->hoveredRow) {
                g_app->hoveredRow = row;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            if (g_app->hoveredRow != -1) {
                g_app->hoveredRow = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (message == WM_LBUTTONUP && g_app->filterDragging) {
            const int clickX = GET_X_LPARAM(lParam);
            const int clickY = GET_Y_LPARAM(lParam);
            const bool isClick = std::abs(clickX - g_app->filterDragStartX) < ui(4);
            g_app->filterDragging = false;
            ReleaseCapture();
            if (isClick && clickY >= ui(kPopupFilterTop) && clickY < ui(kPopupFilterBottom)) {
                for (int slot = 0; slot < 7; ++slot) {
                    const RECT chipRect = automaticFilterRect(slot, g_app->filterScrollOffset);
                    if (clickX >= chipRect.left && clickX < chipRect.right) {
                        applyFilterCommand(automaticFilterCommand(slot));
                        g_app->selected = 0;
                        g_app->scrollOffset = 0;
                        refreshVisible();
                        break;
                    }
                }
            }
            return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            RECT client{};
            GetClientRect(hwnd, &client);
            const int clickX = GET_X_LPARAM(lParam);
            const int clickY = GET_Y_LPARAM(lParam);
            const bool inSearch = clickX >= ui(90) && clickX < ui(266) &&
                clickY >= ui(14) && clickY < ui(46);
            if (!inSearch) SetFocus(hwnd);
            if (GET_Y_LPARAM(lParam) >= ui(12) && GET_Y_LPARAM(lParam) < ui(46) &&
                GET_X_LPARAM(lParam) >= client.right - ui(54)) {
                SetWindowTextW(g_app->searchEdit, L"");
                g_app->query.clear();
                SetFocus(g_app->searchEdit);
                refreshVisible();
                return 0;
            }
            if (GET_Y_LPARAM(lParam) >= ui(12) && GET_Y_LPARAM(lParam) < ui(46) &&
                GET_X_LPARAM(lParam) >= ui(16) && GET_X_LPARAM(lParam) < ui(84)) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }
            if (clickY >= ui(kPopupFilterTop) && clickY < ui(kPopupFilterBottom)) {
                g_app->filterDragging = true;
                g_app->filterDragStartX = GET_X_LPARAM(lParam);
                g_app->filterDragStartOffset = g_app->filterScrollOffset;
                SetCapture(hwnd);
                return 0;
            }
            const int row = popupRowAt(clickY);
            if (row >= 0 && row < static_cast<int>(g_app->visible.size())) {
                const int rowTop = popupRowTop(row);
                if (GET_X_LPARAM(lParam) >= client.right - ui(32) &&
                    GET_Y_LPARAM(lParam) < rowTop + ui(30)) {
                    g_app->store.remove(g_app->visible[static_cast<std::size_t>(row)]);
                    refreshVisible();
                    return 0;
                }
                g_app->selected = row;
                sendPaste();
            }
            return 0;
        }
        if (message == WM_RBUTTONUP) {
            RECT client{};
            GetClientRect(hwnd, &client);
            const int clickY = GET_Y_LPARAM(lParam);
            const int row = popupRowAt(clickY);
            if (row >= 0 && row < static_cast<int>(g_app->visible.size())) {
                g_app->selected = row;
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, kMenuPaste, tr(L"Paste", L"粘贴"));
                AppendMenuW(menu, MF_STRING, kMenuPin, tr(L"Toggle pin", L"切换置顶"));
                AppendMenuW(menu, MF_STRING, kMenuDelete, tr(L"Delete", L"删除"));
                appendFilterMenu(menu);
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd, &point);
                const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                   point.x, point.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                const std::size_t index = g_app->visible[static_cast<std::size_t>(g_app->selected)];
                if (command == kMenuPaste) sendPaste();
                else if (command == kMenuPin) g_app->store.togglePinned(index);
                else if (command == kMenuDelete) g_app->store.remove(index);
                applyFilterCommand(command);
                refreshVisible();
            } else {
                HMENU menu = CreatePopupMenu();
                appendFilterMenu(menu);
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd, &point);
                const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                   point.x, point.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                applyFilterCommand(command);
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
            if (g_app->filterDragging) return 0;
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
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    const int width = 460;
    const int height = std::min(760, static_cast<int>(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - 40));
    const int x = monitorInfo.rcWork.left +
        (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    const int y = monitorInfo.rcWork.top +
        (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    g_app->settings = CreateWindowExW(WS_EX_TOOLWINDOW, L"ClipLiteSettings",
                                      tr(L"ClipLite Settings", L"ClipLite 设置"),
                                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                      x, y, width, height,
                                      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(g_app->settings, SW_SHOW);
    UpdateWindow(g_app->settings);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    AppState app;
    g_app = &app;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    loadSettings(app.settingsData);
    app.store.setEncryption(app.settingsData.encryptData);
    if (!app.store.open()) return 1;
    const std::uint64_t cutoff = app.settingsData.retentionDays > 0
        ? nowUnix() - static_cast<std::uint64_t>(app.settingsData.retentionDays) * 86400ULL
        : 0;
    app.store.prune(app.settingsData.maxItems,
                    static_cast<std::uint64_t>(app.settingsData.maxDiskMb) * 1024ULL * 1024ULL,
                    cutoff);

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW popupClass{};
    popupClass.style = CS_DROPSHADOW;
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
        HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, L"ClipLiteHidden", nullptr);
        if (existing) {
            if (wcsstr(commandLine, L"--exit") || wcsstr(commandLine, L"/exit")) {
                PostMessageW(existing, kExitMessage, 0, 0);
            } else if (wcsstr(commandLine, L"--history") || wcsstr(commandLine, L"/history")) {
                PostMessageW(existing, kShowPopupMessage, 0, 0);
            } else {
                PostMessageW(existing, kShowSettingsMessage, 0, 0);
            }
        }
        CloseHandle(mutex);
        return 0;
    }
    app.hidden = CreateWindowExW(0, L"ClipLiteHidden", L"ClipLite", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, instance, nullptr);
    if (!app.hidden) return 1;
    if (!AddClipboardFormatListener(app.hidden)) {
        MessageBoxW(nullptr, tr(L"Unable to monitor the clipboard.", L"无法监听剪贴板。"),
                    L"ClipLite", MB_OK | MB_ICONERROR);
        DestroyWindow(app.hidden);
        return 1;
    }
    registerHotkeys();
    addTrayIcon();
    if (wcsstr(commandLine, L"--history") || wcsstr(commandLine, L"/history")) {
        PostMessageW(app.hidden, kShowPopupMessage, 0, 0);
    } else {
        PostMessageW(app.hidden, kShowSettingsMessage, 0, 0);
    }

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
