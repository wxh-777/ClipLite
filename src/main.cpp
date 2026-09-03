#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shellscalingapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <winhttp.h>

#include "clip_store.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
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

struct ShortcutBinding {
    UINT modifiers = 0;
    UINT virtualKey = 0;
};

struct CategoryLimit {
    int maxItems = 0;
    int maxDiskMb = 0;
};

enum class PasteMode {
    Automatic,
    PlainText,
    RichText,
};

bool operator==(const CategoryLimit& first, const CategoryLimit& second) {
    return first.maxItems == second.maxItems && first.maxDiskMb == second.maxDiskMb;
}

constexpr int kHotkeyAltV = 1;
constexpr int kHotkeyWinV = 2;
constexpr int kHotkeySettings = 3;
constexpr int kHotkeyPause = 4;
constexpr int kSearchEdit = 10;
constexpr int kSettingWinV = 20;
constexpr int kSettingDark = 21;
constexpr int kSettingLanguage = 22;
constexpr int kSettingClear = 23;
constexpr int kSettingMaxItems = 25;
constexpr int kSettingRetentionDays = 26;
constexpr int kSettingMaxDiskMb = 27;
constexpr int kSettingPause = 28;
constexpr int kSettingStartup = 29;
constexpr int kSettingEncrypt = 30;
constexpr int kSettingMaxContentMb = 31;
constexpr int kSettingIgnoredApps = 32;
constexpr int kSettingSensitiveExpiry = 33;
constexpr int kSettingDataDirectory = 34;
constexpr int kSettingBrowseDataDirectory = 35;
constexpr int kSettingStartupSettings = 36;
constexpr int kSettingStartupNotification = 37;
constexpr int kSettingOpenLog = 38;
constexpr int kSettingRunAsAdministrator = 39;
constexpr int kSettingClearText = 40;
constexpr int kSettingClearImage = 41;
constexpr int kSettingClearFiles = 42;
constexpr int kSettingSupportAuthor = 43;
constexpr int kSettingJoinQqGroup = 44;
constexpr int kSettingSearchImeCompatibility = 45;
constexpr int kSettingPromotePastedItem = 46;
constexpr int kSettingShortcutHistory = 50;
constexpr int kSettingShortcutSettings = 51;
constexpr int kSettingShortcutPause = 52;
constexpr int kSettingShortcutPaste = 53;
constexpr int kSettingShortcutPastePlain = 54;
constexpr int kSettingShortcutPasteRich = 55;
constexpr int kSettingShortcutClosePopup = 56;
constexpr int kSettingShortcutPopupSettings = 57;
constexpr int kSettingShortcutClearFilter = 58;
constexpr int kSettingShortcutDelete = 59;
constexpr int kSettingCategoryMaxBase = 60;
constexpr int kSettingCategoryDiskBase = 64;
constexpr int kMenuPaste = 100;
constexpr int kMenuDelete = 102;
constexpr int kMenuPastePlain = 103;
constexpr int kMenuPasteRich = 104;
constexpr int kMenuPopupPinned = 105;
constexpr int kFilterAll = 130;
constexpr int kFilterText = 131;
constexpr int kFilterFiles = 132;
constexpr int kFilterImage = 133;
constexpr int kFilterPinned = 134;
constexpr int kFilterOther = 135;
constexpr int kStorageCategoryCount = 3;
constexpr int kAccentCount = 4;
constexpr UINT kShowPopupMessage = WM_APP + 1;
constexpr UINT kTrayMessage = WM_APP + 2;
constexpr UINT kShowSettingsMessage = WM_APP + 3;
constexpr UINT kExitMessage = WM_APP + 4;
constexpr UINT kClosePopupMessage = WM_APP + 5;
constexpr UINT kPopupSearchCompleteMessage = WM_APP + 6;
constexpr UINT kRunPopupImageBenchmarkMessage = WM_APP + 7;
constexpr UINT kPopupKeyboardMessage = WM_APP + 8;
constexpr UINT kSupportImageLoadedMessage = WM_APP + 9;
constexpr UINT kPopupEnterImeMessage = WM_APP + 10;
constexpr UINT kClipboardCaptureCompleteMessage = WM_APP + 11;
constexpr UINT_PTR kExpiryTimer = 3;
constexpr UINT_PTR kClipboardCaptureTimer = 7;
constexpr UINT_PTR kSettingsToggleTimer = 4;
constexpr UINT_PTR kSettingsDropdownTimer = 5;
constexpr UINT_PTR kSettingsSyncTimer = 6;
constexpr UINT_PTR kSettingsThemeTimer = 7;
constexpr UINT_PTR kSettingsActionFeedbackTimer = 8;
constexpr UINT_PTR kSettingsEncryptionTimer = 9;
constexpr UINT_PTR kPopupOpenGuardTimer = 10;
constexpr UINT_PTR kPopupDeactivateTimer = 11;
constexpr UINT_PTR kWinVReleaseTimer = 12;
constexpr UINT_PTR kPopupSearchTimer = 13;
constexpr UINT_PTR kSupportOwnerTimer = 14;
constexpr DWORD kSettingsToggleAnimationMs = 160;
constexpr DWORD kSettingsDropdownAnimationMs = 150;
constexpr DWORD kSettingsThemeAnimationMs = 180;
constexpr UINT kPopupOpenGuardMs = 120;
constexpr UINT kPopupDeactivateDelayMs = 80;
constexpr UINT kSettingsSyncDelayMs = 300;
constexpr UINT kSettingsEncryptionDebounceMs = 800;
constexpr UINT kSettingsActionFeedbackMs = 2400;
constexpr UINT kPopupSearchDelayMs = 90;
constexpr UINT kTrayId = 1;
constexpr int kPopupFilterTop = 58;
constexpr int kPopupFilterBottom = 82;
constexpr int kPopupSearchLeft = 86;
constexpr int kPopupSearchRight = 298;
constexpr int kPopupClearLeft = 302;
constexpr int kPopupClearRight = 334;
constexpr int kPopupPinLeft = 338;
constexpr int kPopupPinRight = 358;
constexpr int kPopupCloseLeft = 362;
constexpr int kPopupWidth = 400;
constexpr int kPopupHeight = 500;
constexpr int kPopupCornerRadius = 10;
constexpr int kPopupBorderInset = 1;
constexpr int kPopupListTop = 96;
constexpr int kPopupBottomPadding = 14;
constexpr int kPopupCardHeight = 108;
constexpr int kPopupCardGap = 8;
constexpr int kSettingsWidth = 740;
constexpr int kSettingsHeight = 520;
constexpr int kSettingsSidebarWidth = 180;
constexpr int kSettingsHeaderHeight = 58;
constexpr int kSettingsToggleWidth = 36;
constexpr int kSettingsToggleHeight = 20;
constexpr int kSettingsTrackWidth = 36;
constexpr int kSettingsTrackHeight = 20;
constexpr int kSettingsThemeWidth = 102;
constexpr int kSettingsThemeSegmentWidth = 32;
constexpr int kSettingsShortcutPage = 1;
constexpr int kTrayOpen = 200;
constexpr int kTraySettings = 201;
constexpr int kTrayExit = 202;
constexpr int kIconResource = 101;
constexpr int kSupportCopyGroup = 300;
constexpr int kSupportPaymentWidth = 720;
constexpr int kSupportPaymentHeight = 630;
constexpr int kSupportQqWidth = 560;
constexpr int kSupportQqHeight = 700;
constexpr std::size_t kPopupImagePreviewCacheLimit = 12;

struct Settings {
    bool winV = false;
    bool dark = false;
    int themeMode = -1; // -1 legacy value, 0 system, 1 light, 2 dark
    int accent = 0; // 0 blue, 1 purple, 2 legacy green, 3 orange
    bool pauseMonitoring = false;
    bool startWithWindows = false;
    bool showSettingsOnStartup = true;
    bool showStartupNotification = true;
    bool searchImeCompatibility = false;
    bool promotePastedItem = false;
    bool historyWindowPinned = false;
    bool runAsAdministrator = false;
    bool encryptData = false;
    int maxItems = 1000;
    int retentionDays = 30;
    int maxDiskMb = 256;
    int maxContentMb = 32;
    std::string dataDirectory;
    std::vector<std::string> ignoredApps;
    int sensitiveExpiryHours = 0;
    int language = -1; // -1 system, 0 English, 1 Simplified Chinese
    std::vector<std::string> categories{"General", "Work", "Code", "Links"};
    ShortcutBinding historyHotkey{MOD_ALT, 'V'};
    ShortcutBinding settingsHotkey{MOD_CONTROL | MOD_ALT, 'S'};
    ShortcutBinding pauseHotkey{MOD_CONTROL | MOD_SHIFT, 'P'};
    ShortcutBinding popupPasteHotkey{0, VK_RETURN};
    ShortcutBinding popupPlainPasteHotkey{MOD_CONTROL | MOD_SHIFT, 'V'};
    ShortcutBinding popupRichPasteHotkey{MOD_CONTROL | MOD_SHIFT, 'R'};
    ShortcutBinding popupCloseHotkey{0, VK_ESCAPE};
    ShortcutBinding popupSettingsHotkey{0, VK_F10};
    ShortcutBinding popupClearFilterHotkey{MOD_CONTROL, '0'};
    ShortcutBinding popupDeleteHotkey{0, VK_DELETE};
    std::array<CategoryLimit, kStorageCategoryCount> categoryLimits{};
};

struct PopupImagePreview {
    std::size_t itemIndex = 0;
    HBITMAP bitmap = nullptr;
    int width = 0;
    int height = 0;
};

struct AppState {
    HWND hidden = nullptr;
    HWND popup = nullptr;
    HWND searchEdit = nullptr;
    HWND settings = nullptr;
    HWND support = nullptr;
    HANDLE supportProcess = nullptr;
    DWORD supportProcessId = 0;
    HWND targetWindow = nullptr;
    HWND targetFocusWindow = nullptr;
    HFONT popupFont = nullptr;
    HFONT popupTitleFont = nullptr;
    HFONT popupFilterFont = nullptr;
    HFONT popupPreviewFont = nullptr;
    HFONT popupMetaFont = nullptr;
    HFONT settingsFont = nullptr;
    HFONT settingsMultilineFont = nullptr;
    HFONT settingsNavFont = nullptr;
    HFONT settingsTitleFont = nullptr;
    HFONT settingsCardTitleFont = nullptr;
    HFONT settingsBodyFont = nullptr;
    HBRUSH popupInputBrush = nullptr;
    HBRUSH settingsBackgroundBrush = nullptr;
    HBRUSH settingsCardBrush = nullptr;
    HBRUSH settingsInputBrush = nullptr;
    ULONG_PTR gdiplusToken = 0;
    HHOOK keyboardHook = nullptr;
    HHOOK popupKeyboardHook = nullptr;
    bool winVHotkeyRegistered = false;
    HHOOK popupMouseHook = nullptr;
    bool winKeyDown = false;
    bool suppressWinV = false;
    bool suppressVKeyUp = false;
    bool pendingWinVPopup = false;
    bool winKeyForwarded = false;
    UINT pendingWinKey = VK_LWIN;
    DWORD winKeyDownTime = 0;
    WNDPROC oldEditProc = nullptr;
    POINT popupPoint{};
    int selected = 0;
    int scrollOffset = 0;
    int scrollPosition = 0;
    int filterScrollOffset = 0;
    bool filterDragging = false;
    int filterDragStartX = 0;
    int filterDragStartOffset = 0;
    bool popupPinned = false;
    bool popupOpening = false;
    bool popupActivated = false;
    bool popupOpenedByWinV = false;
    DWORD popupOpenInputTick = 0;
    bool popupImeMode = false;
    bool scrollDragging = false;
    bool fastImagePreview = false;
    int scrollDragStartY = 0;
    int scrollDragStartOffset = 0;
    int hoveredRow = -1;
    int hoveredFilter = -1;
    int hoveredDeleteRow = -1;
    int hoveredPinRow = -1;
    int pressedRow = -1;
    bool hoveredHeader = false;
    int hoveredSettingsTab = -1;
    int hoveredSettingsThemeMode = -1;
    int hoveredSettingsControl = 0;
    int settingsScrollOffset = 0;
    int settingsControlsTab = -1;
    HWND toggleAnimationControl = nullptr;
    LONGLONG toggleAnimationStartTicks = 0;
    float toggleAnimationFrom = 0.0f;
    float toggleAnimationTo = 0.0f;
    bool settingsThemeAnimating = false;
    bool settingsThemeFromDark = false;
    bool settingsThemeToDark = false;
    float settingsThemeProgressValue = 1.0f;
    LONGLONG settingsThemeStartTicks = 0;
    HWND languageDropdown = nullptr;
    int languageDropdownHover = -1;
    LONGLONG languageDropdownStartTicks = 0;
    float languageDropdownFrom = 0.0f;
    float languageDropdownTo = 0.0f;
    bool restoringSettingsControls = false;
    bool settingsClosing = false;
    int settingsTab = 0;
    std::wstring settingsActionFeedback;
    bool settingsActionFeedbackSuccess = true;
    HWND shortcutCaptureControl = nullptr;
    bool shortcutRegistrationWarning = false;
    int filterType = 0;
    bool pinnedOnly = false;
    std::string query;
    bool popupSearchInputActive = false;
    bool popupSearchControlDown = false;
    bool popupSuppressImeTriggerSpace = false;
    std::shared_ptr<std::atomic<bool>> searchCancellation;
    std::thread searchWorker;
    std::shared_ptr<std::atomic<bool>> clipboardCaptureRunning =
        std::make_shared<std::atomic<bool>>(false);
    bool clipboardCapturePending = false;
    std::uint64_t searchGeneration = 0;
    Settings settingsData;
    ClipStore store;
    std::vector<std::size_t> visible;
    std::vector<PopupImagePreview> imagePreviews;
    std::uint64_t imagePreviewRevision = 0;
    int benchmarkExitCode = 0;
    std::uint64_t ignoredClipboardHash = 0;
    std::uint64_t ignoredClipboardTextHash = 0;
    ULONGLONG ignoredClipboardUntil = 0;
};

struct SupportWindowState {
    enum class ImageKind {
        Wechat,
        Alipay,
        Qq,
    };

    struct DownloadResult {
        ImageKind kind;
        std::vector<BYTE> bytes;
    };

    struct DownloadQueue {
        std::mutex mutex;
        std::vector<DownloadResult> completed;
    };

    bool qqGroup = false;
    struct Image {
        IStream* stream = nullptr;
        std::unique_ptr<Gdiplus::Image> value;

        ~Image() {
            value.reset();
            if (stream) stream->Release();
        }
    };
    std::unique_ptr<Image> wechat;
    std::unique_ptr<Image> alipay;
    std::unique_ptr<Image> qq;
    std::shared_ptr<DownloadQueue> downloadQueue = std::make_shared<DownloadQueue>();
    bool wechatLoading = false;
    bool alipayLoading = false;
    bool qqLoading = false;
    HFONT titleFont = nullptr;
    HFONT bodyFont = nullptr;
};

AppState* g_app = nullptr;
UINT g_taskbarCreated = 0;
UINT g_uiDpi = 96;
std::wstring g_diagnosticLogPath;
bool g_supportProcessMode = false;
HWND g_supportOwnerWindow = nullptr;

struct PopupSearchResult {
    HWND popup = nullptr;
    std::uint64_t generation = 0;
    std::uint64_t storeRevision = 0;
    std::vector<std::size_t> candidates;
};

struct ClipboardCaptureResult {
    ClipType type = ClipType::Text;
    std::string payload;
    std::string source;
    bool captured = false;
};

struct PopupImeKeyEvent {
    KBDLLHOOKSTRUCT key{};
    WPARAM hookMessage = 0;
};

std::wstring diagnosticLogPath() {
    return g_diagnosticLogPath.empty() ? clipLiteDataDirectory() + L"\\cliplite.log"
                                       : g_diagnosticLogPath;
}

void appendDiagnosticLog(const char* level, const char* message, DWORD errorCode = 0) {
    constexpr std::int64_t kMaxLogBytes = 1024 * 1024;
    std::FILE* file = nullptr;
    _wfopen_s(&file, diagnosticLogPath().c_str(), L"ab+");
    if (!file) return;
    _fseeki64(file, 0, SEEK_END);
    if (_ftelli64(file) >= kMaxLogBytes) {
        std::fclose(file);
        file = nullptr;
        _wfopen_s(&file, diagnosticLogPath().c_str(), L"wb");
        if (!file) return;
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s",
                 time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                 time.wSecond, time.wMilliseconds, level, message);
    if (errorCode != 0) std::fprintf(file, " error=%lu", errorCode);
    std::fputc('\n', file);
    std::fclose(file);
}

LONGLONG settingsToggleClock();
LONGLONG settingsToggleClockFrequency();
float settingsThemeProgress();
COLORREF settingsThemeColor(COLORREF light, COLORREF dark);
COLORREF settingsAccentColor();
COLORREF settingsAccentSoftColor();
void animateSettingsTheme(HWND hwnd, bool fromDark, bool toDark);
void setSettingsActionFeedback(HWND hwnd, const std::wstring& message, bool success);
void scheduleSettingsSync(HWND hwnd);
void openSupportWindow(bool qqGroup);
LRESULT CALLBACK supportWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
HICON clipLiteIcon();
void clearPopupImagePreviews();

int ui(int value) {
    return MulDiv(value, static_cast<int>(g_uiDpi), 96);
}

struct BenchmarkDirectoryGuard {
    std::wstring path;
    HANDLE handle = INVALID_HANDLE_VALUE;

    ~BenchmarkDirectoryGuard() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (!path.empty()) {
            DeleteFileW((path + L"\\history.bin").c_str());
            DeleteFileW((path + L"\\history.bin.tmp").c_str());
            RemoveDirectoryW(path.c_str());
        }
    }
};

struct ScopedComInitialization {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ~ScopedComInitialization() {
        if (SUCCEEDED(result)) CoUninitialize();
    }

    bool available() const {
        return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
    }
};

bool createImageBenchmarkDirectory(BenchmarkDirectoryGuard& directory) {
    wchar_t temporaryPath[MAX_PATH]{};
    const DWORD temporaryLength = GetTempPathW(ARRAYSIZE(temporaryPath), temporaryPath);
    if (temporaryLength == 0 || temporaryLength >= ARRAYSIZE(temporaryPath)) return false;
    std::wstring root(temporaryPath, temporaryLength);
    while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();

    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::wstring candidate = root + L"\\ClipLiteImageBenchmark-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
        if (!CreateDirectoryW(candidate.c_str(), nullptr)) {
            if (GetLastError() == ERROR_ALREADY_EXISTS) continue;
            return false;
        }
        directory.path = candidate;
        directory.handle = CreateFileW(candidate.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (directory.handle == INVALID_HANDLE_VALUE) return false;
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(directory.handle, &information) ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            CloseHandle(directory.handle);
            directory.handle = INVALID_HANDLE_VALUE;
            return false;
        }
        return true;
    }
    return false;
}

std::string makeBenchmarkImagePayload(unsigned char value) {
    constexpr int kBenchmarkImageWidth = 1024;
    constexpr int kBenchmarkImageHeight = 1024;
    constexpr std::size_t kBenchmarkImageBytes =
        static_cast<std::size_t>(kBenchmarkImageWidth) * kBenchmarkImageHeight * 4;
    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header);
    header.biWidth = kBenchmarkImageWidth;
    header.biHeight = -kBenchmarkImageHeight;
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = static_cast<DWORD>(kBenchmarkImageBytes);
    std::string payload(sizeof(header) + kBenchmarkImageBytes, static_cast<char>(value));
    std::memcpy(payload.data(), &header, sizeof(header));
    return payload;
}

bool populateBenchmarkImages() {
    if (!g_app->store.open() || !g_app->store.clear()) return false;
    for (int index = 0; index < 100; ++index) {
        const std::string payload = makeBenchmarkImagePayload(
            static_cast<unsigned char>(index % 251 + 1));
        if (!g_app->store.append(ClipType::Image, payload, clipLiteHash(payload), "Image stress")) {
            return false;
        }
    }
    return true;
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

bool highContrastEnabled() {
    HIGHCONTRASTW settings{sizeof(settings)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0) != FALSE &&
           (settings.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

const wchar_t* tr(const wchar_t* en, const wchar_t* zh) {
    return languageIsChinese() ? zh : en;
}

struct SettingsLocale {
    const wchar_t* appearanceCard;
    const wchar_t* darkTheme;
    const wchar_t* language;
    const wchar_t* systemCard;
    const wchar_t* searchInputCompatibility;
    const wchar_t* pauseMonitoring;
    const wchar_t* startWithWindows;
    const wchar_t* showSettingsOnStartup;
    const wchar_t* showStartupNotification;
    const wchar_t* runAsAdministrator;
    const wchar_t* importantSystemShortcut;
    const wchar_t* forceReplaceWinV;
    const wchar_t* globalShortcuts;
    const wchar_t* openClipboardHistory;
    const wchar_t* openSettings;
    const wchar_t* pauseResumeMonitoring;
    const wchar_t* historyWindowShortcuts;
    const wchar_t* pasteSelectedItem;
    const wchar_t* promotePastedItem;
    const wchar_t* pastePlainText;
    const wchar_t* pasteRichText;
    const wchar_t* closeHistoryWindow;
    const wchar_t* openSettingsInHistory;
    const wchar_t* clearHistoryFilter;
    const wchar_t* deleteSelectedRecord;
    const wchar_t* registrationStatus;
    const wchar_t* dataRetention;
    const wchar_t* maximumRecords;
    const wchar_t* retentionDays;
    const wchar_t* maximumDiskSpace;
    const wchar_t* maximumItemSize;
    const wchar_t* cacheDirectory;
    const wchar_t* text;
    const wchar_t* images;
    const wchar_t* files;
    const wchar_t* categoryStorage;
    const wchar_t* historyStatistics;
    const wchar_t* privacyProtection;
    const wchar_t* protectHistory;
    const wchar_t* encryptionNote;
    const wchar_t* ignoredApplications;
    const wchar_t* sensitiveContentExpiry;
    const wchar_t* protectionScope;
    const wchar_t* about;
    const wchar_t* navGeneral;
    const wchar_t* navShortcuts;
    const wchar_t* navStorage;
    const wchar_t* navPrivacy;
    const wchar_t* navAbout;
    const wchar_t* titleGeneral;
    const wchar_t* titleShortcuts;
    const wchar_t* titleStorage;
    const wchar_t* titlePrivacy;
    const wchar_t* titleAbout;
    const wchar_t* autoSaved;
    const wchar_t* totalHistoryFormat;
    const wchar_t* pinned;
    const wchar_t* records;
    const wchar_t* space;
    const wchar_t* currentCategoryFormat;
    const wchar_t* categoryNote;
    const wchar_t* shortcutRegistrationWarning;
    const wchar_t* shortcutModifierRequirement;
    const wchar_t* privacyNote;
    const wchar_t* aboutApplication;
    const wchar_t* aboutVersion;
    const wchar_t* aboutStorageFormat;
    const wchar_t* aboutDataDirectory;
    const wchar_t* browse;
    const wchar_t* clearHistory;
    const wchar_t* clearText;
    const wchar_t* clearImages;
    const wchar_t* clearFiles;
    const wchar_t* pressShortcut;
    const wchar_t* needModifier;
    const wchar_t* ignoredAppsPlaceholder;
    const wchar_t* autoLanguage;
    const wchar_t* windowTitle;
    const wchar_t* chooseValidCache;
    const wchar_t* unableCreateCache;
    const wchar_t* targetContainsHistory;
    const wchar_t* unableMigrateHistory;
    const wchar_t* unableOpenCache;
    const wchar_t* chooseCacheDirectory;
    const wchar_t* unableChangeEncryption;
    const wchar_t* historyLabel;
    const wchar_t* textLabel;
    const wchar_t* imagesLabel;
    const wchar_t* filesLabel;
    const wchar_t* confirmClearAllFormat;
    const wchar_t* confirmClearTypeFormat;
    const wchar_t* confirmClearTitle;
    const wchar_t* clearCancelled;
    const wchar_t* noRecordsFormat;
    const wchar_t* clearedRecordsFormat;
    const wchar_t* clearFailed;
    const wchar_t* pruneConfirmationTitle;
    const wchar_t* pruneConfirmationMessage;
    const wchar_t* popupTitle;
    const wchar_t* popupClearSearch;
    const wchar_t* popupFilterAll;
    const wchar_t* popupFilterPinned;
    const wchar_t* popupFilterText;
    const wchar_t* popupFilterImages;
    const wchar_t* popupFilterFiles;
    const wchar_t* popupFilterOther;
    const wchar_t* popupClipboardSource;
    const wchar_t* popupEmpty;
    const wchar_t* popupPaste;
    const wchar_t* popupPastePlain;
    const wchar_t* popupPasteRich;
    const wchar_t* popupPin;
    const wchar_t* popupUnpin;
    const wchar_t* popupDelete;
    const wchar_t* popupFilter;
    const wchar_t* popupSearchPlaceholder;
    const wchar_t* popupTextType;
    const wchar_t* popupImageType;
    const wchar_t* popupFilesType;
    const wchar_t* popupOtherType;
    const wchar_t* relativeJustNow;
    const wchar_t* relativeMinutesAgo;
    const wchar_t* relativeHoursAgo;
    const wchar_t* relativeDaysAgo;
    const wchar_t* trayOpen;
    const wchar_t* traySettings;
    const wchar_t* trayExit;
    const wchar_t* popupEmptyPreview;
    const wchar_t* popupImagePreview;
    const wchar_t* popupFilesPreview;
    const wchar_t* popupHtmlPreview;
    const wchar_t* popupInvalidText;
    const wchar_t* startupNotificationTitle;
    const wchar_t* startupNotification;
    const wchar_t* startupFailureTitle;
    const wchar_t* unableOpenStore;
    const wchar_t* unableCreateBackground;
    const wchar_t* unableMonitorClipboard;
    const wchar_t* unableCreateMutex;
    const wchar_t* logFilePrefix;
    const wchar_t* openLog;
    const wchar_t* unableOpenLog;
    const wchar_t* supportAuthor;
    const wchar_t* joinQqGroup;
    const wchar_t* supportTitle;
    const wchar_t* supportMessage;
    const wchar_t* wechatPay;
    const wchar_t* alipayPay;
    const wchar_t* qqGroupTitle;
    const wchar_t* qqGroupNumber;
    const wchar_t* copyQqGroup;
    const wchar_t* copiedQqGroup;
    const wchar_t* supportImageLoading;
    const wchar_t* supportImageLoadFailed;
};

const SettingsLocale kEnglishSettingsLocale{
    L"Appearance and interaction", L"Dark theme", L"Language", L"System and integration",
    L"Search input compatibility\nKeep the original app in the foreground; use Ctrl + Space for Chinese IME.",
    L"Pause clipboard monitoring", L"Start with Windows", L"Open settings after startup",
    L"Show startup notification", L"Run ClipLite as administrator (restart required to disable)",
    L"Important system shortcut", L"Force replace Win + V", L"Global shortcuts", L"Open clipboard history", L"Open settings",
    L"Pause/resume clipboard monitoring", L"History window shortcuts", L"Paste selected item",
    L"Move pasted item to top", L"Paste as plain text", L"Paste as rich text", L"Close history window",
    L"Open settings in history window", L"Clear history filter", L"Delete selected record",
    L"Registration status", L"Data retention", L"Maximum records", L"Retention days (0 = forever)",
    L"Maximum disk space (MB)", L"Maximum item size (MB)", L"Cache directory", L"Text", L"Images",
    L"Files", L"Category storage", L"History statistics", L"Privacy protection",
    L"Protect history with Windows user encryption",
    L"Changes apply after a short delay and migrate all existing records. Encryption may reduce clipboard and scrolling performance, especially for large images. Encrypted data usually cannot be opened by another Windows user or device.",
    L"Ignored applications (one source name per line)",
    L"Sensitive content expiry (hours, 0 = off)", L"Protection scope", L"About ClipLite",
    L"General", L"Shortcuts", L"Storage", L"Privacy", L"About ClipLite", L"General settings",
    L"Shortcuts", L"Storage", L"Privacy", L"About ClipLite", L"Auto-saved",
    L"Total history: %zu records, %ls", L"Pinned", L"Records", L"Space",
    L"Current %zu \xB7 %ls",
    L"Set to 0 for unlimited. Pinned records are not removed automatically.",
    L"Some shortcuts could not be registered. Choose different combinations.",
    L"Custom shortcuts require at least one modifier key.",
    L"Sensitive markers: password, token, api_key, secret, and private keys; detected by content pattern.",
      L"Application    ClipLite", L"Version        1.0.5 x64", L"Storage format  v4",
    L"Data directory  %LOCALAPPDATA%\\ClipLite", L"Browse", L"Clear history", L"Clear text",
    L"Clear images", L"Clear files", L"Press shortcut", L"Need modifier", L"One application per line", L"Auto",
    L"ClipLite Settings", L"Choose a valid cache directory.", L"Unable to create the cache directory.",
    L"The target directory already contains history. Choose an empty directory.",
    L"Unable to migrate clipboard history.", L"Unable to open the new cache directory.",
    L"Choose a cache directory", L"Unable to change history encryption.", L"history", L"text", L"images",
    L"files", L"This will permanently remove %zu clipboard records. Continue?",
    L"This will permanently remove %zu %ls records. Continue?", L"Confirm clear", L"Clear cancelled",
    L"No %ls records to clear", L"Cleared %zu %ls records", L"Clear failed; history was not changed",
    L"Confirm retention change", L"Reducing retention limits may permanently remove existing clipboard records. Continue?",
    L"Clipboard history", L"Clear", L"All", L"Pinned", L"Text", L"Images", L"Files", L"Other",
    L"Clipboard", L"No clipboard history", L"Paste", L"Paste as plain text", L"Paste as rich text",
    L"Pin history window", L"Unpin history window", L"Delete", L"Filter", L"Search clipboard history",
    L"Text", L"Image", L"Files", L"Other", L"Just now", L" min ago", L" hr ago", L" d ago",
    L"Open history", L"Settings", L"Exit", L"[Empty]", L"[Image]", L"[Files]", L"[HTML]",
    L"[Invalid text]", L"ClipLite started", L"ClipLite is running in the system tray.",
    L"ClipLite startup failed", L"Unable to open clipboard history storage.",
    L"Unable to create the background window.", L"Unable to monitor the clipboard.",
    L"Unable to create the single-instance lock.", L"Log file        ", L"Open log file",
     L"Unable to open the log file.", L"Support the author", L"Join QQ group",
     L"Support ClipLite", L"Thank you for supporting continued development.", L"WeChat Pay",
     L"Alipay", L"ClipLite QQ group", L"Group: 1081580020", L"Copy group number",
     L"Copied", L"Loading image...", L"Unable to load the support image. Check your network connection."
};

const SettingsLocale kChineseSettingsLocale{
    L"界面与交互", L"深色主题", L"语言", L"系统与集成",
    L"搜索输入兼容模式\n保持原应用前台；按 Ctrl + Space 使用中文输入法。", L"暂停剪贴板监听", L"随 Windows 启动",
    L"启动后打开设置", L"启动后显示系统提示", L"以管理员权限运行（关闭后下次启动生效）",
    L"重要系统快捷键", L"强制替换 Win + V", L"全局快捷键", L"打开剪贴板历史", L"打开设置",
    L"暂停/恢复剪贴板监听", L"历史窗口快捷键", L"粘贴选中项目", L"粘贴后移到首位", L"粘贴为纯文本", L"粘贴为富文本",
    L"关闭历史窗口", L"在历史窗口打开设置", L"清除历史筛选", L"删除选中记录", L"注册状态",
    L"数据保留策略", L"最大记录数", L"保留天数（0 = 永久）", L"最大磁盘空间（MB）",
    L"单条内容上限（MB）", L"缓存目录", L"文本", L"图片", L"文件", L"分类存储设置",
    L"历史记录统计", L"隐私保护", L"使用 Windows 用户加密保护历史",
    L"切换将在短暂延迟后生效，并迁移全部已有记录。开启加密可能降低剪贴板及列表滚动性能，处理大型图片时更明显。加密数据通常无法由其他 Windows 用户或设备解密。",
    L"忽略应用（每行一个来源名称）", L"敏感内容过期时间（小时，0 = 关闭）", L"保护范围",
    L"关于 ClipLite", L"通用", L"快捷键", L"存储管理", L"安全与隐私", L"关于 ClipLite",
    L"通用设置", L"快捷键", L"存储管理", L"安全与隐私", L"关于 ClipLite", L"已自动保存",
    L"当前历史总计：%zu 条记录，%ls", L"置顶", L"记录", L"空间",
    L"当前 %zu 条 \xB7 %ls", L"设置为 0 表示不限；置顶记录不会被自动清理。",
    L"部分快捷键注册失败，请更换组合键。", L"自定义快捷键至少需要一个修饰键。",
    L"敏感标记：password、token、api_key、secret 和私钥；按内容格式检测。",
     L"应用名称    ClipLite", L"版本        1.0.5 x64", L"存储格式    v4",
    L"数据目录    %LOCALAPPDATA%\\ClipLite", L"浏览", L"清空历史", L"清理文本", L"清理图片",
    L"清理文件", L"按下组合键", L"需要修饰键", L"每行一个应用名称", L"自动", L"ClipLite 设置",
    L"请选择有效的缓存目录。", L"无法创建缓存目录。", L"目标目录已有历史数据，请选择空目录。",
    L"无法迁移剪贴板历史。", L"无法打开新的缓存目录。", L"选择缓存目录", L"无法更改历史加密设置。",
    L"历史", L"文本", L"图片", L"文件", L"此操作将永久删除 %zu 条剪贴板记录，是否继续？",
    L"此操作将永久删除 %zu 条%ls记录，是否继续？", L"确认清理", L"已取消清理",
    L"没有可清理的%ls记录", L"已清理 %zu 条%ls记录", L"清理失败，历史记录未改变",
    L"确认保留策略变更", L"降低保留限制可能永久删除现有剪贴板记录，是否继续？",
    L"剪贴板历史", L"清空", L"全部", L"置顶", L"文本", L"图片", L"文件", L"其他",
    L"剪贴板", L"暂无剪贴板记录", L"粘贴", L"粘贴为纯文本", L"粘贴为富文本",
    L"置顶历史窗口", L"取消历史窗口置顶", L"删除", L"筛选", L"搜索剪贴板历史",
    L"文本", L"图片", L"文件", L"其他", L"刚刚", L"分钟前", L"小时前", L"天前",
    L"打开历史", L"设置", L"退出", L"[空]", L"[图片]", L"[文件]", L"[HTML]", L"[无效文本]",
    L"ClipLite 已启动", L"ClipLite 正在系统托盘中运行。", L"ClipLite 启动失败",
    L"无法打开剪贴板历史存储。", L"无法创建后台窗口。", L"无法监听剪贴板。",
     L"无法创建单实例锁。", L"日志文件        ", L"打开日志文件", L"无法打开日志文件。",
     L"支持作者", L"加入 QQ 群", L"支持 ClipLite", L"感谢你支持项目持续开发。", L"微信支付",
     L"支付宝", L"ClipLite QQ 群", L"群号：1081580020", L"复制群号", L"已复制", L"正在加载图片...",
     L"无法加载支持图片，请检查网络连接。"
};

const SettingsLocale& settingsLocale() {
    return languageIsChinese() ? kChineseSettingsLocale : kEnglishSettingsLocale;
}

std::wstring settingsPath() {
    return clipLiteDataDirectory() + L"\\settings.ini";
}

bool systemThemeIsDark() {
    HKEY key = nullptr;
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(key);
    }
    return value == 0;
}

void normalizeShortcutBinding(ShortcutBinding& binding, const ShortcutBinding& fallback) {
    constexpr UINT allowedModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
    binding.modifiers &= allowedModifiers;
    if (binding.modifiers == 0 || binding.virtualKey == 0 || binding.virtualKey > 0xFF) {
        binding = fallback;
    }
}

std::uint64_t nowUnix() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

std::wstring formatByteSize(std::uint64_t bytes) {
    constexpr std::uint64_t kKibiByte = 1024;
    constexpr std::uint64_t kMebiByte = kKibiByte * 1024;
    constexpr std::uint64_t kGibiByte = kMebiByte * 1024;
    const wchar_t* unit = L"B";
    double value = static_cast<double>(bytes);
    if (bytes >= kGibiByte) {
        value /= static_cast<double>(kGibiByte);
        unit = L"GB";
    } else if (bytes >= kMebiByte) {
        value /= static_cast<double>(kMebiByte);
        unit = L"MB";
    } else if (bytes >= kKibiByte) {
        value /= static_cast<double>(kKibiByte);
        unit = L"KB";
    } else {
        wchar_t result[32]{};
        swprintf_s(result, L"%llu B", static_cast<unsigned long long>(bytes));
        return result;
    }

    wchar_t result[32]{};
    const wchar_t* format = value >= 100.0 ? L"%.0f %ls" :
        (value >= 10.0 ? L"%.1f %ls" : L"%.2f %ls");
    swprintf_s(result, format, value, unit);
    return result;
}

std::wstring formatRelativeTime(std::uint64_t timestamp) {
    if (timestamp == 0) return {};
    const std::uint64_t current = nowUnix();
    const std::uint64_t elapsed = current > timestamp ? current - timestamp : 0;
    if (elapsed < 60) return settingsLocale().relativeJustNow;
    if (elapsed < 3600) {
        return std::to_wstring(elapsed / 60) + settingsLocale().relativeMinutesAgo;
    }
    if (elapsed < 86400) {
        return std::to_wstring(elapsed / 3600) + settingsLocale().relativeHoursAgo;
    }
    return std::to_wstring(elapsed / 86400) + settingsLocale().relativeDaysAgo;
}

void loadSettings(Settings& settings) {
    std::array<CategoryLimit, 4> legacyCategoryLimits{};
    std::FILE* file = nullptr;
    _wfopen_s(&file, settingsPath().c_str(), L"rb");
    if (!file) {
        settings.themeMode = settings.dark ? 2 : 1;
        return;
    }
    char line[1024]{};
    const auto readShortcutValue = [&line](const char* prefix, UINT& target) {
        const std::size_t length = std::strlen(prefix);
        if (std::strncmp(line, prefix, length) == 0) {
            target = static_cast<UINT>(std::atoi(line + length));
        }
    };
    while (std::fgets(line, sizeof(line), file)) {
        if (std::strncmp(line, "winV=1", 6) == 0) settings.winV = true;
        if (std::strncmp(line, "dark=1", 6) == 0) settings.dark = true;
        if (std::strncmp(line, "themeMode=", 10) == 0) settings.themeMode = std::clamp(std::atoi(line + 10), 0, 2);
        if (std::strncmp(line, "accent=", 7) == 0) settings.accent = std::clamp(std::atoi(line + 7), 0, 3);
        if (std::strncmp(line, "pauseMonitoring=1", 17) == 0) settings.pauseMonitoring = true;
        if (std::strncmp(line, "startWithWindows=1", 18) == 0) settings.startWithWindows = true;
        if (std::strncmp(line, "showSettingsOnStartup=0", 23) == 0) settings.showSettingsOnStartup = false;
        if (std::strncmp(line, "showSettingsOnStartup=1", 23) == 0) settings.showSettingsOnStartup = true;
        if (std::strncmp(line, "showStartupNotification=0", 25) == 0) settings.showStartupNotification = false;
        if (std::strncmp(line, "showStartupNotification=1", 25) == 0) settings.showStartupNotification = true;
        if (std::strncmp(line, "searchImeCompatibility=1", 24) == 0) settings.searchImeCompatibility = true;
        if (std::strncmp(line, "promotePastedItem=1", 19) == 0) settings.promotePastedItem = true;
        if (std::strncmp(line, "historyWindowPinned=1", 21) == 0) settings.historyWindowPinned = true;
        if (std::strncmp(line, "runAsAdministrator=1", 20) == 0) settings.runAsAdministrator = true;
        if (std::strncmp(line, "encryptData=1", 13) == 0) settings.encryptData = true;
        if (std::strncmp(line, "maxItems=", 9) == 0) settings.maxItems = std::clamp(std::atoi(line + 9), 0, 100000);
        if (std::strncmp(line, "retentionDays=", 14) == 0) settings.retentionDays = std::clamp(std::atoi(line + 14), 0, 36500);
        if (std::strncmp(line, "maxDiskMb=", 10) == 0) settings.maxDiskMb = std::clamp(std::atoi(line + 10), 0, 102400);
        if (std::strncmp(line, "maxContentMb=", 13) == 0) settings.maxContentMb = std::clamp(std::atoi(line + 13), 1, 32);
        if (std::strncmp(line, "dataDirectory=", 14) == 0) {
            settings.dataDirectory = line + 14;
            while (!settings.dataDirectory.empty() &&
                   (settings.dataDirectory.back() == '\r' || settings.dataDirectory.back() == '\n')) {
                settings.dataDirectory.pop_back();
            }
        }
        if (std::strncmp(line, "ignoredApp=", 11) == 0 && line[11] != '\0') {
            std::string app(line + 11);
            while (!app.empty() && (app.back() == '\r' || app.back() == '\n')) app.pop_back();
            if (!app.empty()) settings.ignoredApps.push_back(std::move(app));
        }
        if (std::strncmp(line, "sensitiveExpiryHours=", 21) == 0) {
            settings.sensitiveExpiryHours = std::clamp(std::atoi(line + 21), 0, 720);
        }
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
            const std::string maxPrefix = "categoryMaxItems" + std::to_string(i) + "=";
            if (std::strncmp(line, maxPrefix.c_str(), maxPrefix.size()) == 0) {
                legacyCategoryLimits[static_cast<std::size_t>(i)].maxItems =
                    std::clamp(std::atoi(line + maxPrefix.size()), 0, 100000);
            }
            const std::string diskPrefix = "categoryMaxDiskMb" + std::to_string(i) + "=";
            if (std::strncmp(line, diskPrefix.c_str(), diskPrefix.size()) == 0) {
                legacyCategoryLimits[static_cast<std::size_t>(i)].maxDiskMb =
                    std::clamp(std::atoi(line + diskPrefix.size()), 0, 102400);
            }
        }
        if (std::strncmp(line, "language=0", 10) == 0) settings.language = 0;
        if (std::strncmp(line, "language=1", 10) == 0) settings.language = 1;
        if (std::strncmp(line, "language=-1", 11) == 0) settings.language = -1;
        if (std::strncmp(line, "shortcutHistoryModifiers=", 25) == 0) {
            settings.historyHotkey.modifiers = static_cast<UINT>(std::atoi(line + 25));
        }
        if (std::strncmp(line, "shortcutHistoryKey=", 19) == 0) {
            settings.historyHotkey.virtualKey = static_cast<UINT>(std::atoi(line + 19));
        }
        if (std::strncmp(line, "shortcutSettingsModifiers=", 26) == 0) {
            settings.settingsHotkey.modifiers = static_cast<UINT>(std::atoi(line + 26));
        }
        if (std::strncmp(line, "shortcutSettingsKey=", 20) == 0) {
            settings.settingsHotkey.virtualKey = static_cast<UINT>(std::atoi(line + 20));
        }
        if (std::strncmp(line, "shortcutPauseModifiers=", 23) == 0) {
            settings.pauseHotkey.modifiers = static_cast<UINT>(std::atoi(line + 23));
        }
        if (std::strncmp(line, "shortcutPauseKey=", 17) == 0) {
            settings.pauseHotkey.virtualKey = static_cast<UINT>(std::atoi(line + 17));
        }
        readShortcutValue("shortcutPopupPasteModifiers=", settings.popupPasteHotkey.modifiers);
        readShortcutValue("shortcutPopupPasteKey=", settings.popupPasteHotkey.virtualKey);
        readShortcutValue("shortcutPopupPlainPasteModifiers=", settings.popupPlainPasteHotkey.modifiers);
        readShortcutValue("shortcutPopupPlainPasteKey=", settings.popupPlainPasteHotkey.virtualKey);
        readShortcutValue("shortcutPopupRichPasteModifiers=", settings.popupRichPasteHotkey.modifiers);
        readShortcutValue("shortcutPopupRichPasteKey=", settings.popupRichPasteHotkey.virtualKey);
        readShortcutValue("shortcutPopupCloseModifiers=", settings.popupCloseHotkey.modifiers);
        readShortcutValue("shortcutPopupCloseKey=", settings.popupCloseHotkey.virtualKey);
        readShortcutValue("shortcutPopupSettingsModifiers=", settings.popupSettingsHotkey.modifiers);
        readShortcutValue("shortcutPopupSettingsKey=", settings.popupSettingsHotkey.virtualKey);
        readShortcutValue("shortcutPopupClearFilterModifiers=", settings.popupClearFilterHotkey.modifiers);
        readShortcutValue("shortcutPopupClearFilterKey=", settings.popupClearFilterHotkey.virtualKey);
        readShortcutValue("shortcutPopupDeleteModifiers=", settings.popupDeleteHotkey.modifiers);
        readShortcutValue("shortcutPopupDeleteKey=", settings.popupDeleteHotkey.virtualKey);
    }
    std::fclose(file);
    const auto mergeLimit = [](const CategoryLimit& first, const CategoryLimit& second) {
        CategoryLimit result;
        result.maxItems = first.maxItems == 0 || second.maxItems == 0
            ? 0 : std::min(100000, first.maxItems + second.maxItems);
        result.maxDiskMb = first.maxDiskMb == 0 || second.maxDiskMb == 0
            ? 0 : std::min(102400, first.maxDiskMb + second.maxDiskMb);
        return result;
    };
    settings.categoryLimits[0] = mergeLimit(legacyCategoryLimits[0], legacyCategoryLimits[1]);
    settings.categoryLimits[1] = legacyCategoryLimits[2];
    settings.categoryLimits[2] = legacyCategoryLimits[3];
    if (settings.themeMode < 0) settings.themeMode = settings.dark ? 2 : 1;
    settings.dark = settings.themeMode == 2 ||
        (settings.themeMode == 0 && systemThemeIsDark());
    normalizeShortcutBinding(settings.historyHotkey, ShortcutBinding{MOD_ALT, 'V'});
    normalizeShortcutBinding(settings.settingsHotkey, ShortcutBinding{MOD_CONTROL | MOD_ALT, 'S'});
    normalizeShortcutBinding(settings.pauseHotkey, ShortcutBinding{MOD_CONTROL | MOD_SHIFT, 'P'});
    const auto normalizePopupShortcut = [](ShortcutBinding& binding, const ShortcutBinding& fallback) {
        binding.modifiers &= MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
        if (binding.virtualKey == 0 || binding.virtualKey > 0xFF) binding = fallback;
    };
    normalizePopupShortcut(settings.popupPasteHotkey, ShortcutBinding{0, VK_RETURN});
    normalizePopupShortcut(settings.popupPlainPasteHotkey, ShortcutBinding{MOD_CONTROL | MOD_SHIFT, 'V'});
    normalizePopupShortcut(settings.popupRichPasteHotkey, ShortcutBinding{MOD_CONTROL | MOD_SHIFT, 'R'});
    normalizePopupShortcut(settings.popupCloseHotkey, ShortcutBinding{0, VK_ESCAPE});
    normalizePopupShortcut(settings.popupSettingsHotkey, ShortcutBinding{0, VK_F10});
    normalizePopupShortcut(settings.popupClearFilterHotkey, ShortcutBinding{MOD_CONTROL, '0'});
    normalizePopupShortcut(settings.popupDeleteHotkey, ShortcutBinding{0, VK_DELETE});
}

void saveSettings(const Settings& settings) {
    std::ostringstream output;
    output << "winV=" << (settings.winV ? 1 : 0) << "\n"
           << "dark=" << (settings.dark ? 1 : 0) << "\n"
           << "themeMode=" << settings.themeMode << "\n"
           << "accent=" << settings.accent << "\n"
           << "pauseMonitoring=" << (settings.pauseMonitoring ? 1 : 0) << "\n"
           << "startWithWindows=" << (settings.startWithWindows ? 1 : 0) << "\n"
           << "showSettingsOnStartup=" << (settings.showSettingsOnStartup ? 1 : 0) << "\n"
           << "showStartupNotification=" << (settings.showStartupNotification ? 1 : 0) << "\n"
           << "searchImeCompatibility=" << (settings.searchImeCompatibility ? 1 : 0) << "\n"
           << "promotePastedItem=" << (settings.promotePastedItem ? 1 : 0) << "\n"
           << "historyWindowPinned=" << (settings.historyWindowPinned ? 1 : 0) << "\n"
           << "runAsAdministrator=" << (settings.runAsAdministrator ? 1 : 0) << "\n"
           << "encryptData=" << (settings.encryptData ? 1 : 0) << "\n"
           << "maxItems=" << settings.maxItems << "\n"
           << "retentionDays=" << settings.retentionDays << "\n"
           << "maxDiskMb=" << settings.maxDiskMb << "\n"
           << "maxContentMb=" << settings.maxContentMb << "\n"
           << "dataDirectory=" << settings.dataDirectory << "\n"
           << "sensitiveExpiryHours=" << settings.sensitiveExpiryHours << "\n"
           << "language=" << settings.language << "\n"
           << "shortcutHistoryModifiers=" << settings.historyHotkey.modifiers << "\n"
           << "shortcutHistoryKey=" << settings.historyHotkey.virtualKey << "\n"
           << "shortcutSettingsModifiers=" << settings.settingsHotkey.modifiers << "\n"
           << "shortcutSettingsKey=" << settings.settingsHotkey.virtualKey << "\n"
           << "shortcutPauseModifiers=" << settings.pauseHotkey.modifiers << "\n"
           << "shortcutPauseKey=" << settings.pauseHotkey.virtualKey << "\n"
           << "shortcutPopupPasteModifiers=" << settings.popupPasteHotkey.modifiers << "\n"
           << "shortcutPopupPasteKey=" << settings.popupPasteHotkey.virtualKey << "\n"
           << "shortcutPopupPlainPasteModifiers=" << settings.popupPlainPasteHotkey.modifiers << "\n"
           << "shortcutPopupPlainPasteKey=" << settings.popupPlainPasteHotkey.virtualKey << "\n"
           << "shortcutPopupRichPasteModifiers=" << settings.popupRichPasteHotkey.modifiers << "\n"
           << "shortcutPopupRichPasteKey=" << settings.popupRichPasteHotkey.virtualKey << "\n"
           << "shortcutPopupCloseModifiers=" << settings.popupCloseHotkey.modifiers << "\n"
           << "shortcutPopupCloseKey=" << settings.popupCloseHotkey.virtualKey << "\n"
           << "shortcutPopupSettingsModifiers=" << settings.popupSettingsHotkey.modifiers << "\n"
           << "shortcutPopupSettingsKey=" << settings.popupSettingsHotkey.virtualKey << "\n"
           << "shortcutPopupClearFilterModifiers=" << settings.popupClearFilterHotkey.modifiers << "\n"
           << "shortcutPopupClearFilterKey=" << settings.popupClearFilterHotkey.virtualKey << "\n"
           << "shortcutPopupDeleteModifiers=" << settings.popupDeleteHotkey.modifiers << "\n"
           << "shortcutPopupDeleteKey=" << settings.popupDeleteHotkey.virtualKey << "\n";
    for (const std::string& app : settings.ignoredApps) output << "ignoredApp=" << app << "\n";
    for (int i = 0; i < 4; ++i) {
        output << "category" << i << "=" << settings.categories[static_cast<std::size_t>(i)] << "\n";
        if (i < kStorageCategoryCount) {
            const CategoryLimit& limit = settings.categoryLimits[static_cast<std::size_t>(i)];
            output << "categoryMaxItems" << i << "=" << limit.maxItems << "\n"
                   << "categoryMaxDiskMb" << i << "=" << limit.maxDiskMb << "\n";
        }
    }
    const std::string contents = output.str();
    const std::wstring tempPath = settingsPath() + L".tmp";
    std::FILE* file = nullptr;
    _wfopen_s(&file, tempPath.c_str(), L"wb");
    if (!file) {
        appendDiagnosticLog("ERROR", "settings: unable to write settings file", GetLastError());
        return;
    }
    const bool written = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    const bool flushed = written && std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !closed ||
        !MoveFileExW(tempPath.c_str(), settingsPath().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        appendDiagnosticLog("ERROR", "settings: atomic replace failed", GetLastError());
    }
}

bool processIsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation,
                                               sizeof(elevation), &size) != FALSE &&
        elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

bool launchElevatedRestart(const wchar_t* arguments) {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    if (length == 0 || length >= ARRAYSIZE(executable)) return false;
    SHELLEXECUTEINFOW execute{sizeof(execute)};
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = executable;
    execute.lpParameters = arguments;
    execute.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&execute)) {
        appendDiagnosticLog("WARN", "admin: unable to launch elevated restart", GetLastError());
        return false;
    }
    if (execute.hProcess) CloseHandle(execute.hProcess);
    return true;
}

void updateStartupRegistration(bool enabled) {
    HKEY key = nullptr;
    const LONG result = RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) {
        appendDiagnosticLog("ERROR", "startup: unable to update Windows startup registration",
                            static_cast<DWORD>(result));
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

bool isIgnoredClipboardSource(const std::string& source) {
    if (source.empty()) return false;
    for (const std::string& ignored : g_app->settingsData.ignoredApps) {
        if (_stricmp(source.c_str(), ignored.c_str()) == 0) return true;
    }
    return false;
}

bool containsSensitiveMarker(ClipType type, const std::string& payload) {
    if (type != ClipType::Text && type != ClipType::Html) return false;
    std::string sample = payload.substr(0, 8192);
    for (char& character : sample) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    const char* markers[] = {"password=", "passwd=", "token=", "bearer ",
                             "api_key=", "apikey=", "secret=", "private key"};
    for (const char* marker : markers) {
        if (sample.find(marker) != std::string::npos) return true;
    }
    return sample.find("-----begin ") != std::string::npos;
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
    const std::size_t contentSize = payload.size() - dataOffset;
    if (header.magic != kStoredHtmlMagic || header.textSize > contentSize ||
        header.htmlSize > contentSize - header.textSize ||
        static_cast<std::size_t>(header.textSize) + header.htmlSize != contentSize) {
        html = payload;
        return !html.empty();
    }
    text.assign(payload.data() + dataOffset, header.textSize);
    html.assign(payload.data() + dataOffset + header.textSize, header.htmlSize);
    return !html.empty();
}

std::uint64_t clipboardDedupHash(ClipType type, const std::string& payload) {
    if (type != ClipType::Html) return clipLiteHash(payload);
    std::string text;
    std::string html;
    if (splitStoredHtml(payload, text, html) && !text.empty()) return clipLiteHash(text);
    return clipLiteHash(payload);
}

bool openClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(owner)) return true;
        Sleep(5);
    }
    return false;
}

struct DibLayout {
    std::size_t bitsOffset = 0;
    std::size_t rowBytes = 0;
    std::size_t pixelBytes = 0;
};

bool validateDibPayload(const std::string& payload, DibLayout* layout) {
    if (payload.size() < sizeof(BITMAPINFOHEADER)) return false;
    BITMAPINFOHEADER header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > sizeof(BITMAPV5HEADER) ||
        header.biSize > payload.size() || header.biWidth <= 0 || header.biHeight == 0 ||
        header.biHeight == std::numeric_limits<LONG>::min() || header.biPlanes != 1 ||
        header.biWidth > 10000 || std::llabs(static_cast<long long>(header.biHeight)) > 10000) {
        return false;
    }
    if (header.biBitCount != 1 && header.biBitCount != 4 && header.biBitCount != 8 &&
        header.biBitCount != 16 && header.biBitCount != 24 && header.biBitCount != 32) {
        return false;
    }
    const bool bitfields = header.biCompression == BI_BITFIELDS;
    constexpr DWORD kBiAlphaBitfields = 6;
    const bool alphaBitfields = header.biCompression == kBiAlphaBitfields;
    if (header.biCompression != BI_RGB && !bitfields && !alphaBitfields) return false;
    if ((bitfields || alphaBitfields) && header.biBitCount != 16 && header.biBitCount != 32) {
        return false;
    }
    if (alphaBitfields && header.biSize < sizeof(BITMAPV5HEADER)) return false;
    const std::size_t paletteCount = header.biBitCount <= 8
        ? (header.biClrUsed == 0 ? (std::size_t{1} << header.biBitCount) : header.biClrUsed) : 0;
    if (paletteCount > 256) return false;
    const std::size_t maskBytes = (bitfields || alphaBitfields) &&
        header.biSize == sizeof(BITMAPINFOHEADER) ? (alphaBitfields ? 16 : 12) : 0;
    if (paletteCount > (std::numeric_limits<std::size_t>::max() - header.biSize) / sizeof(RGBQUAD)) {
        return false;
    }
    const std::size_t paletteBytes = paletteCount * sizeof(RGBQUAD);
    if (maskBytes > std::numeric_limits<std::size_t>::max() - header.biSize - paletteBytes) {
        return false;
    }
    const std::size_t bitsOffset = header.biSize + paletteBytes + maskBytes;
    const std::uint64_t bitsPerRow = static_cast<std::uint64_t>(header.biWidth) * header.biBitCount;
    const std::uint64_t rowBytes = ((bitsPerRow + 31u) / 32u) * 4u;
    const std::uint64_t pixelBytes = rowBytes * static_cast<std::uint64_t>(
        std::llabs(static_cast<long long>(header.biHeight)));
    if (rowBytes == 0 || pixelBytes > std::numeric_limits<std::size_t>::max() ||
        bitsOffset > payload.size() || pixelBytes > payload.size() - bitsOffset ||
        (header.biSizeImage != 0 && header.biSizeImage < pixelBytes)) return false;
    if (layout) {
        layout->bitsOffset = bitsOffset;
        layout->rowBytes = static_cast<std::size_t>(rowBytes);
        layout->pixelBytes = static_cast<std::size_t>(pixelBytes);
    }
    return true;
}

bool isValidDibPayload(const std::string& payload) {
    return validateDibPayload(payload, nullptr);
}

bool captureClipboard(HWND owner, ClipType& type, std::string& payload, std::string& source) {
    source = clipboardSource();
    if (!openClipboardWithRetry(owner)) return false;

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

void startClipboardCapture() {
    if (!g_app || g_app->settingsData.pauseMonitoring || !g_app->hidden) return;
    const std::shared_ptr<std::atomic<bool>> running = g_app->clipboardCaptureRunning;
    if (running->exchange(true, std::memory_order_acq_rel)) return;
    g_app->clipboardCapturePending = false;
    const HWND hidden = g_app->hidden;
    try {
        std::thread([hidden, running] {
            auto result = std::make_unique<ClipboardCaptureResult>();
            result->captured = captureClipboard(hidden, result->type, result->payload,
                                                result->source);
            ClipboardCaptureResult* raw = result.release();
            if (!PostMessageW(hidden, kClipboardCaptureCompleteMessage,
                              reinterpret_cast<WPARAM>(raw), 0)) {
                delete raw;
            }
            running->store(false, std::memory_order_release);
        }).detach();
    } catch (...) {
        running->store(false, std::memory_order_release);
    }
}

bool setClipboardDataForItem(const ClipItem& item, const std::string& payload,
                              PasteMode mode = PasteMode::Automatic) {
    if (!openClipboardWithRetry(g_app->hidden)) return false;
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }
    HGLOBAL memory = nullptr;
    bool ok = false;

    const auto setUnicodeText = [&](const std::string& value) {
        const std::wstring text = utf8ToWide(value);
        HGLOBAL textMemory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (!textMemory) return false;
        void* destination = GlobalLock(textMemory);
        if (!destination) {
            GlobalFree(textMemory);
            return false;
        }
        std::memcpy(destination, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(textMemory);
        if (SetClipboardData(CF_UNICODETEXT, textMemory)) return true;
        GlobalFree(textMemory);
        return false;
    };

    const auto setHtml = [&](const std::string& value) {
        const UINT format = htmlClipboardFormat();
        if (format == 0) return false;
        HGLOBAL htmlMemory = GlobalAlloc(GMEM_MOVEABLE, value.size());
        if (!htmlMemory) return false;
        void* destination = GlobalLock(htmlMemory);
        if (!destination) {
            GlobalFree(htmlMemory);
            return false;
        }
        std::memcpy(destination, value.data(), value.size());
        GlobalUnlock(htmlMemory);
        if (SetClipboardData(format, htmlMemory)) return true;
        GlobalFree(htmlMemory);
        return false;
    };

    if (item.type == ClipType::Text) {
        ok = setUnicodeText(payload);
    } else if (item.type == ClipType::Image || item.type == ClipType::ImageV5) {
        memory = GlobalAlloc(GMEM_MOVEABLE, payload.size());
        if (memory) {
            void* destination = GlobalLock(memory);
            if (!destination) {
                GlobalFree(memory);
            } else {
                std::memcpy(destination, payload.data(), payload.size());
                GlobalUnlock(memory);
                const UINT format = item.type == ClipType::ImageV5 ? CF_DIBV5 : CF_DIB;
                if (SetClipboardData(format, memory)) ok = true;
                else GlobalFree(memory);
            }
        }
    } else if (item.type == ClipType::Html) {
        std::string plainText;
        std::string html;
        if (splitStoredHtml(payload, plainText, html)) {
            if (mode == PasteMode::PlainText) {
                ok = setUnicodeText(plainText);
            } else {
                ok = setHtml(html);
                if (!plainText.empty() || !ok) ok = setUnicodeText(plainText) || ok;
            }
        }
    } else {
        std::wstring paths = utf8ToWide(payload);
        std::replace(paths.begin(), paths.end(), L'\n', L'\0');
        const std::size_t bytes = sizeof(DROPFILES) + (paths.size() + 2) * sizeof(wchar_t);
        memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (memory) {
            auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
            if (!drop) {
                GlobalFree(memory);
            } else {
                drop->pFiles = sizeof(DROPFILES);
                drop->fWide = TRUE;
                std::memcpy(reinterpret_cast<char*>(drop) + sizeof(DROPFILES), paths.c_str(),
                            (paths.size() + 1) * sizeof(wchar_t));
                GlobalUnlock(memory);
                if (SetClipboardData(CF_HDROP, memory)) ok = true;
                else GlobalFree(memory);
            }
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
    if (filterType == 1) return type == ClipType::Text || type == ClipType::Html;
    if (filterType == 2) return isImageType(type);
    if (filterType == 3) return type == ClipType::Files;
    return type != ClipType::Text && !isImageType(type) &&
           type != ClipType::Files && type != ClipType::Html;
}

const wchar_t* automaticTypeLabel(ClipType type) {
    if (type == ClipType::Text || type == ClipType::Html) return settingsLocale().popupTextType;
    if (isImageType(type)) return settingsLocale().popupImageType;
    if (type == ClipType::Files) return settingsLocale().popupFilesType;
    return settingsLocale().popupOtherType;
}

std::wstring localizedPopupPreview(const ClipItem& item) {
    std::wstring preview = utf8ToWide(item.preview);
    if (preview.empty() || preview == L"[Empty]") return settingsLocale().popupEmptyPreview;
    if (preview == L"[Image]") return settingsLocale().popupImagePreview;
    if (preview == L"[HTML]") return settingsLocale().popupHtmlPreview;
    if (preview == L"[Invalid text]") return settingsLocale().popupInvalidText;
    constexpr wchar_t filesPrefix[] = L"[Files] ";
    if (preview.rfind(filesPrefix, 0) == 0) {
        return std::wstring(settingsLocale().popupFilesPreview) + preview.substr(
            sizeof(filesPrefix) / sizeof(filesPrefix[0]) - 1);
    }
    return preview;
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

int popupScrollStride();

int popupRowAt(int clickY) {
    if (!g_app || !g_app->popup) return -1;
    RECT client{};
    GetClientRect(g_app->popup, &client);
    if (clickY < ui(kPopupListTop) || clickY >= client.bottom - ui(kPopupBottomPadding)) return -1;
    const int stride = popupScrollStride();
    const int firstRow = g_app->scrollPosition / std::max(1, stride);
    const int offset = g_app->scrollPosition % std::max(1, stride);
    int y = ui(kPopupListTop) - offset;
    for (int row = firstRow;
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
    if (!g_app) return -1;
    const int stride = popupScrollStride();
    const int firstRow = g_app->scrollPosition / std::max(1, stride);
    if (row < firstRow) return -1;
    const int offset = g_app->scrollPosition % std::max(1, stride);
    return ui(kPopupListTop) - offset + (row - firstRow) * stride;
}

RECT automaticFilterRect(int slot, int scrollOffset);

int popupFilterAt(int x, int y) {
    if (y < ui(kPopupFilterTop) || y >= ui(kPopupFilterBottom)) return -1;
    for (int slot = 0; slot < 6; ++slot) {
        const RECT rect = automaticFilterRect(slot, g_app->filterScrollOffset);
        if (x >= rect.left && x < rect.right) return slot;
    }
    return -1;
}

bool popupPointInHeader(const RECT& client, int x, int y) {
    return y >= ui(12) && y < ui(46) && x >= ui(16) && x < ui(84) &&
           x < client.right - ui(54);
}

void invalidatePopupHover(HWND hwnd, int row, int filter, bool header) {
    RECT client{};
    GetClientRect(hwnd, &client);
    if (row >= 0 && row < static_cast<int>(g_app->visible.size())) {
        const int top = popupRowTop(row);
        const ClipItem& item = g_app->store.items()[g_app->visible[static_cast<std::size_t>(row)]];
        RECT rowRect{ui(16), top, client.right - ui(16), top + popupCardHeight(item)};
        InvalidateRect(hwnd, &rowRect, FALSE);
    }
    if (filter >= 0 && filter < 6) {
        RECT filterRect = automaticFilterRect(filter, g_app->filterScrollOffset);
        InflateRect(&filterRect, ui(2), ui(2));
        InvalidateRect(hwnd, &filterRect, FALSE);
    } else if (filter >= 7 && filter <= 9) {
        RECT buttonRect{};
        if (filter == 7) {
            buttonRect = RECT{ui(kPopupClearLeft), ui(12), ui(kPopupClearRight), ui(46)};
        } else if (filter == 8) {
            buttonRect = RECT{ui(kPopupPinLeft), ui(12), ui(kPopupPinRight), ui(46)};
        } else {
            buttonRect = RECT{ui(kPopupCloseLeft), ui(12), client.right - ui(16), ui(46)};
        }
        InvalidateRect(hwnd, &buttonRect, FALSE);
    }
    if (header) {
        RECT headerRect{ui(12), ui(10), ui(86), ui(50)};
        InvalidateRect(hwnd, &headerRect, FALSE);
    }
}

void invalidateSettingsNav(HWND hwnd, int tab) {
    if (tab < 0 || tab > 4) return;
    RECT rect{ui(8), ui(50 + tab * 38), ui(180), ui(50 + tab * 38 + 38)};
    InvalidateRect(hwnd, &rect, FALSE);
}

void invalidatePopupList(HWND hwnd) {
    if (!hwnd) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT listRect{ui(kPopupListTop), ui(kPopupListTop), client.right,
                  client.bottom - ui(kPopupBottomPadding)};
    listRect.left = ui(12);
    InvalidateRect(hwnd, &listRect, FALSE);
}

void invalidateFilterBar(HWND hwnd);

int popupScrollStride() {
    return ui(kPopupCardHeight + kPopupCardGap);
}

int popupMaxScrollPixels() {
    if (!g_app || !g_app->popup) return 0;
    return std::max(0, static_cast<int>(g_app->visible.size()) - popupVisibleRows()) *
        popupScrollStride();
}

void setPopupScrollPosition(int position) {
    if (!g_app) return;
    const int maximum = popupMaxScrollPixels();
    g_app->scrollPosition = std::clamp(position, 0, maximum);
    g_app->scrollOffset = g_app->scrollPosition / std::max(1, popupScrollStride());
}

bool popupScrollMetrics(int& trackTop, int& trackBottom, int& thumbTop,
                        int& thumbHeight, int& maxOffset) {
    if (!g_app || !g_app->popup) return false;
    RECT client{};
    GetClientRect(g_app->popup, &client);
    const int visibleRows = popupVisibleRows();
    const int itemCount = static_cast<int>(g_app->visible.size());
    maxOffset = std::max(0, itemCount - visibleRows) * popupScrollStride();
    if (maxOffset == 0) return false;
    trackTop = ui(kPopupListTop);
    trackBottom = client.bottom - ui(kPopupBottomPadding);
    const int trackHeight = std::max(1, trackBottom - trackTop);
    thumbHeight = std::min(trackHeight,
                           std::max(ui(24), trackHeight * visibleRows / std::max(1, itemCount)));
    thumbTop = trackTop + (trackHeight - thumbHeight) * g_app->scrollPosition /
        std::max(1, maxOffset);
    return true;
}

bool popupScrollThumbAt(int x, int y) {
    if (!g_app || !g_app->popup) return false;
    RECT client{};
    GetClientRect(g_app->popup, &client);
    int trackTop = 0;
    int trackBottom = 0;
    int thumbTop = 0;
    int thumbHeight = 0;
    int maxOffset = 0;
    if (!popupScrollMetrics(trackTop, trackBottom, thumbTop, thumbHeight, maxOffset)) return false;
    (void)trackTop;
    (void)trackBottom;
    (void)maxOffset;
    return x >= client.right - ui(12) && x < client.right - ui(1) &&
           y >= thumbTop && y < thumbTop + thumbHeight;
}

Gdiplus::Color makeGdiColor(COLORREF value) {
    return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
}

void configureGdiGraphics(Gdiplus::Graphics& graphics) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
}

void addGdiRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    const float boundedRadius = std::min(radius, std::min(rect.Width, rect.Height) / 2.0f);
    const float diameter = boundedRadius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter,
                diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void drawGdiLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, float width = 1.0f) {
    if (!g_app || g_app->gdiplusToken == 0) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    Gdiplus::Pen pen(makeGdiColor(color), width);
    graphics.DrawLine(&pen, static_cast<float>(x1), static_cast<float>(y1),
                      static_cast<float>(x2), static_cast<float>(y2));
}

void drawGdiRoundedSurface(HDC dc, const RECT& rect, COLORREF fill, COLORREF border, int radius) {
    if (!g_app || g_app->gdiplusToken == 0) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    Gdiplus::GraphicsPath path;
    addGdiRoundedRect(path, Gdiplus::RectF(
        static_cast<float>(rect.left) + 0.5f, static_cast<float>(rect.top) + 0.5f,
        static_cast<float>(rect.right - rect.left - 1),
        static_cast<float>(rect.bottom - rect.top - 1)), static_cast<float>(ui(radius)));
    Gdiplus::SolidBrush brush(makeGdiColor(fill));
    Gdiplus::Pen pen(makeGdiColor(border), 1.0f);
    graphics.FillPath(&brush, &path);
    graphics.DrawPath(&pen, &path);
}

void drawPinIcon(HDC dc, int right, int centerY, COLORREF color, bool filled = false) {
    if (!g_app || g_app->gdiplusToken == 0) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    const float scale = static_cast<float>(g_uiDpi) / 96.0f * (10.0f / 14.0f);
    const float originX = static_cast<float>(right) - 19.0f * scale;
    const float originY = static_cast<float>(centerY) - 12.0f * scale;
    const auto px = [originX, scale](float value) { return originX + value * scale; };
    const auto py = [originY, scale](float value) { return originY + value * scale; };
    Gdiplus::Pen pen(makeGdiColor(color), 1.5f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    graphics.DrawLine(&pen, px(12.0f), py(17.0f), px(12.0f), py(22.0f));

    Gdiplus::GraphicsPath path;
    path.StartFigure();
    path.AddBezier(px(9.0f), py(10.76f), px(9.0f), py(11.5f),
                   px(8.58f), py(12.2f), px(7.89f), py(12.55f));
    path.AddLine(px(7.89f), py(12.55f), px(6.11f), py(13.45f));
    path.AddBezier(px(6.11f), py(13.45f), px(5.42f), py(13.8f),
                   px(5.0f), py(14.5f), px(5.0f), py(15.24f));
    path.AddLine(px(5.0f), py(15.24f), px(5.0f), py(16.0f));
    path.AddBezier(px(5.0f), py(16.0f), px(5.0f), py(16.55f),
                   px(5.45f), py(17.0f), px(6.0f), py(17.0f));
    path.AddLine(px(6.0f), py(17.0f), px(18.0f), py(17.0f));
    path.AddBezier(px(18.0f), py(17.0f), px(18.55f), py(17.0f),
                   px(19.0f), py(16.55f), px(19.0f), py(16.0f));
    path.AddLine(px(19.0f), py(16.0f), px(19.0f), py(15.24f));
    path.AddBezier(px(19.0f), py(15.24f), px(19.0f), py(14.5f),
                   px(18.58f), py(13.8f), px(17.89f), py(13.45f));
    path.AddLine(px(17.89f), py(13.45f), px(16.11f), py(12.55f));
    path.AddBezier(px(16.11f), py(12.55f), px(15.42f), py(12.2f),
                   px(15.0f), py(11.5f), px(15.0f), py(10.76f));
    path.AddLine(px(15.0f), py(10.76f), px(15.0f), py(6.0f));
    path.AddLine(px(15.0f), py(6.0f), px(16.0f), py(6.0f));
    path.AddBezier(px(16.0f), py(6.0f), px(17.1f), py(6.0f),
                   px(18.0f), py(5.1f), px(18.0f), py(4.0f));
    path.AddBezier(px(18.0f), py(4.0f), px(18.0f), py(2.9f),
                   px(17.1f), py(2.0f), px(16.0f), py(2.0f));
    path.AddLine(px(16.0f), py(2.0f), px(8.0f), py(2.0f));
    path.AddBezier(px(8.0f), py(2.0f), px(6.9f), py(2.0f),
                   px(6.0f), py(2.9f), px(6.0f), py(4.0f));
    path.AddBezier(px(6.0f), py(4.0f), px(6.0f), py(5.1f),
                   px(6.9f), py(6.0f), px(8.0f), py(6.0f));
    path.AddLine(px(8.0f), py(6.0f), px(9.0f), py(6.0f));
    path.CloseFigure();
    if (filled) {
        Gdiplus::SolidBrush brush(makeGdiColor(color));
        graphics.FillPath(&brush, &path);
    }
    graphics.DrawPath(&pen, &path);
}

void drawEmptyClipboardIcon(HDC dc, int centerX, int top, COLORREF color) {
    if (!g_app || g_app->gdiplusToken == 0) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    Gdiplus::Pen pen(makeGdiColor(color), 1.0f);
    Gdiplus::GraphicsPath body;
    addGdiRoundedRect(body, Gdiplus::RectF(
        static_cast<float>(centerX - ui(14)), static_cast<float>(top + ui(6)),
        static_cast<float>(ui(28)), static_cast<float>(ui(32))), static_cast<float>(ui(4)));
    graphics.DrawPath(&pen, &body);
    Gdiplus::GraphicsPath clip;
    addGdiRoundedRect(clip, Gdiplus::RectF(
        static_cast<float>(centerX - ui(7)), static_cast<float>(top),
        static_cast<float>(ui(14)), static_cast<float>(ui(10))), static_cast<float>(ui(3)));
    graphics.DrawPath(&pen, &clip);
    graphics.DrawLine(&pen, static_cast<float>(centerX - ui(7)), static_cast<float>(top + ui(20)),
                      static_cast<float>(centerX + ui(7)), static_cast<float>(top + ui(20)));
    graphics.DrawLine(&pen, static_cast<float>(centerX - ui(7)), static_cast<float>(top + ui(27)),
                      static_cast<float>(centerX + ui(3)), static_cast<float>(top + ui(27)));
}

void drawDeleteIcon(HDC dc, int x, int y, COLORREF color) {
    if (!g_app || g_app->gdiplusToken == 0) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    Gdiplus::Pen pen(makeGdiColor(color), 1.0f);
    graphics.DrawLine(&pen, static_cast<float>(x), static_cast<float>(y),
                      static_cast<float>(x + ui(10)), static_cast<float>(y + ui(10)));
    graphics.DrawLine(&pen, static_cast<float>(x + ui(10)), static_cast<float>(y),
                      static_cast<float>(x), static_cast<float>(y + ui(10)));
}

void drawSearchIcon(HDC dc, int centerX, int centerY, COLORREF color) {
    if (!g_app || g_app->gdiplusToken == 0) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    const float scale = static_cast<float>(g_uiDpi) / 96.0f;
    const float x = static_cast<float>(centerX);
    const float y = static_cast<float>(centerY);
    Gdiplus::Pen pen(makeGdiColor(color), 1.5f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawEllipse(&pen, x - 5.0f * scale, y - 6.0f * scale,
                         10.0f * scale, 10.0f * scale);
    graphics.DrawLine(&pen, x + 3.0f * scale, y + 4.0f * scale,
                      x + 7.0f * scale, y + 8.0f * scale);
}

void drawMetadataTag(HDC dc, const RECT& rect, const std::wstring& value,
                     COLORREF background, COLORREF border, COLORREF text, int radius) {
    if (!g_app || g_app->gdiplusToken == 0) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    Gdiplus::GraphicsPath path;
    addGdiRoundedRect(path, Gdiplus::RectF(
        static_cast<float>(rect.left) + 0.5f, static_cast<float>(rect.top) + 0.5f,
        static_cast<float>(rect.right - rect.left - 1),
        static_cast<float>(rect.bottom - rect.top - 1)), static_cast<float>(ui(radius)));
    Gdiplus::SolidBrush brush(makeGdiColor(background));
    Gdiplus::Pen pen(makeGdiColor(border), 1.0f);
    graphics.FillPath(&brush, &path);
    graphics.DrawPath(&pen, &path);
    SetTextColor(dc, text);
    RECT textRect = rect;
    DrawTextW(dc, value.c_str(), -1, &textRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void drawSettingsNavIcon(HDC dc, int index, int x, int y, COLORREF color) {
    if (!g_app || g_app->gdiplusToken == 0 || index < 0 || index > 4) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    const float scale = static_cast<float>(g_uiDpi) / 96.0f;
    const auto px = [x, scale](float value) { return static_cast<float>(x) + value * scale; };
    const auto py = [y, scale](float value) { return static_cast<float>(y) + value * scale; };
    Gdiplus::Pen pen(makeGdiColor(color), 1.5f * scale);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    if (index == 0) {
        const float lines[] = {7.0f, 12.0f, 17.0f};
        const float knobs[] = {9.0f, 15.0f, 11.0f};
        for (int row = 0; row < 3; ++row) {
            graphics.DrawLine(&pen, px(4.0f), py(lines[row]), px(20.0f), py(lines[row]));
            graphics.DrawEllipse(&pen, px(knobs[row] - 2.0f), py(lines[row] - 2.0f),
                                 4.0f * scale, 4.0f * scale);
        }
    } else if (index == 1) {
        Gdiplus::GraphicsPath keyboard;
        addGdiRoundedRect(keyboard, Gdiplus::RectF(px(3.5f), py(5.5f),
                                                   17.0f * scale, 13.0f * scale), 2.0f * scale);
        graphics.DrawPath(&pen, &keyboard);
        const float firstRow[] = {6.5f, 10.5f, 14.5f, 17.5f};
        for (const float key : firstRow) {
            graphics.DrawLine(&pen, px(key), py(9.5f), px(key + 1.5f), py(9.5f));
            graphics.DrawLine(&pen, px(key), py(13.0f), px(key + 1.5f), py(13.0f));
        }
        graphics.DrawLine(&pen, px(8.5f), py(16.5f), px(15.5f), py(16.5f));
    } else if (index == 2) {
        Gdiplus::GraphicsPath top;
        top.AddEllipse(px(4.0f), py(3.0f), 16.0f * scale, 5.0f * scale);
        graphics.DrawPath(&pen, &top);
        Gdiplus::GraphicsPath body;
        body.StartFigure();
        body.AddLine(px(4.0f), py(5.5f), px(4.0f), py(18.5f));
        body.AddBezier(px(4.0f), py(18.5f), px(4.0f), py(19.88f),
                       px(7.58f), py(21.0f), px(12.0f), py(21.0f));
        body.AddBezier(px(12.0f), py(21.0f), px(16.42f), py(21.0f),
                       px(20.0f), py(19.88f), px(20.0f), py(18.5f));
        body.AddLine(px(20.0f), py(18.5f), px(20.0f), py(5.5f));
        graphics.DrawPath(&pen, &body);
        Gdiplus::GraphicsPath middle;
        middle.AddBezier(px(4.0f), py(12.0f), px(4.0f), py(13.38f),
                         px(7.58f), py(14.5f), px(12.0f), py(14.5f));
        middle.AddBezier(px(12.0f), py(14.5f), px(16.42f), py(14.5f),
                         px(20.0f), py(13.38f), px(20.0f), py(12.0f));
        graphics.DrawPath(&pen, &middle);
    } else if (index == 3) {
        Gdiplus::GraphicsPath shield;
        shield.AddLine(px(12.0f), py(3.0f), px(18.5f), py(5.5f));
        shield.AddLine(px(18.5f), py(5.5f), px(18.5f), py(10.5f));
        shield.AddBezier(px(18.5f), py(10.5f), px(18.5f), py(14.8f),
                         px(16.0f), py(17.8f), px(12.0f), py(19.5f));
        shield.AddBezier(px(12.0f), py(19.5f), px(8.0f), py(17.8f),
                         px(5.5f), py(14.8f), px(5.5f), py(10.5f));
        shield.AddLine(px(5.5f), py(10.5f), px(5.5f), py(5.5f));
        shield.CloseFigure();
        graphics.DrawPath(&pen, &shield);
        Gdiplus::GraphicsPath lock;
        addGdiRoundedRect(lock, Gdiplus::RectF(px(9.5f), py(10.0f),
                                               5.0f * scale, 4.0f * scale), 1.0f * scale);
        graphics.DrawPath(&pen, &lock);
        Gdiplus::GraphicsPath shackle;
        shackle.StartFigure();
        shackle.AddLine(px(10.5f), py(10.0f), px(10.5f), py(8.5f));
        shackle.AddBezier(px(10.5f), py(8.5f), px(10.5f), py(7.5f),
                          px(11.0f), py(6.5f), px(12.0f), py(6.5f));
        shackle.AddBezier(px(12.0f), py(6.5f), px(13.0f), py(6.5f),
                          px(13.5f), py(7.5f), px(13.5f), py(8.5f));
        shackle.AddLine(px(13.5f), py(8.5f), px(13.5f), py(10.0f));
        graphics.DrawPath(&pen, &shackle);
    } else {
        graphics.DrawEllipse(&pen, px(4.0f), py(4.0f), 16.0f * scale, 16.0f * scale);
        graphics.DrawLine(&pen, px(12.0f), py(11.5f), px(12.0f), py(15.5f));
        Gdiplus::SolidBrush dot(makeGdiColor(color));
        graphics.FillEllipse(&dot, px(11.5f), py(8.0f), scale, scale);
    }
}

void drawSettingsThemeIcon(HDC dc, int mode, int centerX, int centerY, COLORREF color) {
    const wchar_t* glyphs[] = {L"\u25D0", L"\u2600", L"\u263E"};
    HFONT font = CreateFontW(-ui(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
    if (!font) return;
    HGDIOBJ oldFont = SelectObject(dc, font);
    const int radius = ui(9);
    RECT rect{centerX - radius, centerY - radius, centerX + radius, centerY + radius};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, glyphs[std::clamp(mode, 0, 2)], -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void drawSettingsAccentDot(HDC dc, int left, int top, int size,
                           COLORREF color, bool selected, COLORREF ringColor) {
    if (!g_app || g_app->gdiplusToken == 0) return;
        auto makeColor = [](COLORREF value) {
            return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
        };
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        Gdiplus::SolidBrush brush(makeColor(color));
        const Gdiplus::RectF dot(static_cast<float>(left) + 0.5f,
                                 static_cast<float>(top) + 0.5f,
                                 static_cast<float>(size) - 1.0f,
                                 static_cast<float>(size) - 1.0f);
        graphics.FillEllipse(&brush, dot);
        if (selected) {
            Gdiplus::Pen ring(makeColor(ringColor), 1.2f);
            const Gdiplus::RectF outline(static_cast<float>(left) - 1.5f,
                                         static_cast<float>(top) - 1.5f,
                                         static_cast<float>(size) + 3.0f,
                                         static_cast<float>(size) + 3.0f);
            graphics.DrawEllipse(&ring, outline);
        }
}

void cancelPopupSearch() {
    if (!g_app) return;
    if (g_app->searchCancellation) {
        g_app->searchCancellation->store(true, std::memory_order_relaxed);
    }
    if (g_app->searchWorker.joinable()) g_app->searchWorker.join();
    g_app->searchCancellation.reset();
    ++g_app->searchGeneration;
}

void applyVisibleCandidates(const std::vector<std::size_t>& candidates,
                            bool preserveScrollPosition = false) {
    if (!g_app || !g_app->popup) return;
    const int previousScrollPosition = g_app->scrollPosition;
    g_app->visible.clear();
    for (const std::size_t index : candidates) {
        if (index >= g_app->store.items().size()) continue;
        const ClipItem& item = g_app->store.items()[index];
        const bool typeMatches = isAutomaticTypeMatch(g_app->filterType, item.type);
        if (typeMatches && (!g_app->pinnedOnly || item.pinned)) {
            g_app->visible.push_back(index);
        }
    }
    if (g_app->visible.empty()) g_app->selected = 0;
    else g_app->selected = std::clamp(g_app->selected, 0, static_cast<int>(g_app->visible.size()) - 1);
    if (preserveScrollPosition) {
        setPopupScrollPosition(previousScrollPosition);
    } else {
        const int visibleRows = popupVisibleRows();
        const int maxOffset = std::max(0, static_cast<int>(g_app->visible.size()) - visibleRows);
        g_app->scrollOffset = std::clamp(g_app->scrollOffset, 0, maxOffset);
        if (g_app->selected < g_app->scrollOffset) g_app->scrollOffset = g_app->selected;
        if (g_app->selected >= g_app->scrollOffset + visibleRows) {
            g_app->scrollOffset = g_app->selected - visibleRows + 1;
        }
        g_app->scrollPosition = g_app->scrollOffset * popupScrollStride();
    }
    invalidatePopupList(g_app->popup);
    invalidateFilterBar(g_app->popup);
}

void refreshVisible(bool preserveScrollPosition = false) {
    if (!g_app->popup) return;
    KillTimer(g_app->popup, kPopupSearchTimer);
    cancelPopupSearch();
    applyVisibleCandidates(g_app->store.search(g_app->query), preserveScrollPosition);
}

void startPopupSearch(HWND popup) {
    if (!g_app || g_app->popup != popup) return;
    cancelPopupSearch();
    const auto cancellation = std::make_shared<std::atomic<bool>>(false);
    g_app->searchCancellation = cancellation;
    const HWND notifyWindow = g_app->hidden;
    const std::uint64_t generation = g_app->searchGeneration;
    ClipSearchSnapshot snapshot = g_app->store.searchSnapshot();
    const std::uint64_t storeRevision = snapshot.revision;
    const std::string query = g_app->query;
    g_app->searchWorker = std::thread([notifyWindow, popup, generation, storeRevision,
                                       snapshot = std::move(snapshot), query,
                                       cancellation]() mutable {
        std::vector<std::size_t> candidates = ClipStore::search(
            snapshot, query, cancellation.get());
        if (cancellation->load(std::memory_order_relaxed)) return;
        auto* result = new PopupSearchResult{popup, generation, storeRevision,
                                             std::move(candidates)};
        if (!PostMessageW(notifyWindow, kPopupSearchCompleteMessage,
                          reinterpret_cast<WPARAM>(result), 0)) {
            delete result;
        }
    });
}

void scrollPopup(int delta) {
    if (!g_app->popup || g_app->visible.empty()) return;
    g_app->fastImagePreview = true;
    setPopupScrollPosition(g_app->scrollPosition + delta);
    invalidatePopupList(g_app->popup);
}

void notifyPasteFailure() {
    MessageBeep(MB_ICONWARNING);
}

void clearPopupImagePreviews() {
    if (!g_app) return;
    for (const PopupImagePreview& preview : g_app->imagePreviews) {
        if (preview.bitmap) DeleteObject(preview.bitmap);
    }
    g_app->imagePreviews.clear();
    g_app->imagePreviewRevision = g_app->store.revision();
}

bool drawCachedImagePreview(HDC dc, const PopupImagePreview& preview, const RECT& rowRect) {
    HDC source = CreateCompatibleDC(dc);
    if (!source) return false;
    HGDIOBJ previous = SelectObject(source, preview.bitmap);
    const int left = rowRect.left + (rowRect.right - rowRect.left - preview.width) / 2;
    const int top = rowRect.top + (rowRect.bottom - rowRect.top - preview.height) / 2;
    const bool drawn = BitBlt(dc, left, top, preview.width, preview.height,
                              source, 0, 0, SRCCOPY) != FALSE;
    SelectObject(source, previous);
    DeleteDC(source);
    return drawn;
}

bool drawImagePreview(HDC dc, const ClipItem& item, const RECT& rowRect) {
    if (!g_app) return false;
    if (g_app->imagePreviewRevision != g_app->store.revision()) clearPopupImagePreviews();

    const std::size_t itemIndex = static_cast<std::size_t>(&item - g_app->store.items().data());
    auto cached = std::find_if(g_app->imagePreviews.begin(), g_app->imagePreviews.end(),
                               [itemIndex](const PopupImagePreview& preview) {
        return preview.itemIndex == itemIndex;
    });
    if (cached != g_app->imagePreviews.end()) {
        if (drawCachedImagePreview(dc, *cached, rowRect)) return true;
        if (cached->bitmap) DeleteObject(cached->bitmap);
        g_app->imagePreviews.erase(cached);
    }

    std::string payload;
    if (!g_app->store.readPayload(itemIndex, payload)) return false;
    DibLayout layout{};
    if (!validateDibPayload(payload, &layout)) return false;

    BITMAPINFOHEADER header{};
    std::memcpy(&header, payload.data(), sizeof(header));
    const int width = rowRect.right - rowRect.left;
    const int height = rowRect.bottom - rowRect.top;
    const float imageWidth = static_cast<float>(header.biWidth);
    const float imageHeight = static_cast<float>(std::llabs(static_cast<long long>(header.biHeight)));
    const float scale = std::min(static_cast<float>(width) / imageWidth,
                                 static_cast<float>(height) / imageHeight);
    const int previewWidth = std::max(1, static_cast<int>(imageWidth * scale + 0.5f));
    const int previewHeight = std::max(1, static_cast<int>(imageHeight * scale + 0.5f));
    const auto drawDirect = [&]() {
        const int left = rowRect.left + (width - previewWidth) / 2;
        const int top = rowRect.top + (height - previewHeight) / 2;
        const int previousMode = SetStretchBltMode(dc,
            g_app->scrollDragging ? COLORONCOLOR : HALFTONE);
        POINT previousOrigin{};
        SetBrushOrgEx(dc, 0, 0, &previousOrigin);
        const int result = StretchDIBits(dc, left, top, previewWidth, previewHeight,
                                         0, 0, header.biWidth,
                                         static_cast<int>(std::llabs(static_cast<long long>(header.biHeight))),
                                         payload.data() + layout.bitsOffset,
                                         reinterpret_cast<const BITMAPINFO*>(payload.data()),
                                         DIB_RGB_COLORS, SRCCOPY);
        SetBrushOrgEx(dc, previousOrigin.x, previousOrigin.y, nullptr);
        SetStretchBltMode(dc, previousMode);
        return result != GDI_ERROR;
    };

    HDC previewDc = CreateCompatibleDC(dc);
    HBITMAP previewBitmap = previewDc ? CreateCompatibleBitmap(dc, previewWidth, previewHeight) : nullptr;
    if (!previewDc || !previewBitmap) {
        if (previewBitmap) DeleteObject(previewBitmap);
        if (previewDc) DeleteDC(previewDc);
        return drawDirect();
    }
    HGDIOBJ previous = SelectObject(previewDc, previewBitmap);
    const int previousMode = SetStretchBltMode(previewDc,
        g_app->fastImagePreview || g_app->scrollDragging ? COLORONCOLOR : HALFTONE);
    POINT previousOrigin{};
    SetBrushOrgEx(previewDc, 0, 0, &previousOrigin);
    const int result = StretchDIBits(previewDc, 0, 0, previewWidth, previewHeight,
                                     0, 0, header.biWidth,
                                     static_cast<int>(std::llabs(static_cast<long long>(header.biHeight))),
                                     payload.data() + layout.bitsOffset,
                                     reinterpret_cast<const BITMAPINFO*>(payload.data()),
                                     DIB_RGB_COLORS, SRCCOPY);
    SetBrushOrgEx(previewDc, previousOrigin.x, previousOrigin.y, nullptr);
    SetStretchBltMode(previewDc, previousMode);
    SelectObject(previewDc, previous);
    DeleteDC(previewDc);
    if (result == GDI_ERROR) {
        DeleteObject(previewBitmap);
        return drawDirect();
    }

    if (g_app->imagePreviews.size() >= kPopupImagePreviewCacheLimit) {
        if (g_app->imagePreviews.front().bitmap) DeleteObject(g_app->imagePreviews.front().bitmap);
        g_app->imagePreviews.erase(g_app->imagePreviews.begin());
    }
    g_app->imagePreviews.push_back(PopupImagePreview{itemIndex, previewBitmap,
                                                       previewWidth, previewHeight});
    return drawCachedImagePreview(dc, g_app->imagePreviews.back(), rowRect) || drawDirect();
}

void rememberPasteTarget(HWND candidate = nullptr, bool refreshFocus = true) {
    if (!g_app) return;
    if (!candidate) candidate = GetForegroundWindow();
    HWND target = candidate ? GetAncestor(candidate, GA_ROOT) : nullptr;
    if (!IsWindow(target) || target == g_app->popup) return;

    const HWND previousTarget = g_app->targetWindow;
    g_app->targetWindow = target;
    const DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
    GUITHREADINFO threadInfo{sizeof(threadInfo)};
    if (targetThread != 0 && GetGUIThreadInfo(targetThread, &threadInfo) &&
        IsWindow(threadInfo.hwndFocus) && GetAncestor(threadInfo.hwndFocus, GA_ROOT) == target) {
        g_app->targetFocusWindow = threadInfo.hwndFocus;
    } else if (refreshFocus || previousTarget != target) {
        g_app->targetFocusWindow = candidate;
    }
}

void restorePasteTargetFocus(HWND target) {
    if (!IsWindow(target)) return;
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
    const bool attached = targetThread != 0 && targetThread != currentThread &&
        AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;
    SetForegroundWindow(target);
    SetWindowPos(target, HWND_TOP, 0, 0, 0, 0,
                 SWP_DRAWFRAME | SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    if (IsWindow(g_app->targetFocusWindow) &&
        GetAncestor(g_app->targetFocusWindow, GA_ROOT) == target) {
        SetFocus(g_app->targetFocusWindow);
    }
    if (attached) AttachThreadInput(currentThread, targetThread, FALSE);
}

bool waitForPasteModifiersReleased() {
    constexpr int kMaxWaitMs = 500;
    for (int elapsed = 0; elapsed < kMaxWaitMs; elapsed += 10) {
        const bool pressed = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RCONTROL) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RMENU) & 0x8000) != 0;
        if (!pressed) return true;
        Sleep(10);
    }
    return false;
}

void sendPaste(PasteMode mode = PasteMode::Automatic) {
    if (!g_app->popup || g_app->visible.empty()) return;
    const int selected = std::clamp(g_app->selected, 0, static_cast<int>(g_app->visible.size()) - 1);
    const std::size_t index = g_app->visible[static_cast<std::size_t>(selected)];
    std::string payload;
    if (!g_app->store.readPayload(index, payload)) {
        appendDiagnosticLog("ERROR", "paste: unable to read selected history payload");
        notifyPasteFailure();
        return;
    }
    const std::uint64_t hash = g_app->store.items()[index].hash;
    if (!setClipboardDataForItem(g_app->store.items()[index], payload, mode)) {
        appendDiagnosticLog("ERROR", "paste: unable to write selected item to clipboard");
        notifyPasteFailure();
        return;
    }
    g_app->ignoredClipboardHash = hash;
    g_app->ignoredClipboardTextHash = clipboardDedupHash(g_app->store.items()[index].type, payload);
    g_app->ignoredClipboardUntil = GetTickCount64() + 750;
    if (!g_app->store.recordUse(index, g_app->settingsData.promotePastedItem)) {
        appendDiagnosticLog("WARN", "paste: unable to persist usage metadata");
    }
    HWND target = g_app->targetWindow;
    const bool keepPopup = g_app->popupPinned;
    g_app->popupImeMode = false;
    g_app->popupSearchInputActive = false;
    g_app->popupSearchControlDown = false;
    g_app->popupSuppressImeTriggerSpace = false;
    if (keepPopup) {
        ShowWindow(g_app->popup, SW_HIDE);
    } else {
        DestroyWindow(g_app->popup);
        g_app->popup = nullptr;
        g_app->searchEdit = nullptr;
    }
    if (!IsWindow(target)) {
        appendDiagnosticLog("WARN", "paste: target window is no longer valid");
        notifyPasteFailure();
        if (keepPopup && IsWindow(g_app->popup)) ShowWindow(g_app->popup, SW_SHOWNOACTIVATE);
        return;
    }
    restorePasteTargetFocus(target);
    if (!waitForPasteModifiersReleased()) {
        appendDiagnosticLog("WARN", "paste: user modifier key is still pressed");
        notifyPasteFailure();
        if (keepPopup && IsWindow(g_app->popup)) ShowWindow(g_app->popup, SW_SHOWNOACTIVATE);
        return;
    }
    INPUT keyDown[2]{};
    keyDown[0].type = INPUT_KEYBOARD;
    keyDown[0].ki.wVk = VK_LCONTROL;
    keyDown[1].type = INPUT_KEYBOARD;
    keyDown[1].ki.wVk = 'V';
    const UINT sentDown = SendInput(2, keyDown, sizeof(INPUT));
    Sleep(8);
    INPUT keyUp[2]{};
    keyUp[0] = keyDown[1];
    keyUp[0].ki.dwFlags = KEYEVENTF_KEYUP;
    keyUp[1] = keyDown[0];
    keyUp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    const UINT sentUp = SendInput(2, keyUp, sizeof(INPUT));
    if (sentDown != 2 || sentUp != 2) {
        SendInput(2, keyUp, sizeof(INPUT));
        appendDiagnosticLog("ERROR", "paste: SendInput failed", GetLastError());
        notifyPasteFailure();
        if (keepPopup && IsWindow(g_app->popup)) ShowWindow(g_app->popup, SW_SHOWNOACTIVATE);
        return;
    }
    appendDiagnosticLog("INFO", "paste: keyboard input sent");
    if (keepPopup && IsWindow(g_app->popup)) {
        SetWindowPos(g_app->popup, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

DWORD lastInputTick() {
    LASTINPUTINFO info{sizeof(info)};
    return GetLastInputInfo(&info) ? info.dwTime : 0;
}

HICON clipLiteIcon();
void updatePopupMouseHook();
void updatePopupKeyboardHook();

void applyPopupWindowFrame(HWND hwnd, int width, int height) {
    if (!hwnd) return;
    const int ellipse = ui(kPopupCornerRadius * 2);
    HRGN region = CreateRoundRectRgn(0, 0, width, height, ellipse, ellipse);
    if (region && SetWindowRgn(hwnd, region, TRUE) == 0) DeleteObject(region);

    // Keep the compositor's corner treatment aligned with the custom client frame on Windows 11.
    constexpr DWORD kDwmWindowCornerPreference = 33;
    constexpr DWORD kDwmCornerRound = 2;
    DwmSetWindowAttribute(hwnd, kDwmWindowCornerPreference, &kDwmCornerRound,
                          sizeof(kDwmCornerRound));
}

void showPopup(bool openedByWinV = false) {
    if (g_app->popup) {
        if (GetForegroundWindow() != g_app->popup) rememberPasteTarget();
        g_app->popupImeMode = false;
        g_app->popupSearchInputActive = false;
        g_app->popupSearchControlDown = false;
        g_app->popupSuppressImeTriggerSpace = false;
        g_app->popupOpenedByWinV = openedByWinV;
        g_app->popupOpenInputTick = lastInputTick();
        updatePopupMouseHook();
        g_app->popupOpening = true;
        g_app->popupActivated = false;
        ShowWindow(g_app->popup, SW_SHOWNOACTIVATE);
        SetWindowPos(g_app->popup, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        updatePopupKeyboardHook();
        restorePasteTargetFocus(g_app->targetWindow);
        SetTimer(g_app->popup, kPopupOpenGuardTimer, kPopupOpenGuardMs, nullptr);
        return;
    }
    rememberPasteTarget();
    GetCursorPos(&g_app->popupPoint);
    const HMONITOR monitor = MonitorFromPoint(g_app->popupPoint, MONITOR_DEFAULTTONEAREST);
    g_uiDpi = monitorDpi(monitor);
    g_app->query.clear();
    g_app->selected = 0;
    g_app->scrollOffset = 0;
    g_app->scrollPosition = 0;
    g_app->fastImagePreview = false;
    g_app->filterScrollOffset = 0;
    g_app->filterType = 0;
    g_app->pinnedOnly = false;
    g_app->hoveredRow = -1;
    g_app->hoveredFilter = -1;
    g_app->hoveredDeleteRow = -1;
    g_app->hoveredPinRow = -1;
    g_app->hoveredHeader = false;
    g_app->popupOpening = true;
    g_app->popupActivated = false;
    g_app->popupOpenedByWinV = openedByWinV;
    g_app->popupOpenInputTick = lastInputTick();
    clearPopupImagePreviews();
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
    g_app->popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                   L"ClipLitePopup",
                                   L"ClipLite", WS_POPUP | WS_CLIPCHILDREN, x, y, width, height,
                                   nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_app->popup) {
        g_app->popupOpening = false;
        return;
    }
    updatePopupMouseHook();
    SendMessageW(g_app->popup, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(clipLiteIcon()));
    SendMessageW(g_app->popup, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(clipLiteIcon()));
    applyPopupWindowFrame(g_app->popup, width, height);
    ShowWindow(g_app->popup, SW_SHOWNOACTIVATE);
    SetWindowPos(g_app->popup, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    updatePopupKeyboardHook();
    restorePasteTargetFocus(g_app->targetWindow);
    SetTimer(g_app->popup, kPopupOpenGuardTimer, kPopupOpenGuardMs, nullptr);
}

void closePopup() {
    cancelPopupSearch();
    const HWND target = g_app->targetWindow;
    if (g_app->popupMouseHook) {
        UnhookWindowsHookEx(g_app->popupMouseHook);
        g_app->popupMouseHook = nullptr;
    }
    if (g_app->popupKeyboardHook) {
        UnhookWindowsHookEx(g_app->popupKeyboardHook);
        g_app->popupKeyboardHook = nullptr;
    }
    if (g_app->popup) {
        KillTimer(g_app->popup, kPopupOpenGuardTimer);
        KillTimer(g_app->popup, kPopupDeactivateTimer);
        DestroyWindow(g_app->popup);
    }
    g_app->popup = nullptr;
    g_app->searchEdit = nullptr;
    g_app->popupSearchInputActive = false;
    g_app->popupSearchControlDown = false;
    g_app->popupSuppressImeTriggerSpace = false;
    g_app->popupImeMode = false;
    g_app->popupOpening = false;
    g_app->popupActivated = false;
    g_app->popupOpenedByWinV = false;
    g_app->popupOpenInputTick = 0;
    if (IsWindow(target)) restorePasteTargetFocus(target);
}

void runPopupImageScrollBenchmark() {
    const std::size_t imageCount = g_app->store.countType(ClipType::Image);
    if (imageCount == 0) {
        appendDiagnosticLog("ERROR", "benchmark: no image records available");
        g_app->benchmarkExitCode = 1;
        PostQuitMessage(0);
        return;
    }
    showPopup();
    if (!g_app->popup) {
        appendDiagnosticLog("ERROR", "benchmark: unable to create history popup");
        g_app->benchmarkExitCode = 1;
        PostQuitMessage(0);
        return;
    }
    UpdateWindow(g_app->popup);

    POINT point{ui(120), ui(kPopupListTop + 40)};
    ClientToScreen(g_app->popup, &point);
    const WPARAM wheel = static_cast<WPARAM>(static_cast<WORD>(-WHEEL_DELTA)) << 16;
    const LPARAM position = MAKELPARAM(static_cast<WORD>(point.x), static_cast<WORD>(point.y));
    const int iterations = std::min(40, std::max(1,
        static_cast<int>(g_app->visible.size()) - popupVisibleRows()));
    std::vector<LONGLONG> samples;
    samples.reserve(iterations);
    const LONGLONG frequency = settingsToggleClockFrequency();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const LONGLONG start = settingsToggleClock();
        SendMessageW(g_app->popup, WM_MOUSEWHEEL, wheel, position);
        UpdateWindow(g_app->popup);
        samples.push_back(settingsToggleClock() - start);
    }
    std::sort(samples.begin(), samples.end());
    const auto milliseconds = [frequency](LONGLONG ticks) {
        return static_cast<double>(ticks) * 1000.0 /
            static_cast<double>(std::max<LONGLONG>(1, frequency));
    };
    const std::size_t p50Index = (samples.size() - 1) / 2;
    const std::size_t p95Index = std::min(samples.size() - 1,
        (samples.size() * 95 + 99) / 100 - 1);
    char message[256]{};
    std::snprintf(message, sizeof(message),
                  "benchmark: image-scroll images=%zu frames=%d p50=%.2fms p95=%.2fms max=%.2fms gdi=%lu",
                  imageCount, iterations, milliseconds(samples[p50Index]), milliseconds(samples[p95Index]),
                  milliseconds(samples.back()), GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
    appendDiagnosticLog("INFO", message);
    closePopup();
    PostQuitMessage(0);
}

void setPopupPinned(bool pinned) {
    if (!g_app) return;
    g_app->popupPinned = pinned;
    g_app->settingsData.historyWindowPinned = pinned;
    saveSettings(g_app->settingsData);
    if (!g_app->popup) return;
    SetWindowPos(g_app->popup, pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    updatePopupMouseHook();
    updatePopupKeyboardHook();
    InvalidateRect(g_app->popup, nullptr, FALSE);
}

HICON clipLiteIcon() {
    return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResource));
}

void openSettings();

bool addTrayIcon() {
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_app->hidden;
    icon.uID = kTrayId;
    icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon.uCallbackMessage = kTrayMessage;
    icon.hIcon = clipLiteIcon();
    wcscpy_s(icon.szTip, L"ClipLite");
    return Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
}

void showStartupNotification() {
    if (!g_app || !g_app->hidden) return;
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = g_app->hidden;
    icon.uID = kTrayId;
    icon.uFlags = NIF_INFO;
    wcscpy_s(icon.szInfoTitle, settingsLocale().startupNotificationTitle);
    wcscpy_s(icon.szInfo, settingsLocale().startupNotification);
    icon.dwInfoFlags = NIIF_INFO;
    if (!Shell_NotifyIconW(NIM_MODIFY, &icon)) {
        appendDiagnosticLog("WARN", "startup: unable to show tray notification", GetLastError());
    }
}

void showStartupFailure(const wchar_t* message) {
    appendDiagnosticLog("ERROR", "startup: fatal initialization failure");
    MessageBoxW(nullptr, message, settingsLocale().startupFailureTitle,
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
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
    AppendMenuW(menu, MF_STRING, kTrayOpen, settingsLocale().trayOpen);
    AppendMenuW(menu, MF_STRING, kTraySettings, settingsLocale().traySettings);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, settingsLocale().trayExit);
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

LRESULT CALLBACK popupMouseProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_app && g_app->popupMouseHook && g_app->popup &&
        !g_app->popupPinned &&
        !g_app->popupOpening &&
        (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN ||
         wParam == WM_MBUTTONDOWN || wParam == WM_XBUTTONDOWN)) {
        const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        RECT popupRect{};
        GetWindowRect(g_app->popup, &popupRect);
        if (!PtInRect(&popupRect, mouse->pt)) {
            PostMessageW(g_app->hidden, kClosePopupMessage, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool isPopupKeyboardCommand(UINT virtualKey) {
    switch (virtualKey) {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_HOME:
    case VK_END:
    case VK_RETURN:
    case VK_ESCAPE:
    case VK_TAB:
    case VK_DELETE:
    case VK_F10:
        return true;
    default:
        return false;
    }
}

bool isPopupTextInputKey(UINT virtualKey) {
    if ((virtualKey >= 'A' && virtualKey <= 'Z') ||
        (virtualKey >= '0' && virtualKey <= '9') ||
        (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)) {
        return true;
    }
    switch (virtualKey) {
    case VK_SPACE:
    case VK_BACK:
    case VK_DECIMAL:
    case VK_OEM_1:
    case VK_OEM_PLUS:
    case VK_OEM_COMMA:
    case VK_OEM_MINUS:
    case VK_OEM_PERIOD:
    case VK_OEM_2:
    case VK_OEM_3:
    case VK_OEM_4:
    case VK_OEM_5:
    case VK_OEM_6:
    case VK_OEM_7:
    case VK_PACKET:
    case VK_PROCESSKEY:
        return true;
    default:
        return false;
    }
}

bool popupSearchHasFocus() {
    return g_app && g_app->popup && g_app->searchEdit &&
           IsWindow(g_app->searchEdit) && g_app->popupSearchInputActive;
}

bool popupShouldEnterImeMode(const KBDLLHOOKSTRUCT& key) {
    if (!g_app || g_app->popupImeMode) return false;
    if (key.vkCode == VK_PROCESSKEY) return true;
    if (key.vkCode == VK_SPACE && g_app->popupSearchControlDown) return true;
    return false;
}

bool activatePopupSearchFocus(bool imeMode) {
    if (!g_app || !g_app->popup || !g_app->searchEdit ||
        (imeMode && g_app->popupImeMode)) return false;
    g_app->popupImeMode = imeMode;
    g_app->popupSearchInputActive = false;
    g_app->popupSearchControlDown = false;
    g_app->popupSuppressImeTriggerSpace = false;
    KillTimer(g_app->popup, kPopupDeactivateTimer);
    SetForegroundWindow(g_app->popup);
    SetActiveWindow(g_app->popup);
    SetFocus(g_app->searchEdit);
    g_app->popupActivated = GetForegroundWindow() == g_app->popup;
    if (!g_app->popupActivated) {
        g_app->popupImeMode = false;
        g_app->popupSearchInputActive = false;
        g_app->popupSuppressImeTriggerSpace = false;
        return false;
    }
    InvalidateRect(g_app->popup, nullptr, FALSE);
    return true;
}

void enterPopupImeMode() {
    activatePopupSearchFocus(true);
}

void syncPopupSearchKeyboardLayout() {
    if (!g_app || !IsWindow(g_app->targetWindow)) return;
    const DWORD targetThread = GetWindowThreadProcessId(g_app->targetWindow, nullptr);
    if (targetThread == 0) return;
    const HKL layout = GetKeyboardLayout(targetThread);
    if (layout) ActivateKeyboardLayout(layout, 0);
}

void updatePopupSearchKeyboardState(const KBDLLHOOKSTRUCT& key, bool keyDown) {
    BYTE state[256]{};
    if (!GetKeyboardState(state)) return;
    const BYTE value = keyDown ? 0x80 : 0;
    state[key.vkCode] = static_cast<BYTE>((state[key.vkCode] & 0x01) | value);
    switch (key.vkCode) {
    case VK_LSHIFT:
    case VK_RSHIFT:
        state[VK_SHIFT] = value;
        break;
    case VK_LCONTROL:
    case VK_RCONTROL:
        state[VK_CONTROL] = value;
        break;
    case VK_LMENU:
    case VK_RMENU:
        state[VK_MENU] = value;
        break;
    case VK_CAPITAL:
    case VK_NUMLOCK:
    case VK_SCROLL:
        if (keyDown) state[key.vkCode] ^= 0x01;
        break;
    default:
        break;
    }
    SetKeyboardState(state);
}

LPARAM popupKeyMessageLParam(const KBDLLHOOKSTRUCT& key, bool keyUp) {
    LPARAM value = 1L | (static_cast<LPARAM>(key.scanCode & 0xff) << 16);
    if ((key.flags & LLKHF_EXTENDED) != 0) value |= 1L << 24;
    if (keyUp) value |= (1L << 30) | (1L << 31);
    return value;
}

void forwardPopupSearchKey(const KBDLLHOOKSTRUCT& key, WPARAM hookMessage) {
    if (!g_app || !g_app->searchEdit || !IsWindow(g_app->searchEdit)) return;
    const bool keyDown = hookMessage == WM_KEYDOWN || hookMessage == WM_SYSKEYDOWN;
    const bool keyUp = hookMessage == WM_KEYUP || hookMessage == WM_SYSKEYUP;
    if (!keyDown && !keyUp) return;
    const bool systemKey = (key.flags & LLKHF_ALTDOWN) != 0;
    const UINT message = keyDown
        ? (systemKey ? WM_SYSKEYDOWN : WM_KEYDOWN)
        : (systemKey ? WM_SYSKEYUP : WM_KEYUP);
    const LPARAM lParam = popupKeyMessageLParam(key, keyUp);
    updatePopupSearchKeyboardState(key, keyDown);
    PostMessageW(g_app->searchEdit, message, key.vkCode, lParam);
}

LRESULT CALLBACK popupKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_app && g_app->popupKeyboardHook && g_app->popup) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
        const bool injected = (key->flags & LLKHF_INJECTED) != 0;
        const bool popupHasNoSystemFocus = GetForegroundWindow() != g_app->popup;
        const bool searchHasFocus = popupSearchHasFocus();
        if (!injected && searchHasFocus && (keyDown || keyUp)) {
            if (key->vkCode == VK_LCONTROL || key->vkCode == VK_RCONTROL) {
                g_app->popupSearchControlDown = keyDown;
            }
        }
        if (!injected && popupHasNoSystemFocus && searchHasFocus && (keyDown || keyUp) &&
            key->vkCode != VK_LWIN && key->vkCode != VK_RWIN) {
            if (keyDown && popupShouldEnterImeMode(*key)) {
                auto* event = new PopupImeKeyEvent{*key, wParam};
                if (!PostMessageW(g_app->hidden, kPopupEnterImeMessage,
                                  reinterpret_cast<WPARAM>(event), 0)) {
                    delete event;
                }
                return 1;
            }
            forwardPopupSearchKey(*key, wParam);
            return 1;
        }
        if (!injected && (keyDown || keyUp) && isPopupKeyboardCommand(key->vkCode) &&
            popupHasNoSystemFocus && !searchHasFocus) {
            if (keyDown) PostMessageW(g_app->hidden, kPopupKeyboardMessage, key->vkCode, 0);
            return 1;
        }
        if (!injected && keyDown && isPopupTextInputKey(key->vkCode) &&
            popupHasNoSystemFocus && !searchHasFocus) {
            PostMessageW(g_app->hidden, kClosePopupMessage, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void updatePopupKeyboardHook() {
    if (!g_app) return;
    if (g_app->popupKeyboardHook) {
        UnhookWindowsHookEx(g_app->popupKeyboardHook);
        g_app->popupKeyboardHook = nullptr;
    }
    if (g_app->popup && g_app->targetWindow) {
        g_app->popupKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, popupKeyboardProc,
                                                     GetModuleHandleW(nullptr), 0);
        if (!g_app->popupKeyboardHook) {
            appendDiagnosticLog("WARN", "popup: keyboard hook installation failed", GetLastError());
        } else {
            appendDiagnosticLog("INFO", "popup: dual-focus keyboard hook installed");
        }
    }
}

void updatePopupMouseHook() {
    if (!g_app) return;
    if (g_app->popupMouseHook) {
        UnhookWindowsHookEx(g_app->popupMouseHook);
        g_app->popupMouseHook = nullptr;
    }
    if (g_app->popup && !g_app->popupPinned) {
        g_app->popupMouseHook = SetWindowsHookExW(WH_MOUSE_LL, popupMouseProc,
                                                  GetModuleHandleW(nullptr), 0);
    }
}

std::vector<BYTE> downloadSupportImage(const wchar_t* url) {
    constexpr std::size_t maxImageBytes = 8u * 1024u * 1024u;
    URL_COMPONENTS components{sizeof(components)};
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url, 0, 0, &components)) return {};

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }

    const auto closeHandle = [](HINTERNET handle) {
        if (handle) WinHttpCloseHandle(handle);
    };
    using HttpHandle = std::unique_ptr<void, decltype(closeHandle)>;
    HttpHandle session(WinHttpOpen(L"ClipLite/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), closeHandle);
    if (!session) return {};
    WinHttpSetTimeouts(session.get(), 5000, 5000, 5000, 10000);

    HttpHandle connection(WinHttpConnect(session.get(), host.c_str(), components.nPort, 0), closeHandle);
    if (!connection) return {};
    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags),
                       closeHandle);
    if (!request ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        return {};
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || statusCode != 200) {
        return {};
    }

    std::vector<BYTE> bytes;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) return {};
        if (available == 0) break;
        if (available > maxImageBytes - bytes.size()) return {};
        const std::size_t offset = bytes.size();
        bytes.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), bytes.data() + offset, available, &read)) return {};
        bytes.resize(offset + read);
    }
    return bytes;
}

std::unique_ptr<SupportWindowState::Image> decodeSupportImage(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return nullptr;
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return nullptr;
    }
    std::memcpy(destination, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return nullptr;
    }
    auto result = std::make_unique<SupportWindowState::Image>();
    result->stream = stream;
    result->value = std::make_unique<Gdiplus::Image>(stream, FALSE);
    if (result->value->GetLastStatus() != Gdiplus::Ok) return nullptr;
    return result;
}

bool loadSupportImageAsync(HWND hwnd,
                           const std::shared_ptr<SupportWindowState::DownloadQueue>& queue,
                           SupportWindowState::ImageKind kind, const wchar_t* url) {
    try {
        std::thread([hwnd, queue, kind, url = std::wstring(url)] {
            SupportWindowState::DownloadResult result{kind, downloadSupportImage(url.c_str())};
            {
                std::lock_guard<std::mutex> lock(queue->mutex);
                queue->completed.push_back(std::move(result));
            }
            PostMessageW(hwnd, kSupportImageLoadedMessage, 0, 0);
        }).detach();
        return true;
    } catch (...) {
        return false;
    }
}

void drawSupportImage(HDC dc, Gdiplus::Image* image, const RECT& bounds) {
    if (!image || image->GetWidth() == 0 || image->GetHeight() == 0) return;
    Gdiplus::Graphics graphics(dc);
    configureGdiGraphics(graphics);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    const float width = static_cast<float>(bounds.right - bounds.left);
    const float height = static_cast<float>(bounds.bottom - bounds.top);
    const float scale = std::min(width / static_cast<float>(image->GetWidth()),
                                 height / static_cast<float>(image->GetHeight()));
    const float drawWidth = static_cast<float>(image->GetWidth()) * scale;
    const float drawHeight = static_cast<float>(image->GetHeight()) * scale;
    const Gdiplus::RectF destination(
        static_cast<float>(bounds.left) + (width - drawWidth) / 2.0f,
        static_cast<float>(bounds.top) + (height - drawHeight) / 2.0f,
        drawWidth, drawHeight);
    graphics.DrawImage(image, destination);
}

void drawSupportText(HDC dc, HFONT font, const wchar_t* text, RECT rect,
                     COLORREF color, UINT format) {
    if (!text) return;
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format);
    if (oldFont) SelectObject(dc, oldFont);
}

void paintSupportWindow(HWND hwnd, HDC dc) {
    auto* state = reinterpret_cast<SupportWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!state) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    const COLORREF background = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOW)
        : settingsThemeColor(RGB(240, 244, 248), RGB(21, 26, 34));
    const COLORREF card = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOW)
        : settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
    const COLORREF text = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOWTEXT)
        : settingsThemeColor(RGB(30, 41, 59), RGB(226, 232, 240));
    const COLORREF secondary = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOWTEXT)
        : settingsThemeColor(RGB(95, 113, 131), RGB(143, 161, 179));
    const COLORREF line = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOWTEXT)
        : settingsThemeColor(RGB(200, 211, 222), RGB(46, 57, 71));

    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(dc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);
    drawSupportText(dc, state->titleFont, state->qqGroup ? settingsLocale().qqGroupTitle
                                                            : settingsLocale().supportTitle,
                    RECT{ui(24), ui(16), client.right - ui(24), ui(48)}, text,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    drawSupportText(dc, state->bodyFont, state->qqGroup ? settingsLocale().qqGroupNumber
                                                         : settingsLocale().supportMessage,
                    RECT{ui(24), ui(50), client.right - ui(24), ui(78)}, secondary,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (state->qqGroup) {
        const RECT cardRect{ui(20), ui(88), client.right - ui(20), client.bottom - ui(94)};
        drawGdiRoundedSurface(dc, cardRect, card, line, 8);
        drawSupportImage(dc, state->qq ? state->qq->value.get() : nullptr,
                         RECT{cardRect.left + ui(18), cardRect.top + ui(18),
                              cardRect.right - ui(18), cardRect.bottom - ui(18)});
        drawSupportText(dc, state->bodyFont, settingsLocale().qqGroupNumber,
                        RECT{ui(24), client.bottom - ui(88), client.right - ui(24), client.bottom - ui(60)},
                        text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (!state->qq) {
            const wchar_t* status = state->qqLoading ? settingsLocale().supportImageLoading
                                                     : settingsLocale().supportImageLoadFailed;
            drawSupportText(dc, state->bodyFont, status,
                            RECT{cardRect.left + ui(20), cardRect.top + ui(20),
                                 cardRect.right - ui(20), cardRect.bottom - ui(20)},
                            secondary, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
    } else {
        const int gap = ui(14);
        const int left = ui(20);
        const int right = client.right - ui(20);
        const int cardWidth = (right - left - gap) / 2;
        const int cardTop = ui(88);
        const int cardBottom = client.bottom - ui(22);
        const RECT wechatCard{left, cardTop, left + cardWidth, cardBottom};
        const RECT alipayCard{left + cardWidth + gap, cardTop, right, cardBottom};
        drawGdiRoundedSurface(dc, wechatCard, card, line, 8);
        drawGdiRoundedSurface(dc, alipayCard, card, line, 8);
        drawSupportText(dc, state->bodyFont, settingsLocale().wechatPay,
                        RECT{wechatCard.left + ui(10), wechatCard.top + ui(10),
                             wechatCard.right - ui(10), wechatCard.top + ui(36)}, text,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawSupportText(dc, state->bodyFont, settingsLocale().alipayPay,
                        RECT{alipayCard.left + ui(10), alipayCard.top + ui(10),
                             alipayCard.right - ui(10), alipayCard.top + ui(36)}, text,
                        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawSupportImage(dc, state->wechat ? state->wechat->value.get() : nullptr,
                         RECT{wechatCard.left + ui(12), wechatCard.top + ui(42),
                               wechatCard.right - ui(12), wechatCard.bottom - ui(12)});
        drawSupportImage(dc, state->alipay ? state->alipay->value.get() : nullptr,
                         RECT{alipayCard.left + ui(12), alipayCard.top + ui(42),
                               alipayCard.right - ui(12), alipayCard.bottom - ui(12)});
        if (!state->wechat) {
            const wchar_t* status = state->wechatLoading ? settingsLocale().supportImageLoading
                                                         : settingsLocale().supportImageLoadFailed;
            drawSupportText(dc, state->bodyFont, status,
                            RECT{wechatCard.left + ui(20), wechatCard.top + ui(50),
                                 wechatCard.right - ui(20), wechatCard.bottom - ui(20)},
                            secondary, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
        if (!state->alipay) {
            const wchar_t* status = state->alipayLoading ? settingsLocale().supportImageLoading
                                                         : settingsLocale().supportImageLoadFailed;
            drawSupportText(dc, state->bodyFont, status,
                            RECT{alipayCard.left + ui(20), alipayCard.top + ui(50),
                                 alipayCard.right - ui(20), alipayCard.bottom - ui(20)},
                            secondary, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        }
    }
}

bool copySupportGroupNumber(HWND owner) {
    constexpr wchar_t groupNumber[] = L"1081580020";
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    const std::size_t bytes = (ARRAYSIZE(groupNumber)) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }
    void* destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    std::memcpy(destination, groupNumber, bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

LRESULT CALLBACK supportWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CREATE) {
        auto* state = new SupportWindowState;
        state->qqGroup = reinterpret_cast<const CREATESTRUCTW*>(lParam)->lpCreateParams != nullptr;
        state->titleFont = CreateFontW(-ui(18), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        state->bodyFont = CreateFontW(-ui(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (g_supportProcessMode && g_supportOwnerWindow) {
            SetTimer(hwnd, kSupportOwnerTimer, 250, nullptr);
        }
        if (state->qqGroup) {
            state->qqLoading = loadSupportImageAsync(
                hwnd, state->downloadQueue, SupportWindowState::ImageKind::Qq,
                L"https://env-00jx4xoyfjjc.normal.cloudstatic.cn/pic/ClipLite/support-qq.jpg");
            CreateWindowW(L"BUTTON", settingsLocale().copyQqGroup,
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                          ui(180), ui(kSupportQqHeight - 54), ui(200), ui(30), hwnd,
                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSupportCopyGroup)),
                          GetModuleHandleW(nullptr), nullptr);
            RECT client{};
            GetClientRect(hwnd, &client);
            SetWindowPos(GetDlgItem(hwnd, kSupportCopyGroup), nullptr,
                         (client.right - ui(200)) / 2, client.bottom - ui(52),
                         ui(200), ui(30), SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            state->wechatLoading = loadSupportImageAsync(
                hwnd, state->downloadQueue, SupportWindowState::ImageKind::Wechat,
                L"https://env-00jx4xoyfjjc.normal.cloudstatic.cn/pic/common/support-wechat.png");
            state->alipayLoading = loadSupportImageAsync(
                hwnd, state->downloadQueue, SupportWindowState::ImageKind::Alipay,
                L"https://env-00jx4xoyfjjc.normal.cloudstatic.cn/pic/common/support-alipay.png");
        }
        return 0;
    }
    auto* state = reinterpret_cast<SupportWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_TIMER && wParam == kSupportOwnerTimer) {
        if (!g_supportOwnerWindow || !IsWindow(g_supportOwnerWindow)) DestroyWindow(hwnd);
        return 0;
    }
    if (message == kSupportImageLoadedMessage && state) {
        std::vector<SupportWindowState::DownloadResult> completed;
        {
            std::lock_guard<std::mutex> lock(state->downloadQueue->mutex);
            completed.swap(state->downloadQueue->completed);
        }
        for (auto& result : completed) {
            auto image = decodeSupportImage(result.bytes);
            switch (result.kind) {
            case SupportWindowState::ImageKind::Wechat:
                state->wechat = std::move(image);
                state->wechatLoading = false;
                break;
            case SupportWindowState::ImageKind::Alipay:
                state->alipay = std::move(image);
                state->alipayLoading = false;
                break;
            case SupportWindowState::ImageKind::Qq:
                state->qq = std::move(image);
                state->qqLoading = false;
                break;
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == kSupportCopyGroup) {
        if (copySupportGroupNumber(hwnd)) {
            SetWindowTextW(GetDlgItem(hwnd, kSupportCopyGroup), settingsLocale().copiedQqGroup);
        }
        return 0;
    }
    if (message == WM_SIZE && state && state->qqGroup) {
        RECT client{};
        GetClientRect(hwnd, &client);
        SetWindowPos(GetDlgItem(hwnd, kSupportCopyGroup), nullptr,
                     (client.right - ui(200)) / 2, client.bottom - ui(52),
                     ui(200), ui(30), SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc) paintSupportWindow(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_DESTROY) {
        KillTimer(hwnd, kSupportOwnerTimer);
        if (state) {
            if (state->titleFont) DeleteObject(state->titleFont);
            if (state->bodyFont) DeleteObject(state->bodyFont);
            delete state;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (g_app && g_app->support == hwnd) g_app->support = nullptr;
        if (g_supportProcessMode) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void createSupportWindow(bool qqGroup) {
    if (!g_app) return;
    if (g_app->support) {
        SetForegroundWindow(g_app->support);
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    g_uiDpi = monitorDpi(monitor);
    const int width = ui(qqGroup ? kSupportQqWidth : kSupportPaymentWidth);
    const int height = ui(qqGroup ? kSupportQqHeight : kSupportPaymentHeight);
    const int x = monitorInfo.rcWork.left +
        (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    const int y = monitorInfo.rcWork.top +
        (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    const wchar_t* title = qqGroup ? settingsLocale().qqGroupTitle : settingsLocale().supportTitle;
    g_app->support = CreateWindowExW(WS_EX_APPWINDOW, L"ClipLiteSupport", title,
                                     WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                     x, y, width, height, g_app->settings, nullptr,
                                     GetModuleHandleW(nullptr),
                                     reinterpret_cast<LPVOID>(static_cast<INT_PTR>(qqGroup ? 1 : 0)));
    if (!g_app->support) return;
    SendMessageW(g_app->support, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(clipLiteIcon()));
    SendMessageW(g_app->support, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(clipLiteIcon()));
    ShowWindow(g_app->support, SW_SHOW);
    UpdateWindow(g_app->support);
    SetForegroundWindow(g_app->support);
}

HWND supportWindowForProcess(DWORD processId) {
    HWND window = nullptr;
    while ((window = FindWindowExW(nullptr, window, L"ClipLiteSupport", nullptr)) != nullptr) {
        DWORD windowProcessId = 0;
        GetWindowThreadProcessId(window, &windowProcessId);
        if (windowProcessId == processId) return window;
    }
    return nullptr;
}

void closeSupportProcess() {
    if (!g_app || !g_app->supportProcess) return;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(g_app->supportProcess, &exitCode) && exitCode == STILL_ACTIVE) {
        if (HWND window = supportWindowForProcess(g_app->supportProcessId)) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
    CloseHandle(g_app->supportProcess);
    g_app->supportProcess = nullptr;
    g_app->supportProcessId = 0;
}

void openSupportWindow(bool qqGroup) {
    if (!g_app || !g_app->settings) return;
    if (g_app->supportProcess) {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(g_app->supportProcess, &exitCode) && exitCode == STILL_ACTIVE) {
            if (HWND window = supportWindowForProcess(g_app->supportProcessId)) {
                ShowWindow(window, SW_RESTORE);
                SetForegroundWindow(window);
            }
            return;
        }
        closeSupportProcess();
    }

    std::vector<wchar_t> executable(32768);
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                            static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return;
    const auto owner = static_cast<unsigned long long>(
        reinterpret_cast<ULONG_PTR>(g_app->settings));
    std::wstring command = L"\"" + std::wstring(executable.data(), length) + L"\" ";
    command += qqGroup ? L"--support-qq" : L"--support-payment";
    command += L" --support-owner=" + std::to_wstring(owner);

    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.data(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_DEFAULT_ERROR_MODE, nullptr, nullptr, &startup, &process)) {
        appendDiagnosticLog("WARN", "support: unable to launch helper process", GetLastError());
        return;
    }
    CloseHandle(process.hThread);
    g_app->supportProcess = process.hProcess;
    g_app->supportProcessId = process.dwProcessId;
    AllowSetForegroundWindow(process.dwProcessId);
}

int runSupportWindowProcess(HINSTANCE instance, bool qqGroup, HWND owner) {
    if (!owner || !IsWindow(owner)) return 0;
    g_supportProcessMode = true;
    g_supportOwnerWindow = owner;

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW supportClass{};
    supportClass.style = CS_HREDRAW | CS_VREDRAW;
    supportClass.lpfnWndProc = supportWindowProc;
    supportClass.hInstance = instance;
    supportClass.hIcon = clipLiteIcon();
    supportClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    supportClass.lpszClassName = L"ClipLiteSupport";
    if (!RegisterClassW(&supportClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_app->gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        g_app->gdiplusToken = 0;
        return 1;
    }
    createSupportWindow(qqGroup);
    if (!g_app->support) {
        Gdiplus::GdiplusShutdown(g_app->gdiplusToken);
        g_app->gdiplusToken = 0;
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    Gdiplus::GdiplusShutdown(g_app->gdiplusToken);
    g_app->gdiplusToken = 0;
    g_supportOwnerWindow = nullptr;
    g_supportProcessMode = false;
    return 0;
}

void resetWinKeyState(bool releaseForwardedKey) {
    if (!g_app) return;
    if (g_app->hidden) KillTimer(g_app->hidden, kWinVReleaseTimer);
    if (releaseForwardedKey && g_app->winKeyForwarded) {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(g_app->pendingWinKey);
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(input));
    }
    g_app->winKeyDown = false;
    g_app->suppressWinV = false;
    g_app->suppressVKeyUp = false;
    g_app->pendingWinVPopup = false;
    g_app->winKeyForwarded = false;
    g_app->pendingWinKey = VK_LWIN;
    g_app->winKeyDownTime = 0;
}

LRESULT CALLBACK lowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && g_app && g_app->keyboardHook) {
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
        const bool injected = (key->flags & LLKHF_INJECTED) != 0;
        const bool winKey = key->vkCode == VK_LWIN || key->vkCode == VK_RWIN;
        if (injected) return CallNextHookEx(nullptr, code, wParam, lParam);

        const auto replayWinKeyDown = [&]() {
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = static_cast<WORD>(g_app->pendingWinKey);
            return SendInput(1, &input, sizeof(input)) == 1;
        };

        if (winKey) {
            if (keyDown) {
                if (g_app->winKeyDown) return 1;
                g_app->winKeyDown = true;
                g_app->pendingWinKey = static_cast<UINT>(key->vkCode);
                g_app->winKeyDownTime = key->time;
                g_app->winKeyForwarded = false;
                return 1;
            }
            if (keyUp) {
                if (!g_app->winKeyDown) return CallNextHookEx(nullptr, code, wParam, lParam);
                const bool showWinVPopup = g_app->suppressWinV && g_app->pendingWinVPopup;
                if (showWinVPopup) {
                    resetWinKeyState(false);
                    PostMessageW(g_app->hidden, kShowPopupMessage, 1, 0);
                    return 1;
                }
                if (!g_app->winKeyForwarded) {
                    INPUT inputs[2]{};
                    inputs[0].type = INPUT_KEYBOARD;
                    inputs[0].ki.wVk = static_cast<WORD>(g_app->pendingWinKey);
                    inputs[1] = inputs[0];
                    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
                    if (SendInput(2, inputs, sizeof(INPUT)) != 2) {
                        resetWinKeyState(false);
                        return CallNextHookEx(nullptr, code, wParam, lParam);
                    }
                }
                resetWinKeyState(false);
                return CallNextHookEx(nullptr, code, wParam, lParam);
            }
        }

        if (g_app->winKeyDown && keyDown) {
            if (key->vkCode == 'V' && !g_app->winKeyForwarded) {
                if (!g_app->suppressWinV) {
                    g_app->suppressWinV = true;
                    g_app->suppressVKeyUp = true;
                    g_app->pendingWinVPopup = true;
                    g_app->winKeyDown = false;
                    g_app->winKeyForwarded = false;
                    SetTimer(g_app->hidden, kWinVReleaseTimer, 8, nullptr);
                }
                return 1;
            }
            if (g_app->suppressWinV) return 1;
            if (!g_app->winKeyForwarded && !replayWinKeyDown()) {
                resetWinKeyState(false);
            } else if (!g_app->winKeyForwarded) {
                g_app->winKeyForwarded = true;
            }
        }
        if (keyUp && g_app->suppressVKeyUp && key->vkCode == 'V') {
            g_app->suppressVKeyUp = false;
            return 1;
        }
        if (g_app->winKeyDown && g_app->suppressWinV && keyUp) return 1;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

bool registerConfiguredHotkey(int id, const ShortcutBinding& binding) {
    if (!g_app || !g_app->hidden || binding.modifiers == 0 || binding.virtualKey == 0) return false;
    return RegisterHotKey(g_app->hidden, id, binding.modifiers | MOD_NOREPEAT,
                          binding.virtualKey) != FALSE;
}

void registerHotkeys() {
    UnregisterHotKey(g_app->hidden, kHotkeyAltV);
    UnregisterHotKey(g_app->hidden, kHotkeyWinV);
    UnregisterHotKey(g_app->hidden, kHotkeySettings);
    UnregisterHotKey(g_app->hidden, kHotkeyPause);
    g_app->winVHotkeyRegistered = false;
    resetWinKeyState(true);
    if (g_app->keyboardHook) {
        UnhookWindowsHookEx(g_app->keyboardHook);
        g_app->keyboardHook = nullptr;
    }
    g_app->shortcutRegistrationWarning = false;
    auto registerWithFallback = [&](int id, ShortcutBinding& binding,
                                     const ShortcutBinding& fallback) {
        if (registerConfiguredHotkey(id, binding)) return;
        appendDiagnosticLog("WARN", "hotkey: configured shortcut registration failed");
        g_app->shortcutRegistrationWarning = true;
        binding = fallback;
        registerConfiguredHotkey(id, binding);
    };
    registerWithFallback(kHotkeyAltV, g_app->settingsData.historyHotkey,
                         ShortcutBinding{MOD_ALT, 'V'});
    registerWithFallback(kHotkeySettings, g_app->settingsData.settingsHotkey,
                         ShortcutBinding{MOD_CONTROL | MOD_ALT, 'S'});
    registerWithFallback(kHotkeyPause, g_app->settingsData.pauseHotkey,
                         ShortcutBinding{MOD_CONTROL | MOD_SHIFT, 'P'});
    if (g_app->settingsData.winV) {
        g_app->keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, lowLevelKeyboardProc,
                                                 GetModuleHandleW(nullptr), 0);
        if (g_app->keyboardHook) {
            appendDiagnosticLog("INFO", "hotkey: Win+V low-level interceptor installed");
        }
        if (!g_app->keyboardHook) {
            appendDiagnosticLog("ERROR", "hotkey: unable to install Win+V interceptor",
                                GetLastError());
            g_app->winVHotkeyRegistered = registerConfiguredHotkey(
                kHotkeyWinV, ShortcutBinding{MOD_WIN, 'V'});
            if (!g_app->winVHotkeyRegistered) {
                MessageBoxW(g_app->hidden, tr(L"Unable to replace Win+V.", L"无法替换 Win+V。"),
                            L"ClipLite", MB_OK | MB_ICONWARNING);
                g_app->settingsData.winV = false;
                saveSettings(g_app->settingsData);
            } else {
                appendDiagnosticLog("INFO", "hotkey: Win+V registered through RegisterHotKey fallback");
            }
        }
    }
    if (g_app->shortcutRegistrationWarning && g_app->settings) {
        InvalidateRect(g_app->settings, nullptr, FALSE);
    }
}

bool shortcutMatches(const ShortcutBinding& binding, UINT virtualKey);

LRESULT CALLBACK editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        return TRUE;
    }
    if (message == WM_CHAR && wParam == L' ' && g_app->popupSuppressImeTriggerSpace) {
        g_app->popupSuppressImeTriggerSpace = false;
        return 0;
    }
    if (message == WM_KEYUP && wParam == VK_SPACE) {
        g_app->popupSuppressImeTriggerSpace = false;
    }
    if (message == WM_LBUTTONDOWN && !g_app->settingsData.searchImeCompatibility) {
        activatePopupSearchFocus(false);
    }
    if (message == WM_LBUTTONDOWN || message == WM_SETFOCUS) {
        g_app->popupSearchInputActive = true;
        syncPopupSearchKeyboardLayout();
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        const LRESULT result = CallWindowProcW(g_app->oldEditProc, hwnd, message, wParam, lParam);
        if (message == WM_KILLFOCUS) {
            g_app->popupSearchInputActive = false;
            g_app->popupSearchControlDown = false;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        if (g_app->popup) InvalidateRect(g_app->popup, nullptr, FALSE);
        return result;
    }
    if (message == WM_KEYDOWN) {
        if (shortcutMatches(g_app->settingsData.popupPlainPasteHotkey, static_cast<UINT>(wParam))) {
            sendPaste(PasteMode::PlainText);
            return 0;
        }
        if (shortcutMatches(g_app->settingsData.popupRichPasteHotkey, static_cast<UINT>(wParam))) {
            sendPaste(PasteMode::RichText);
            return 0;
        }
        if (shortcutMatches(g_app->settingsData.popupPasteHotkey, static_cast<UINT>(wParam))) {
            sendPaste();
            return 0;
        }
        if (shortcutMatches(g_app->settingsData.popupCloseHotkey, static_cast<UINT>(wParam))) {
            closePopup();
            return 0;
        }
        if (wParam == VK_DOWN || wParam == VK_UP || wParam == VK_TAB) {
            PostMessageW(g_app->hidden, kPopupKeyboardMessage, wParam, 0);
            return 0;
        }
        if (shortcutMatches(g_app->settingsData.popupSettingsHotkey, static_cast<UINT>(wParam))) {
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
    return kFilterOther;
}

RECT automaticFilterRect(int slot, int scrollOffset = 0) {
    static const int widths[] = {44, 44, 44, 46, 56, 44};
    int left = ui(16);
    for (int i = 0; i < slot; ++i) left += ui(widths[i] + 6);
    left -= scrollOffset;
    return RECT{left, ui(kPopupFilterTop), left + ui(widths[slot]), ui(kPopupFilterBottom)};
}

int filterContentWidth() {
    static const int widths[] = {44, 44, 44, 46, 56, 44};
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
    if (command == kFilterOther) return g_app->filterType == 4 && !g_app->pinnedOnly;
    if (command == kFilterText) return g_app->filterType == 1 && !g_app->pinnedOnly;
    if (command == kFilterImage) return g_app->filterType == 2 && !g_app->pinnedOnly;
    if (command == kFilterFiles) return g_app->filterType == 3 && !g_app->pinnedOnly;
    return false;
}

void paintSettingsEditBorders(HWND hwnd, HDC dc);

struct SettingsRowLayout {
    const wchar_t* label = nullptr;
    int top = 0;
    int height = 0;
    std::vector<int> controlIds;
    std::vector<int> controlWidths;
    std::vector<int> controlHeights;
    std::vector<int> controlX;
};

struct SettingsCardLayout {
    const wchar_t* title = nullptr;
    int top = 0;
    int bottom = 0;
    std::vector<SettingsRowLayout> rows;
};

struct SettingsLayout {
    std::vector<SettingsCardLayout> cards;
    int contentBottom = 0;
};

int settingsTextHeight(HWND hwnd, const wchar_t* text, int width) {
    if (!hwnd || !text || !*text) return 16;
    HDC dc = GetDC(hwnd);
    if (!dc) return 16;
    HFONT font = g_app->settingsFont;
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    RECT rect{0, 0, ui(std::max(1, width)), 0};
    DrawTextW(dc, text, -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    if (oldFont) SelectObject(dc, oldFont);
    ReleaseDC(hwnd, dc);
    return std::max(16, MulDiv(rect.bottom, 96, static_cast<int>(g_uiDpi)));
}

SettingsRowLayout makeSettingsRow(HWND hwnd, const wchar_t* label,
                                  std::initializer_list<int> ids,
                                  std::initializer_list<int> widths,
                                  std::initializer_list<int> heights,
                                  int contentWidth) {
    SettingsRowLayout row;
    row.label = label;
    row.controlIds.assign(ids.begin(), ids.end());
    row.controlWidths.assign(widths.begin(), widths.end());
    row.controlHeights.assign(heights.begin(), heights.end());
    for (std::size_t i = 0; i < row.controlIds.size(); ++i) {
        if (HWND control = GetDlgItem(hwnd, row.controlIds[i])) {
            RECT controlRect{};
            GetClientRect(control, &controlRect);
            const int widthInLogicalUnits = MulDiv(controlRect.right, 96, static_cast<int>(g_uiDpi));
            const int heightInLogicalUnits = MulDiv(controlRect.bottom, 96, static_cast<int>(g_uiDpi));
            if (widthInLogicalUnits > 0) row.controlWidths[i] = widthInLogicalUnits;
            if (heightInLogicalUnits > 0) row.controlHeights[i] = heightInLogicalUnits;
        }
    }
    int controlsWidth = 0;
    for (std::size_t i = 0; i < row.controlWidths.size(); ++i) {
        controlsWidth += row.controlWidths[i];
        if (i > 0) controlsWidth += 8;
    }
    const int labelWidth = std::max(80, contentWidth - controlsWidth - 28);
    int height = settingsTextHeight(hwnd, label, labelWidth) + 12;
    for (const int controlHeight : row.controlHeights) height = std::max(height, controlHeight + 12);
    row.height = std::max(42, height);
    return row;
}

void placeSettingsCard(SettingsCardLayout& card, int& cursor) {
    card.top = cursor;
    cursor += 40;
    for (SettingsRowLayout& row : card.rows) {
        row.top = cursor;
        cursor += row.height;
    }
    card.bottom = cursor + 12;
    cursor = card.bottom + 12;
}

SettingsLayout buildSettingsLayout(HWND hwnd) {
    SettingsLayout layout;
    RECT client{};
    GetClientRect(hwnd, &client);
    const int clientWidth = std::max(1, MulDiv(client.right, 96, static_cast<int>(g_uiDpi)));
    const int contentWidth = std::max(1, clientWidth - kSettingsSidebarWidth - 40);
    int cursor = 74;
    auto makeCard = [&](const wchar_t* title, std::vector<SettingsRowLayout> rows,
                        int extraHeight = 0) {
        SettingsCardLayout card;
        card.title = title;
        card.rows = std::move(rows);
        placeSettingsCard(card, cursor);
        if (extraHeight > 0) {
            card.bottom += extraHeight;
            cursor += extraHeight;
        }
        layout.cards.push_back(std::move(card));
    };
    if (g_app->settingsTab == 0) {
        makeCard(settingsLocale().appearanceCard, {
            makeSettingsRow(hwnd, settingsLocale().darkTheme,
                            {kSettingDark}, {36}, {20}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().language,
                            {kSettingLanguage}, {150}, {30}, contentWidth)
        });
        makeCard(settingsLocale().systemCard, {
            makeSettingsRow(hwnd, settingsLocale().pauseMonitoring,
                            {kSettingPause}, {36}, {20}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().startWithWindows,
                            {kSettingStartup}, {36}, {20}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().showSettingsOnStartup,
                            {kSettingStartupSettings}, {36}, {20}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().showStartupNotification,
                            {kSettingStartupNotification}, {36}, {20}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().runAsAdministrator,
                            {kSettingRunAsAdministrator}, {36}, {20}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().searchInputCompatibility,
                            {kSettingSearchImeCompatibility}, {36}, {20}, contentWidth)
        });
    } else if (g_app->settingsTab == kSettingsShortcutPage) {
        makeCard(settingsLocale().importantSystemShortcut, {
            makeSettingsRow(hwnd, settingsLocale().forceReplaceWinV,
                            {kSettingWinV}, {36}, {20}, contentWidth)
        });
        makeCard(settingsLocale().globalShortcuts, {
            makeSettingsRow(hwnd, settingsLocale().openClipboardHistory,
                            {kSettingShortcutHistory}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().openSettings,
                            {kSettingShortcutSettings}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().pauseResumeMonitoring,
                            {kSettingShortcutPause}, {150}, {30}, contentWidth)
        });
        makeCard(settingsLocale().historyWindowShortcuts, {
            makeSettingsRow(hwnd, settingsLocale().pasteSelectedItem,
                            {kSettingShortcutPaste}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().promotePastedItem,
                            {kSettingPromotePastedItem}, {36}, {20}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().pastePlainText,
                            {kSettingShortcutPastePlain}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().pasteRichText,
                            {kSettingShortcutPasteRich}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().closeHistoryWindow,
                            {kSettingShortcutClosePopup}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().openSettingsInHistory,
                            {kSettingShortcutPopupSettings}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().clearHistoryFilter,
                            {kSettingShortcutClearFilter}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().deleteSelectedRecord,
                            {kSettingShortcutDelete}, {150}, {30}, contentWidth)
        });
        makeCard(settingsLocale().registrationStatus, {}, 48);
    } else if (g_app->settingsTab == 2) {
        makeCard(settingsLocale().dataRetention, {
            makeSettingsRow(hwnd, settingsLocale().maximumRecords,
                            {kSettingMaxItems}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().retentionDays,
                            {kSettingRetentionDays}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().maximumDiskSpace,
                            {kSettingMaxDiskMb}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().maximumItemSize,
                            {kSettingMaxContentMb}, {150}, {30}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().cacheDirectory,
                            {kSettingDataDirectory, kSettingBrowseDataDirectory},
                            {360, 64}, {30, 30}, contentWidth)
        });
        const wchar_t* labels[] = {
            settingsLocale().text, settingsLocale().images, settingsLocale().files
        };
        const int clearIds[] = {kSettingClearText, kSettingClearImage, kSettingClearFiles};
        std::vector<SettingsRowLayout> categoryRows;
        for (int i = 0; i < kStorageCategoryCount; ++i) {
            categoryRows.push_back(makeSettingsRow(hwnd, labels[i],
                                                    {kSettingCategoryMaxBase + i,
                                                     kSettingCategoryDiskBase + i, clearIds[i]},
                                                    {70, 70, 64}, {30, 30, 30}, contentWidth));
        }
        makeCard(settingsLocale().categoryStorage, std::move(categoryRows), 34);
        SettingsCardLayout& categoryCard = layout.cards.back();
        for (SettingsRowLayout& row : categoryCard.rows) row.top += 20;
        categoryCard.bottom += 20;
        cursor += 20;
        SettingsCardLayout stats;
        stats.title = settingsLocale().historyStatistics;
        stats.top = cursor;
        stats.bottom = cursor + 160;
        SettingsRowLayout statsAction = makeSettingsRow(
            hwnd, nullptr, {kSettingClear}, {100}, {30}, contentWidth);
        statsAction.top = stats.top + 110;
        stats.rows.push_back(std::move(statsAction));
        layout.cards.push_back(std::move(stats));
        cursor += 172;
    } else if (g_app->settingsTab == 3) {
        makeCard(settingsLocale().privacyProtection, {
            makeSettingsRow(hwnd, settingsLocale().protectHistory,
                            {kSettingEncrypt}, {36}, {20}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().ignoredApplications,
                            {kSettingIgnoredApps}, {150}, {60}, contentWidth),
            makeSettingsRow(hwnd, settingsLocale().sensitiveContentExpiry,
                            {kSettingSensitiveExpiry}, {150}, {30}, contentWidth)
        }, 90);
        makeCard(settingsLocale().protectionScope, {}, 48);
    } else {
        makeCard(settingsLocale().about, {}, 300);
    }
    const int controlRight = clientWidth - 20 - 14;
    for (SettingsCardLayout& card : layout.cards) {
        for (SettingsRowLayout& row : card.rows) {
            int totalWidth = 0;
            for (std::size_t i = 0; i < row.controlWidths.size(); ++i) {
                totalWidth += row.controlWidths[i];
                if (i > 0) totalWidth += 8;
            }
            int x = controlRight - totalWidth;
            row.controlX.clear();
            for (std::size_t i = 0; i < row.controlWidths.size(); ++i) {
                row.controlX.push_back(x);
                x += row.controlWidths[i] + 8;
            }
        }
    }
    layout.contentBottom = std::max(300, cursor - 12);
    return layout;
}

int settingsScrollMax(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int viewport = std::max(0, static_cast<int>(client.bottom) - ui(kSettingsHeaderHeight));
    const int contentHeight = buildSettingsLayout(hwnd).contentBottom;
    return std::max(0, ui(contentHeight) - viewport);
}

int settingsContentY(int logicalY) {
    const bool scrollable = g_app->settingsTab == 0 ||
        g_app->settingsTab == kSettingsShortcutPage || g_app->settingsTab == 2;
    return ui(logicalY) - ui(scrollable ? g_app->settingsScrollOffset : 0);
}

HFONT createCachedFont(int size, int weight, const wchar_t* face) {
    return CreateFontW(-ui(size), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

void releasePaintFonts(HFONT& first, HFONT& second, HFONT& third, HFONT& fourth) {
    if (first) DeleteObject(first);
    if (second) DeleteObject(second);
    if (third) DeleteObject(third);
    if (fourth) DeleteObject(fourth);
    first = nullptr;
    second = nullptr;
    third = nullptr;
    fourth = nullptr;
}

void createSettingsPaintFonts() {
    if (!g_app) return;
    g_app->settingsNavFont = createCachedFont(13, FW_NORMAL, L"Microsoft YaHei");
    g_app->settingsTitleFont = createCachedFont(18, FW_SEMIBOLD, L"Microsoft YaHei");
    g_app->settingsCardTitleFont = createCachedFont(13, FW_SEMIBOLD, L"Microsoft YaHei");
    g_app->settingsBodyFont = createCachedFont(13, FW_NORMAL, L"Microsoft YaHei");
}

void createPopupPaintFonts() {
    if (!g_app) return;
    g_app->popupTitleFont = createCachedFont(13, FW_SEMIBOLD, L"Microsoft YaHei");
    g_app->popupFilterFont = createCachedFont(11, FW_NORMAL, L"Microsoft YaHei");
    g_app->popupPreviewFont = createCachedFont(13, FW_NORMAL, L"Microsoft YaHei");
    g_app->popupMetaFont = createCachedFont(10, FW_NORMAL, L"Microsoft YaHei");
}

void paintSettingsContent(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    COLORREF windowBackground = settingsThemeColor(RGB(240, 244, 248), RGB(21, 26, 34));
    COLORREF sidebarBackground = settingsThemeColor(RGB(232, 238, 245), RGB(26, 33, 44));
    COLORREF cardBackground = settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
    COLORREF line = settingsThemeColor(RGB(200, 211, 222), RGB(46, 57, 71));
    COLORREF text = settingsThemeColor(RGB(30, 41, 59), RGB(226, 232, 240));
    COLORREF secondary = settingsThemeColor(RGB(95, 113, 131), RGB(143, 161, 179));
    COLORREF accent = settingsAccentColor();
    if (highContrastEnabled()) {
        windowBackground = GetSysColor(COLOR_WINDOW);
        sidebarBackground = GetSysColor(COLOR_BTNFACE);
        cardBackground = GetSysColor(COLOR_WINDOW);
        line = GetSysColor(COLOR_WINDOWTEXT);
        text = GetSysColor(COLOR_WINDOWTEXT);
        secondary = GetSysColor(COLOR_WINDOWTEXT);
        accent = GetSysColor(COLOR_HIGHLIGHT);
    }
    HBRUSH backgroundBrush = CreateSolidBrush(windowBackground);
    FillRect(dc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);
    HBRUSH sidebarBrush = CreateSolidBrush(sidebarBackground);
    RECT sidebarRect{0, 0, ui(kSettingsSidebarWidth), client.bottom};
    FillRect(dc, &sidebarRect, sidebarBrush);
    DeleteObject(sidebarBrush);

    RECT topbarRect{ui(kSettingsSidebarWidth), 0, client.right, ui(kSettingsHeaderHeight)};
    HBRUSH topbarBrush = CreateSolidBrush(cardBackground);
    FillRect(dc, &topbarRect, topbarBrush);
    DeleteObject(topbarBrush);

    const int contentLeft = ui(kSettingsSidebarWidth + 20);
    const int contentRight = client.right - ui(20);
    auto drawRounded = [&](RECT rect, COLORREF fill, COLORREF border, int radiusLogical = 10) {
        if (g_app->gdiplusToken != 0) {
            auto makeColor = [](COLORREF value) {
                return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
            };
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            const Gdiplus::RectF box(static_cast<float>(rect.left) + 0.5f,
                                     static_cast<float>(rect.top) + 0.5f,
                                     static_cast<float>(rect.right - rect.left - 1),
                                     static_cast<float>(rect.bottom - rect.top - 1));
            const float radius = std::min(static_cast<float>(ui(radiusLogical)),
                                          std::min(box.Width, box.Height) / 2.0f);
            const float diameter = radius * 2.0f;
            Gdiplus::GraphicsPath path;
            path.AddArc(box.X, box.Y, diameter, diameter, 180.0f, 90.0f);
            path.AddArc(box.X + box.Width - diameter, box.Y, diameter, diameter, 270.0f, 90.0f);
            path.AddArc(box.X + box.Width - diameter, box.Y + box.Height - diameter,
                        diameter, diameter, 0.0f, 90.0f);
            path.AddArc(box.X, box.Y + box.Height - diameter, diameter, diameter, 90.0f, 90.0f);
            path.CloseFigure();
            Gdiplus::SolidBrush brush(makeColor(fill));
            Gdiplus::Pen pen(makeColor(border), 1.0f);
            graphics.FillPath(&brush, &path);
            graphics.DrawPath(&pen, &path);
            return;
        }
        (void)rect;
        (void)fill;
        (void)border;
        (void)radiusLogical;
    };

    drawGdiLine(dc, ui(kSettingsSidebarWidth), 0, ui(kSettingsSidebarWidth), client.bottom, line);
    drawGdiLine(dc, ui(kSettingsSidebarWidth), ui(kSettingsHeaderHeight - 1),
                client.right, ui(kSettingsHeaderHeight - 1), line);

    HFONT navFont = g_app->settingsNavFont;
    HFONT titleFont = g_app->settingsTitleFont;
    HFONT cardTitleFont = g_app->settingsCardTitleFont;
    HFONT bodyFont = g_app->settingsBodyFont;
    SetBkMode(dc, TRANSPARENT);
    const wchar_t* navLabels[] = {
        settingsLocale().navGeneral, settingsLocale().navShortcuts, settingsLocale().navStorage,
        settingsLocale().navPrivacy, settingsLocale().navAbout
    };
    const int activeTop = 50 + g_app->settingsTab * 38;
    drawRounded(RECT{ui(12), ui(activeTop), ui(168), ui(activeTop + 34)},
                settingsAccentSoftColor(), settingsAccentSoftColor(), 6);
    if (g_app->hoveredSettingsTab >= 0 && g_app->hoveredSettingsTab != g_app->settingsTab) {
        const int hoverTop = 50 + g_app->hoveredSettingsTab * 38;
        drawRounded(RECT{ui(12), ui(hoverTop), ui(168), ui(hoverTop + 34)},
                    settingsAccentSoftColor(), settingsAccentSoftColor(), 6);
    }
    SelectObject(dc, navFont);
    SetTextColor(dc, secondary);
    RECT menuTitle{ui(24), ui(22), ui(156), ui(42)};
    DrawTextW(dc, L"ClipLite", -1, &menuTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    for (int i = 0; i < 5; ++i) {
        const int top = 54 + i * 38;
        const bool active = i == g_app->settingsTab;
        const bool hovered = i == g_app->hoveredSettingsTab;
        const COLORREF navColor = active || hovered ? accent : secondary;
        SetTextColor(dc, navColor);
        drawSettingsNavIcon(dc, i, ui(24), ui(top + 3), navColor);
        RECT label{ui(52), ui(top), ui(168), ui(top + 30)};
        DrawTextW(dc, navLabels[i], -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(dc, titleFont);
    SetTextColor(dc, text);
    const wchar_t* titles[] = {
        settingsLocale().titleGeneral, settingsLocale().titleShortcuts, settingsLocale().titleStorage,
        settingsLocale().titlePrivacy, settingsLocale().titleAbout
    };
    RECT pageTitle{contentLeft, ui(20), contentRight, ui(48)};
    DrawTextW(dc, titles[g_app->settingsTab], -1, &pageTitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const int themeWidth = ui(kSettingsThemeWidth);
    const int themeLeft = contentRight - themeWidth;
    const int themeTop = ui(14);
    const int accentGap = ui(6);
    const int accentSize = ui(14);
    const int accentRight = themeLeft - ui(14);
    const int accentLeft = accentRight - accentSize * 4 - accentGap * 3;
    SelectObject(dc, bodyFont);
    SetTextColor(dc, g_app->settingsActionFeedback.empty()
        ? secondary
        : (g_app->settingsActionFeedbackSuccess ? accent : RGB(185, 28, 28)));
    RECT saveState{accentLeft - ui(86), ui(20), accentLeft - ui(8), ui(42)};
    const wchar_t* saveLabel = g_app->settingsActionFeedback.empty()
        ? settingsLocale().autoSaved : g_app->settingsActionFeedback.c_str();
    DrawTextW(dc, saveLabel, -1, &saveState,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const COLORREF themeSurface = settingsThemeColor(RGB(232, 238, 245), RGB(26, 33, 44));
    const COLORREF themeLine = settingsThemeColor(RGB(200, 211, 222), RGB(46, 57, 71));
    drawRounded(RECT{themeLeft, themeTop, themeLeft + themeWidth, themeTop + ui(30)},
                themeSurface, themeLine, 15);
    const int themeMode = std::clamp(g_app->settingsData.themeMode, 0, 2);
    const int themeSegmentWidth = ui(kSettingsThemeSegmentWidth);
    for (int i = 0; i < 3; ++i) {
        RECT modeRect{themeLeft + ui(3) + i * themeSegmentWidth, themeTop + ui(3),
                      themeLeft + ui(3) + (i + 1) * themeSegmentWidth, themeTop + ui(27)};
        const bool selected = themeMode == i;
        const bool hovered = g_app->hoveredSettingsThemeMode == i;
        if (selected || hovered) {
            const COLORREF segmentBackground = selected ? accent : settingsAccentSoftColor();
            drawRounded(modeRect, segmentBackground, segmentBackground, 12);
        }
        const COLORREF iconColor = selected ? RGB(255, 255, 255) :
            (hovered ? accent : secondary);
        drawSettingsThemeIcon(dc, i,
                              (modeRect.left + modeRect.right) / 2,
                              (modeRect.top + modeRect.bottom) / 2,
                              iconColor);
    }
    const COLORREF accentColors[] = {
        RGB(37, 99, 235), RGB(124, 58, 237), RGB(39, 124, 97), RGB(217, 119, 6)
    };
    for (int i = 0; i < kAccentCount; ++i) {
        const int left = accentLeft + i * (accentSize + accentGap);
        drawSettingsAccentDot(dc, left, themeTop + ui(7), accentSize,
                              accentColors[i], g_app->settingsData.accent == i, text);
    }

    auto drawCard = [&](int top, int bottom) {
        RECT rect{contentLeft, settingsContentY(top), contentRight, settingsContentY(bottom)};
        drawRounded(rect, cardBackground, line);
    };
    auto drawCardTitle = [&](const wchar_t* label, int top) {
        SelectObject(dc, cardTitleFont);
        SetTextColor(dc, text);
        RECT title{contentLeft + ui(14), settingsContentY(top), contentRight - ui(14),
                   settingsContentY(top + 20)};
        DrawTextW(dc, label, -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        drawGdiLine(dc, contentLeft + ui(14), settingsContentY(top + 27),
                    contentRight - ui(14), settingsContentY(top + 27), line);
    };
    auto drawLayoutRow = [&](const SettingsRowLayout& row) {
        if (!row.label) return;
        SelectObject(dc, bodyFont);
        SetTextColor(dc, text);
        const int labelRight = row.controlX.empty() ? contentRight - ui(14) : ui(row.controlX.front() - 8);
        const int labelLeftLogical = kSettingsSidebarWidth + 20 + 14;
        const int labelRightLogical = row.controlX.empty()
            ? MulDiv(client.right, 96, static_cast<int>(g_uiDpi)) - 20 - 14
            : row.controlX.front() - 8;
        const int labelHeight = settingsTextHeight(hwnd, row.label,
                                                    std::max(1, labelRightLogical - labelLeftLogical));
        const int labelTop = row.top + std::max(0, (row.height - labelHeight) / 2);
        RECT label{contentLeft + ui(14), settingsContentY(labelTop), labelRight,
                   settingsContentY(labelTop + labelHeight)};
        DrawTextW(dc, row.label, -1, &label,
                  DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    };
    auto drawStats = [&](int top) {
        const std::size_t textCount = g_app->store.countType(ClipType::Text);
        const std::size_t imageCount = g_app->store.countType(ClipType::Image);
        const std::size_t fileCount = g_app->store.countType(ClipType::Files);
        std::size_t pinnedCount = 0;
        for (const ClipItem& item : g_app->store.items()) {
            if (item.pinned) ++pinnedCount;
        }
        wchar_t summary[192]{};
        const std::wstring totalSize = formatByteSize(g_app->store.diskBytes());
        swprintf_s(summary, settingsLocale().totalHistoryFormat,
                   g_app->store.activeCount(), totalSize.c_str());
        SelectObject(dc, bodyFont);
        SetTextColor(dc, secondary);
        RECT summaryRect{contentLeft + ui(14), settingsContentY(top + 37), contentRight - ui(14),
                         settingsContentY(top + 61)};
        DrawTextW(dc, summary, -1, &summaryRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const wchar_t* labels[] = {
            settingsLocale().text, settingsLocale().images,
            settingsLocale().files, settingsLocale().pinned
        };
        const std::size_t counts[] = {textCount, imageCount, fileCount, pinnedCount};
        for (int i = 0; i < 4; ++i) {
            wchar_t value[64]{};
            swprintf_s(value, L"%zu", counts[i]);
            const int x = contentLeft + ui(16 + (i % 2) * 190);
            const int y = top + 68 + (i / 2) * 23;
            SetTextColor(dc, secondary);
            RECT labelRect{x, settingsContentY(y), x + ui(42), settingsContentY(y + 20)};
            DrawTextW(dc, labels[i], -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(dc, text);
            RECT valueRect{x + ui(50), settingsContentY(y), x + ui(100), settingsContentY(y + 20)};
            DrawTextW(dc, value, -1, &valueRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    };

    const SettingsLayout layout = buildSettingsLayout(hwnd);
    const int bodyClip = SaveDC(dc);
    IntersectClipRect(dc, ui(kSettingsSidebarWidth), ui(kSettingsHeaderHeight), client.right, client.bottom);
    for (const SettingsCardLayout& card : layout.cards) {
        drawCard(card.top, card.bottom);
        drawCardTitle(card.title, card.top + 12);
        for (const SettingsRowLayout& row : card.rows) drawLayoutRow(row);
    }
    if (g_app->settingsTab == 2 && layout.cards.size() >= 3) {
        drawStats(layout.cards[2].top);
        const SettingsCardLayout& categoryCard = layout.cards[1];
        if (categoryCard.rows.size() >= kStorageCategoryCount) {
            SelectObject(dc, bodyFont);
            SetTextColor(dc, secondary);
            const SettingsRowLayout& firstRow = categoryCard.rows.front();
            RECT maxHeader{ui(firstRow.controlX[0]), settingsContentY(categoryCard.top + 40),
                           ui(firstRow.controlX[0] + 70), settingsContentY(categoryCard.top + 54)};
            RECT diskHeader{ui(firstRow.controlX[1]), settingsContentY(categoryCard.top + 40),
                            ui(firstRow.controlX[1] + 70), settingsContentY(categoryCard.top + 54)};
            DrawTextW(dc, settingsLocale().records, -1, &maxHeader,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
             DrawTextW(dc, settingsLocale().space, -1, &diskHeader,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            const ClipType types[] = {ClipType::Text, ClipType::Image, ClipType::Files};
            for (int i = 0; i < kStorageCategoryCount; ++i) {
                const SettingsRowLayout& row = categoryCard.rows[static_cast<std::size_t>(i)];
                wchar_t count[64]{};
                const std::wstring categorySize = formatByteSize(g_app->store.bytesType(types[i]));
                swprintf_s(count, settingsLocale().currentCategoryFormat,
                           g_app->store.countType(types[i]), categorySize.c_str());
                RECT countRect{contentLeft + ui(62), settingsContentY(row.top),
                               ui(row.controlX.front() - 8), settingsContentY(row.top + row.height)};
                SetTextColor(dc, secondary);
                DrawTextW(dc, count, -1, &countRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            RECT note{contentLeft + ui(14), settingsContentY(categoryCard.bottom - 48),
                      contentRight - ui(14), settingsContentY(categoryCard.bottom - 22)};
            DrawTextW(dc, settingsLocale().categoryNote, -1, &note,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
    } else if (g_app->settingsTab == kSettingsShortcutPage && !layout.cards.empty()) {
        SelectObject(dc, bodyFont);
        SetTextColor(dc, secondary);
        const SettingsCardLayout& card = layout.cards.back();
        RECT description{contentLeft + ui(14), settingsContentY(card.bottom - 38), contentRight - ui(14),
                         settingsContentY(card.bottom - 12)};
        DrawTextW(dc, g_app->shortcutRegistrationWarning
                      ? settingsLocale().shortcutRegistrationWarning
                      : settingsLocale().shortcutModifierRequirement,
                  -1, &description, DT_LEFT | DT_WORDBREAK);
    } else if (g_app->settingsTab == 3 && layout.cards.size() >= 2) {
        SelectObject(dc, bodyFont);
        SetTextColor(dc, secondary);
        const SettingsCardLayout& privacyCard = layout.cards.front();
        RECT encryptionNote{contentLeft + ui(14), settingsContentY(privacyCard.bottom - 80),
                            contentRight - ui(14), settingsContentY(privacyCard.bottom - 12)};
        DrawTextW(dc, settingsLocale().encryptionNote, -1, &encryptionNote,
                  DT_LEFT | DT_WORDBREAK);
        const SettingsCardLayout& card = layout.cards.back();
        RECT description{contentLeft + ui(14), settingsContentY(card.bottom - 38), contentRight - ui(14),
                         settingsContentY(card.bottom - 12)};
        DrawTextW(dc, settingsLocale().privacyNote, -1, &description,
                  DT_LEFT | DT_WORDBREAK);
    } else if (g_app->settingsTab == 4 && !layout.cards.empty()) {
        SelectObject(dc, bodyFont);
        SetTextColor(dc, text);
        const wchar_t* about[] = {
            settingsLocale().aboutApplication, settingsLocale().aboutVersion,
            settingsLocale().aboutStorageFormat, settingsLocale().aboutDataDirectory
        };
        for (int i = 0; i < 4; ++i) {
            RECT aboutRect{contentLeft + ui(14), settingsContentY(layout.cards.front().top + 50 + i * 34),
                           contentRight - ui(14), settingsContentY(layout.cards.front().top + 76 + i * 34)};
            DrawTextW(dc, about[i], -1, &aboutRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        const std::wstring logFile = settingsLocale().logFilePrefix + diagnosticLogPath();
        RECT logRect{contentLeft + ui(14), settingsContentY(layout.cards.front().top + 186),
                     contentRight - ui(14), settingsContentY(layout.cards.front().top + 212)};
        DrawTextW(dc, logFile.c_str(), -1, &logRect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    RestoreDC(dc, bodyClip);

#ifdef _DEBUG
    {
        HPEN debugPen = CreatePen(PS_DASH, ui(1), RGB(220, 70, 70));
        HGDIOBJ debugPreviousPen = SelectObject(dc, debugPen);
        HGDIOBJ debugPreviousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RECT headerRect{ui(kSettingsSidebarWidth), 0, client.right, ui(kSettingsHeaderHeight)};
        Rectangle(dc, headerRect.left, headerRect.top, headerRect.right, headerRect.bottom);
        auto drawDebugCard = [&](int top, int bottom) {
            Rectangle(dc, contentLeft, settingsContentY(top), contentRight,
                      settingsContentY(bottom));
        };
        const SettingsLayout debugLayout = buildSettingsLayout(hwnd);
        for (const SettingsCardLayout& card : debugLayout.cards) {
            drawDebugCard(card.top, card.bottom);
        }
        SelectObject(dc, debugPreviousBrush);
        SelectObject(dc, debugPreviousPen);
        DeleteObject(debugPen);
        HFONT debugFont = CreateFontW(-ui(10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
        HFONT oldDebugFont = reinterpret_cast<HFONT>(SelectObject(dc, debugFont));
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(255, 250, 210));
        SetTextColor(dc, RGB(140, 30, 30));
        wchar_t marker[96]{};
        swprintf_s(marker, L"DEBUG body=%d scroll=%d", kSettingsHeaderHeight,
                   g_app->settingsScrollOffset);
        RECT markerRect{contentLeft + ui(4), ui(2), contentRight, ui(16)};
        DrawTextW(dc, marker, -1, &markerRect, DT_LEFT | DT_SINGLELINE);
        paintSettingsEditBorders(hwnd, dc);
        SelectObject(dc, oldDebugFont);
        DeleteObject(debugFont);
    }
#endif
}

void paintSettings(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = buffer ? CreateCompatibleBitmap(dc, width, height) : nullptr;
    if (!buffer || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        paintSettingsContent(hwnd, dc);
        return;
    }
    HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
    paintSettingsContent(hwnd, buffer);
    BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
}

void paintPopupContent(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    COLORREF background = settingsThemeColor(RGB(242, 244, 247), RGB(38, 42, 48));
    COLORREF text = settingsThemeColor(RGB(26, 26, 26), RGB(240, 243, 247));
    COLORREF secondary = settingsThemeColor(RGB(102, 102, 102), RGB(175, 183, 193));
    COLORREF border = settingsThemeColor(RGB(209, 213, 219), RGB(75, 83, 92));
    COLORREF card = settingsThemeColor(RGB(255, 255, 255), RGB(48, 53, 60));
    COLORREF cardBorder = settingsThemeColor(RGB(234, 234, 234), RGB(67, 74, 82));
    COLORREF chip = settingsThemeColor(RGB(255, 255, 255), RGB(55, 61, 69));
    COLORREF accent = settingsAccentColor();
    if (highContrastEnabled()) {
        background = GetSysColor(COLOR_WINDOW);
        text = GetSysColor(COLOR_WINDOWTEXT);
        secondary = GetSysColor(COLOR_WINDOWTEXT);
        border = GetSysColor(COLOR_WINDOWTEXT);
        card = GetSysColor(COLOR_WINDOW);
        cardBorder = GetSysColor(COLOR_WINDOWTEXT);
        chip = GetSysColor(COLOR_WINDOW);
        accent = GetSysColor(COLOR_HIGHLIGHT);
    }
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(dc, &client, brush);
    DeleteObject(brush);
    const int popupFrameClip = SaveDC(dc);
    IntersectClipRect(dc, ui(kPopupBorderInset), ui(kPopupBorderInset),
                      client.right - ui(kPopupBorderInset),
                      client.bottom - ui(kPopupBorderInset));
    drawGdiRoundedSurface(dc,
                          RECT{ui(kPopupBorderInset), ui(kPopupBorderInset),
                               client.right - ui(kPopupBorderInset),
                               client.bottom - ui(kPopupBorderInset)},
                          background, border, kPopupCornerRadius);

    SetBkMode(dc, TRANSPARENT);
    HFONT titleFont = g_app->popupTitleFont;
    HFONT filterFont = g_app->popupFilterFont;
    HFONT previewFont = g_app->popupPreviewFont;
    HFONT metaFont = g_app->popupMetaFont;
    RECT paintClip{};
    const int paintClipType = GetClipBox(dc, &paintClip);
    HGDIOBJ old = SelectObject(dc, titleFont);
    SetTextColor(dc, text);
    RECT titleRect{ui(16), ui(14), ui(82), ui(46)};
    if (g_app->hoveredHeader) {
        drawGdiRoundedSurface(dc, RECT{ui(12), ui(10), ui(86), ui(50)},
                              settingsAccentSoftColor(), settingsAccentSoftColor(), 4);
    }
    DrawTextW(dc, settingsLocale().popupTitle, -1, &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const COLORREF searchBorder = g_app->searchEdit && g_app->popupSearchInputActive
        ? accent : border;
    const RECT searchRect{ui(kPopupSearchLeft), ui(12), ui(kPopupSearchRight), ui(48)};
    drawGdiRoundedSurface(dc, searchRect,
                          settingsThemeColor(RGB(255, 255, 255), RGB(48, 53, 60)),
                          searchBorder, 6);
    drawSearchIcon(dc, ui(kPopupSearchLeft + 14), ui(30),
                   g_app->searchEdit && g_app->popupSearchInputActive ? accent : secondary);
    const wchar_t* clearLabel = settingsLocale().popupClearSearch;
    SelectObject(dc, filterFont);
    RECT clearRect{ui(kPopupClearLeft), ui(14), ui(kPopupClearRight), ui(46)};
    if (g_app->hoveredFilter == 7) {
        drawGdiRoundedSurface(dc, clearRect, settingsAccentSoftColor(),
                              settingsAccentSoftColor(), 4);
    }
    SetTextColor(dc, g_app->hoveredFilter == 7 ? accent : secondary);
    DrawTextW(dc, clearLabel, -1, &clearRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const RECT pinRect{ui(kPopupPinLeft), ui(14), ui(kPopupPinRight), ui(46)};
    const RECT closeRect{ui(kPopupCloseLeft), ui(14), client.right - ui(16), ui(46)};
    if (g_app->hoveredFilter == 8 && !g_app->popupPinned) {
        drawGdiRoundedSurface(dc, pinRect, settingsAccentSoftColor(),
                              settingsAccentSoftColor(), 4);
    }
    if (g_app->hoveredFilter == 9) {
        drawGdiRoundedSurface(dc, closeRect, settingsAccentSoftColor(),
                              settingsAccentSoftColor(), 4);
    }
    const COLORREF pinColor = g_app->popupPinned ? RGB(245, 158, 11) :
        (g_app->hoveredFilter == 8 ? accent : secondary);
    drawPinIcon(dc, ui(kPopupPinRight - 2), ui(30), pinColor, g_app->popupPinned);
    drawDeleteIcon(dc, ui(kPopupCloseLeft + 5), ui(25),
                   g_app->hoveredFilter == 9 ? RGB(220, 38, 38) : secondary);

    const wchar_t* filterLabels[] = {
        settingsLocale().popupFilterAll, settingsLocale().popupFilterPinned,
        settingsLocale().popupFilterText, settingsLocale().popupFilterImages,
        settingsLocale().popupFilterFiles, settingsLocale().popupFilterOther
    };
    SelectObject(dc, filterFont);
    const int filterClip = SaveDC(dc);
    IntersectClipRect(dc, ui(16), ui(kPopupFilterTop),
                      client.right - ui(16), ui(kPopupFilterBottom));
    for (int slot = 0; slot < 6; ++slot) {
        const RECT chipRect = automaticFilterRect(slot, g_app->filterScrollOffset);
        const bool active = isAutomaticFilterActive(slot);
        const bool hovered = slot == g_app->hoveredFilter;
        const COLORREF chipBackground = active ? accent :
            (hovered ? settingsThemeColor(RGB(231, 242, 239), RGB(67, 78, 88)) : chip);
        const COLORREF chipBorder = active ? accent : (hovered ? accent : border);
        drawGdiRoundedSurface(dc, chipRect, chipBackground, chipBorder, 4);
        SetTextColor(dc, active || hovered ? (active ? RGB(255, 255, 255) : accent) : secondary);
        DrawTextW(dc, filterLabels[slot], -1, const_cast<RECT*>(&chipRect),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    RestoreDC(dc, filterClip);

    const int listBottom = client.bottom - ui(kPopupBottomPadding);
    const int listClip = SaveDC(dc);
    IntersectClipRect(dc, ui(12), ui(kPopupListTop), client.right - ui(1), listBottom);
    const int scrollStride = popupScrollStride();
    const int firstVisibleRow = g_app->scrollPosition / std::max(1, scrollStride);
    const int scrollRemainder = g_app->scrollPosition % std::max(1, scrollStride);
    int y = ui(kPopupListTop) - scrollRemainder;
    for (int row = firstVisibleRow;
         row < static_cast<int>(g_app->visible.size()); ++row) {
        const ClipItem& item = g_app->store.items()[g_app->visible[static_cast<std::size_t>(row)]];
        const int height = popupCardHeight(item);
        if (y >= listBottom) break;
        RECT rowRect{ui(16), y, client.right - ui(16), y + height};
        if (paintClipType != NULLREGION &&
            (rowRect.bottom <= paintClip.top || rowRect.top >= paintClip.bottom)) {
            y += height + ui(kPopupCardGap);
            continue;
        }
        const bool rowHovered = row == g_app->hoveredRow;
        const bool rowSelected = row == g_app->selected;
        drawGdiRoundedSurface(dc, rowRect,
                              rowSelected ? settingsAccentSoftColor() :
                                  (rowHovered ? settingsThemeColor(RGB(247, 251, 250), RGB(57, 64, 73)) : card),
                              rowSelected || rowHovered ? accent : cardBorder, 6);
        if (rowSelected) {
            drawGdiRoundedSurface(dc,
                                  RECT{rowRect.left + ui(4), rowRect.top + ui(12),
                                        rowRect.left + ui(7), rowRect.bottom - ui(12)},
                                  accent, accent, 2);
        }
        const bool image = isImageType(item.type);
        const int metadataTop = y + height - ui(24);
        if (image && drawImagePreview(dc, item, RECT{rowRect.left + ui(12), y + ui(12),
                                                     rowRect.left + ui(84), y + ui(84)})) {
        } else {
            const std::wstring preview = localizedPopupPreview(item);
            SelectObject(dc, previewFont);
            SetTextColor(dc, text);
            RECT previewRect{rowRect.left + ui(12), y + ui(10), rowRect.right - ui(12),
                             metadataTop - ui(4)};
            DrawTextW(dc, preview.c_str(), -1, &previewRect,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        }
        SelectObject(dc, metaFont);
        const wchar_t* kind = automaticTypeLabel(item.type);
        SetTextColor(dc, secondary);
        const std::wstring source = item.source.empty()
            ? std::wstring(settingsLocale().popupClipboardSource) : utf8ToWide(item.source);
        const bool metadataVisible = metadataTop >= ui(kPopupListTop) &&
            metadataTop + ui(16) <= listBottom;
        if (metadataVisible) {
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
            const bool pinHovered = row == g_app->hoveredPinRow;
            if (item.pinned) {
                drawPinIcon(dc, rowRect.right - ui(10), metadataTop + ui(8),
                            pinHovered ? RGB(217, 119, 6) : RGB(245, 158, 11), true);
            } else if (rowHovered) {
                drawPinIcon(dc, rowRect.right - ui(10), metadataTop + ui(8),
                            pinHovered ? accent : RGB(176, 176, 176));
            }
        }
        if (rowHovered) {
            drawDeleteIcon(dc, rowRect.right - ui(20), y + ui(9),
                           row == g_app->hoveredDeleteRow ? RGB(220, 38, 38) : RGB(176, 176, 176));
        }
        y += height + ui(kPopupCardGap);
    }
    if (g_app->visible.empty()) {
        const wchar_t* empty = settingsLocale().popupEmpty;
        SetTextColor(dc, secondary);
        const int centerX = client.right / 2;
        const int emptyTop = ui(kPopupListTop + 54);
        drawEmptyClipboardIcon(dc, centerX, emptyTop,
                                settingsThemeColor(RGB(178, 187, 194), RGB(115, 126, 138)));
        RECT emptyRect{ui(24), emptyTop + ui(48), client.right - ui(24), emptyTop + ui(72)};
        DrawTextW(dc, empty, -1, &emptyRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    int trackTop = 0;
    int trackBottom = 0;
    int thumbTop = 0;
    int thumbHeight = 0;
    int maxOffset = 0;
    if (popupScrollMetrics(trackTop, trackBottom, thumbTop, thumbHeight, maxOffset)) {
        const RECT scrollRect{client.right - ui(5), thumbTop, client.right - ui(2),
                               thumbTop + thumbHeight};
        const COLORREF scrollColor = settingsThemeColor(RGB(192, 192, 192), RGB(103, 113, 124));
        drawGdiRoundedSurface(dc, scrollRect, scrollColor, scrollColor, 3);
    }
    RestoreDC(dc, listClip);
    RestoreDC(dc, popupFrameClip);
    SelectObject(dc, old);
}

void paintPopup(HWND hwnd, HDC dc, const RECT* updateRect) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);
    if (updateRect) {
        IntersectClipRect(buffer, updateRect->left, updateRect->top,
                          updateRect->right, updateRect->bottom);
    }
    paintPopupContent(hwnd, buffer);
    const RECT paintRect = updateRect ? *updateRect : client;
    BitBlt(dc, paintRect.left, paintRect.top,
           paintRect.right - paintRect.left, paintRect.bottom - paintRect.top,
           buffer, paintRect.left, paintRect.top, SRCCOPY);
    SelectObject(buffer, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
}

bool settingsToggleValue(HWND hwnd);
void setSettingsToggleValue(HWND hwnd, bool enabled);
int settingsLanguageSelection(HWND hwnd);
void setSettingsLanguageSelection(HWND hwnd, int selection);
void configureSettingsEdit(HWND hwnd);

bool isSettingsShortcut(int id) {
    return id >= kSettingShortcutHistory && id <= kSettingShortcutDelete;
}

ShortcutBinding* settingsShortcutBinding(Settings& settings, int id) {
    if (id == kSettingShortcutHistory) return &settings.historyHotkey;
    if (id == kSettingShortcutSettings) return &settings.settingsHotkey;
    if (id == kSettingShortcutPause) return &settings.pauseHotkey;
    if (id == kSettingShortcutPaste) return &settings.popupPasteHotkey;
    if (id == kSettingShortcutPastePlain) return &settings.popupPlainPasteHotkey;
    if (id == kSettingShortcutPasteRich) return &settings.popupRichPasteHotkey;
    if (id == kSettingShortcutClosePopup) return &settings.popupCloseHotkey;
    if (id == kSettingShortcutPopupSettings) return &settings.popupSettingsHotkey;
    if (id == kSettingShortcutClearFilter) return &settings.popupClearFilterHotkey;
    if (id == kSettingShortcutDelete) return &settings.popupDeleteHotkey;
    return nullptr;
}

std::wstring shortcutKeyName(UINT virtualKey) {
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        return std::wstring(1, static_cast<wchar_t>(virtualKey));
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        return std::wstring(1, static_cast<wchar_t>(virtualKey));
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        return L"F" + std::to_wstring(virtualKey - VK_F1 + 1);
    }
    if (virtualKey == VK_SPACE) return L"Space";
    if (virtualKey == VK_ESCAPE) return L"Esc";
    if (virtualKey == VK_RETURN) return L"Enter";
    if (virtualKey == VK_TAB) return L"Tab";
    if (virtualKey == VK_OEM_MINUS) return L"-";
    if (virtualKey == VK_OEM_PLUS) return L"+";
    wchar_t name[64]{};
    const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    if (scanCode != 0 && GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name,
                                         static_cast<int>(sizeof(name) / sizeof(name[0]))) > 0) {
        return name;
    }
    return L"VK " + std::to_wstring(virtualKey);
}

std::wstring formatShortcut(const ShortcutBinding& binding) {
    std::wstring result;
    if (binding.modifiers & MOD_CONTROL) result += L"Ctrl + ";
    if (binding.modifiers & MOD_ALT) result += L"Alt + ";
    if (binding.modifiers & MOD_SHIFT) result += L"Shift + ";
    if (binding.modifiers & MOD_WIN) result += L"Win + ";
    result += shortcutKeyName(binding.virtualKey);
    return result;
}

void refreshSettingsShortcutControls(HWND hwnd) {
    if (!hwnd) return;
    const int ids[] = {kSettingShortcutHistory, kSettingShortcutSettings, kSettingShortcutPause,
                       kSettingShortcutPaste, kSettingShortcutPastePlain, kSettingShortcutPasteRich,
                       kSettingShortcutClosePopup, kSettingShortcutPopupSettings,
                       kSettingShortcutClearFilter, kSettingShortcutDelete};
    for (const int id : ids) {
        HWND control = GetDlgItem(hwnd, id);
        ShortcutBinding* binding = settingsShortcutBinding(g_app->settingsData, id);
        if (control && binding) SetWindowTextW(control, formatShortcut(*binding).c_str());
    }
}

UINT shortcutModifiersFromKeyboard() {
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= MOD_WIN;
    return modifiers;
}

bool shortcutMatches(const ShortcutBinding& binding, UINT virtualKey) {
    return binding.virtualKey == virtualKey &&
           shortcutModifiersFromKeyboard() == binding.modifiers;
}

bool isShortcutModifierKey(UINT virtualKey) {
    return virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
           virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU ||
           virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
           virtualKey == VK_LWIN || virtualKey == VK_RWIN;
}

void cancelSettingsShortcutCapture(HWND hwnd) {
    if (!g_app || !g_app->shortcutCaptureControl) return;
    g_app->shortcutCaptureControl = nullptr;
    refreshSettingsShortcutControls(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void beginSettingsShortcutCapture(HWND hwnd, HWND control) {
    if (!hwnd || !control) return;
    g_app->shortcutCaptureControl = control;
    SetFocus(control);
    SetWindowTextW(control, settingsLocale().pressShortcut);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void captureSettingsShortcut(HWND hwnd, HWND control, UINT virtualKey) {
    if (!hwnd || !control || g_app->shortcutCaptureControl != control) return;
    if (virtualKey == VK_ESCAPE) {
        cancelSettingsShortcutCapture(hwnd);
        return;
    }
    if (isShortcutModifierKey(virtualKey)) return;
    const UINT modifiers = shortcutModifiersFromKeyboard();
    const int id = GetDlgCtrlID(control);
    const bool modifierless = id == kSettingShortcutPaste || id == kSettingShortcutClosePopup ||
        id == kSettingShortcutPopupSettings || id == kSettingShortcutDelete;
    if (modifiers == 0 && !modifierless) {
        SetWindowTextW(control, settingsLocale().needModifier);
        return;
    }
    ShortcutBinding* binding = settingsShortcutBinding(g_app->settingsData, GetDlgCtrlID(control));
    if (!binding) return;
    binding->modifiers = modifiers;
    binding->virtualKey = virtualKey;
    g_app->shortcutCaptureControl = nullptr;
    registerHotkeys();
    refreshSettingsShortcutControls(hwnd);
    saveSettings(g_app->settingsData);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void createSettingsControlsModern(HWND hwnd) {
    auto createToggle = [hwnd](int id, int x, int y, bool checked) {
        HWND control = CreateWindowW(L"BUTTON", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                         BS_CHECKBOX | BS_OWNERDRAW,
                                      ui(x), ui(y), ui(kSettingsToggleWidth), ui(kSettingsToggleHeight), hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        setSettingsToggleValue(control, checked);
        return control;
    };
    auto createShortcut = [hwnd](int id, int y, const ShortcutBinding& binding) {
        HWND control = CreateWindowW(L"BUTTON", formatShortcut(binding).c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                     ui(520), ui(y), ui(150), ui(30), hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                     GetModuleHandleW(nullptr), nullptr);
        return control;
    };

    createToggle(kSettingDark, 640, 182, g_app->settingsData.dark);
    createToggle(kSettingWinV, 640, 112, g_app->settingsData.winV);
    createToggle(kSettingPause, 640, 264, g_app->settingsData.pauseMonitoring);
    createToggle(kSettingStartup, 640, 299, g_app->settingsData.startWithWindows);
    createToggle(kSettingStartupSettings, 640, 334, g_app->settingsData.showSettingsOnStartup);
    createToggle(kSettingStartupNotification, 640, 369, g_app->settingsData.showStartupNotification);
    createToggle(kSettingRunAsAdministrator, 640, 404, g_app->settingsData.runAsAdministrator);
    createToggle(kSettingSearchImeCompatibility, 640, 439,
                 g_app->settingsData.searchImeCompatibility);
    createToggle(kSettingPromotePastedItem, 640, 474,
                 g_app->settingsData.promotePastedItem);
    createToggle(kSettingEncrypt, 640, 116, g_app->settingsData.encryptData);
    createShortcut(kSettingShortcutHistory, 110, g_app->settingsData.historyHotkey);
    createShortcut(kSettingShortcutSettings, 154, g_app->settingsData.settingsHotkey);
    createShortcut(kSettingShortcutPause, 198, g_app->settingsData.pauseHotkey);
    createShortcut(kSettingShortcutPaste, 242, g_app->settingsData.popupPasteHotkey);
    createShortcut(kSettingShortcutPastePlain, 286, g_app->settingsData.popupPlainPasteHotkey);
    createShortcut(kSettingShortcutPasteRich, 330, g_app->settingsData.popupRichPasteHotkey);
    createShortcut(kSettingShortcutClosePopup, 374, g_app->settingsData.popupCloseHotkey);
    createShortcut(kSettingShortcutPopupSettings, 418, g_app->settingsData.popupSettingsHotkey);
    createShortcut(kSettingShortcutClearFilter, 462, g_app->settingsData.popupClearFilterHotkey);
    createShortcut(kSettingShortcutDelete, 506, g_app->settingsData.popupDeleteHotkey);

    HWND language = CreateWindowW(L"STATIC", L"",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_NOTIFY,
                                  ui(520), ui(147), ui(180), ui(30), hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingLanguage)),
                                  GetModuleHandleW(nullptr), nullptr);
    const int languageSelection = g_app->settingsData.language < 0
        ? 0 : g_app->settingsData.language + 1;
    setSettingsLanguageSelection(language, languageSelection);

    wchar_t value[32]{};
    swprintf_s(value, L"%d", g_app->settingsData.maxItems);
    CreateWindowExW(0, L"EDIT", value,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL | ES_NUMBER,
                     ui(520), ui(112), ui(150), ui(30), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingMaxItems)),
                    GetModuleHandleW(nullptr), nullptr);
    swprintf_s(value, L"%d", g_app->settingsData.retentionDays);
    CreateWindowExW(0, L"EDIT", value,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL | ES_NUMBER,
                     ui(520), ui(147), ui(150), ui(30), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingRetentionDays)),
                    GetModuleHandleW(nullptr), nullptr);
    swprintf_s(value, L"%d", g_app->settingsData.maxDiskMb);
    CreateWindowExW(0, L"EDIT", value,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL | ES_NUMBER,
                     ui(520), ui(182), ui(150), ui(30), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingMaxDiskMb)),
                    GetModuleHandleW(nullptr), nullptr);
    swprintf_s(value, L"%d", g_app->settingsData.maxContentMb);
    CreateWindowExW(0, L"EDIT", value,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL | ES_NUMBER,
                     ui(520), ui(217), ui(150), ui(30), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingMaxContentMb)),
                    GetModuleHandleW(nullptr), nullptr);
    const std::wstring dataDirectory = g_app->settingsData.dataDirectory.empty()
        ? clipLiteDataDirectory() : utf8ToWide(g_app->settingsData.dataDirectory);
    CreateWindowExW(0, L"EDIT", dataDirectory.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL,
                    ui(520), ui(252), ui(360), ui(30), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingDataDirectory)),
                    GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", settingsLocale().browse,
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                  ui(888), ui(252), ui(64), ui(30), hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingBrowseDataDirectory)),
                  GetModuleHandleW(nullptr), nullptr);

    std::wstring ignoredApps;
    for (const std::string& app : g_app->settingsData.ignoredApps) {
        if (!ignoredApps.empty()) ignoredApps += L"\r\n";
        ignoredApps += utf8ToWide(app);
    }
    CreateWindowExW(0, L"EDIT", ignoredApps.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL,
                     ui(520), ui(151), ui(150), ui(60), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingIgnoredApps)),
                    GetModuleHandleW(nullptr), nullptr);
    swprintf_s(value, L"%d", g_app->settingsData.sensitiveExpiryHours);
    CreateWindowExW(0, L"EDIT", value,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL | ES_NUMBER,
                     ui(520), ui(235), ui(150), ui(30), hwnd,
                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingSensitiveExpiry)),
                     GetModuleHandleW(nullptr), nullptr);
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        const CategoryLimit& limit = g_app->settingsData.categoryLimits[static_cast<std::size_t>(i)];
        swprintf_s(value, L"%d", limit.maxItems);
        CreateWindowExW(0, L"EDIT", value,
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL | ES_NUMBER,
                        ui(520), ui(285 + i * 48), ui(70), ui(30), hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingCategoryMaxBase + i)),
                        GetModuleHandleW(nullptr), nullptr);
        swprintf_s(value, L"%d", limit.maxDiskMb);
        CreateWindowExW(0, L"EDIT", value,
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOHSCROLL | ES_NUMBER,
                        ui(598), ui(285 + i * 48), ui(70), ui(30), hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingCategoryDiskBase + i)),
                        GetModuleHandleW(nullptr), nullptr);
    }

    CreateWindowW(L"BUTTON", settingsLocale().clearHistory,
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ui(232), ui(360), ui(120), ui(30), hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingClear)),
                  GetModuleHandleW(nullptr), nullptr);
    const int clearIds[] = {kSettingClearText, kSettingClearImage, kSettingClearFiles};
    const wchar_t* clearLabels[] = {
        settingsLocale().clearText, settingsLocale().clearImages, settingsLocale().clearFiles
    };
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        CreateWindowW(L"BUTTON", clearLabels[i],
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ui(232 + i * 82), ui(324),
                      ui(84), ui(26), hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(clearIds[i])),
                      GetModuleHandleW(nullptr), nullptr);
    }
    CreateWindowW(L"BUTTON", settingsLocale().openLog,
                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ui(520), ui(298), ui(150), ui(30), hwnd,
                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingOpenLog)),
                   GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", settingsLocale().supportAuthor,
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ui(520), ui(338), ui(150), ui(30), hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingSupportAuthor)),
                  GetModuleHandleW(nullptr), nullptr);
    CreateWindowW(L"BUTTON", settingsLocale().joinQqGroup,
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ui(520), ui(378), ui(150), ui(30), hwnd,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingJoinQqGroup)),
                  GetModuleHandleW(nullptr), nullptr);
}

void refreshSettingsLocalizedControls(HWND hwnd) {
    if (!hwnd) return;
    SetWindowTextW(hwnd, settingsLocale().windowTitle);
    if (HWND browse = GetDlgItem(hwnd, kSettingBrowseDataDirectory)) {
        SetWindowTextW(browse, settingsLocale().browse);
    }
    if (HWND clear = GetDlgItem(hwnd, kSettingClear)) {
        SetWindowTextW(clear, settingsLocale().clearHistory);
    }
    if (HWND openLog = GetDlgItem(hwnd, kSettingOpenLog)) {
        SetWindowTextW(openLog, settingsLocale().openLog);
    }
    if (HWND support = GetDlgItem(hwnd, kSettingSupportAuthor)) {
        SetWindowTextW(support, settingsLocale().supportAuthor);
    }
    if (HWND qq = GetDlgItem(hwnd, kSettingJoinQqGroup)) {
        SetWindowTextW(qq, settingsLocale().joinQqGroup);
    }
    const int clearIds[] = {kSettingClearText, kSettingClearImage, kSettingClearFiles};
    const wchar_t* clearLabels[] = {
        settingsLocale().clearText, settingsLocale().clearImages, settingsLocale().clearFiles
    };
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        if (HWND clear = GetDlgItem(hwnd, clearIds[i])) {
            SetWindowTextW(clear, clearLabels[i]);
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void updateSettingsTabControls(HWND hwnd) {
    g_app->hoveredSettingsControl = 0;
    if (g_app->shortcutCaptureControl) cancelSettingsShortcutCapture(hwnd);
    g_app->settingsScrollOffset = std::clamp(g_app->settingsScrollOffset, 0, settingsScrollMax(hwnd));
    if (g_app->languageDropdown) {
        DestroyWindow(g_app->languageDropdown);
        KillTimer(hwnd, kSettingsDropdownTimer);
    }
    const int ids[] = {kSettingDark, kSettingWinV, kSettingLanguage, kSettingPause,
                         kSettingStartup, kSettingStartupSettings, kSettingStartupNotification,
                         kSettingRunAsAdministrator, kSettingSearchImeCompatibility,
                         kSettingPromotePastedItem, kSettingEncrypt, kSettingMaxItems,
                        kSettingRetentionDays, kSettingMaxDiskMb, kSettingMaxContentMb,
                        kSettingDataDirectory, kSettingBrowseDataDirectory,
                          kSettingIgnoredApps, kSettingSensitiveExpiry, kSettingClear,
                             kSettingClearText, kSettingClearImage, kSettingClearFiles,
                             kSettingOpenLog, kSettingSupportAuthor, kSettingJoinQqGroup,
                            kSettingShortcutHistory,
                         kSettingShortcutSettings, kSettingShortcutPause,
                         kSettingShortcutPaste, kSettingShortcutPastePlain,
                         kSettingShortcutPasteRich, kSettingShortcutClosePopup,
                         kSettingShortcutPopupSettings, kSettingShortcutClearFilter,
                         kSettingShortcutDelete,
                        kSettingCategoryMaxBase, kSettingCategoryMaxBase + 1,
                        kSettingCategoryMaxBase + 2,
                        kSettingCategoryDiskBase, kSettingCategoryDiskBase + 1,
                        kSettingCategoryDiskBase + 2};
    const bool tabChanged = g_app->settingsControlsTab != g_app->settingsTab;
    if (tabChanged) {
        for (const int id : ids) {
            if (HWND control = GetDlgItem(hwnd, id)) ShowWindow(control, SW_HIDE);
        }
        g_app->settingsControlsTab = g_app->settingsTab;
    }
    RECT client{};
    GetClientRect(hwnd, &client);
    const int bodyTop = ui(kSettingsHeaderHeight);
    const int scrollOffset = (g_app->settingsTab == 0 ||
                              g_app->settingsTab == kSettingsShortcutPage ||
                              g_app->settingsTab == 2)
        ? ui(g_app->settingsScrollOffset) : 0;
    HDWP defer = BeginDeferWindowPos(static_cast<int>(sizeof(ids) / sizeof(ids[0])));
    auto show = [&, hwnd, client, bodyTop, scrollOffset, tabChanged](int id, int x, int y,
                                                                   int width, int height,
                                                                   bool fixed = false) {
        HWND control = GetDlgItem(hwnd, id);
        if (!control) return;
        wchar_t className[32]{};
        GetClassNameW(control, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
        const bool edit = std::wcscmp(className, L"Edit") == 0;
        const int top = ui(y) - (fixed ? 0 : scrollOffset);
        const bool visible = fixed || (top >= bodyTop && top + ui(height) <= client.bottom);
        RECT current{};
        GetWindowRect(control, &current);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&current), 2);
        const bool resized = current.right - current.left != ui(width) ||
            current.bottom - current.top != ui(height);
        const bool moved = current.left != ui(x) || current.top != top;
        if (moved || resized || static_cast<bool>(IsWindowVisible(control)) != visible) {
            UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING | SWP_NOREDRAW;
            if (!moved) flags |= SWP_NOMOVE;
            if (!resized) flags |= SWP_NOSIZE;
            if (visible) flags |= SWP_SHOWWINDOW;
            else flags |= SWP_HIDEWINDOW;
            if (defer) {
                HDWP next = DeferWindowPos(defer, control, nullptr, ui(x), top,
                                           ui(width), ui(height), flags);
                if (next) {
                    defer = next;
                } else {
                    EndDeferWindowPos(defer);
                    defer = nullptr;
                }
            }
            if (!defer) {
                SetWindowPos(control, nullptr, ui(x), top, ui(width), ui(height), flags);
            }
        }
        if (edit && (resized || tabChanged)) configureSettingsEdit(control);
    };
    const SettingsLayout layout = buildSettingsLayout(hwnd);
    for (const SettingsCardLayout& card : layout.cards) {
        for (const SettingsRowLayout& row : card.rows) {
            for (std::size_t i = 0; i < row.controlIds.size(); ++i) {
                const int controlHeight = row.controlHeights[i];
                const int top = row.top + (row.height - controlHeight) / 2;
                show(row.controlIds[i], row.controlX[i], top,
                     row.controlWidths[i], controlHeight);
            }
        }
    }
    if (g_app->settingsTab == 4) {
        show(kSettingOpenLog, 520, 298, 150, 30);
        show(kSettingSupportAuthor, 520, 338, 150, 30);
        show(kSettingJoinQqGroup, 520, 378, 150, 30);
    }
    if (defer) EndDeferWindowPos(defer);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
}

void appendPasteMenu(HMENU menu, const ClipItem& item) {
    AppendMenuW(menu, MF_STRING, kMenuPaste, settingsLocale().popupPaste);
    AppendMenuW(menu, MF_STRING, kMenuPastePlain, settingsLocale().popupPastePlain);
    AppendMenuW(menu, MF_STRING | (item.type == ClipType::Html ? 0 : MF_GRAYED),
                kMenuPasteRich, settingsLocale().popupPasteRich);
}

void appendFilterMenu(HMENU menu) {
    HMENU filters = CreatePopupMenu();
    AppendMenuW(filters, MF_STRING, kFilterAll, settingsLocale().popupFilterAll);
    AppendMenuW(filters, MF_STRING, kFilterPinned, settingsLocale().popupFilterPinned);
    AppendMenuW(filters, MF_STRING, kFilterText, settingsLocale().popupFilterText);
    AppendMenuW(filters, MF_STRING, kFilterImage, settingsLocale().popupFilterImages);
    AppendMenuW(filters, MF_STRING, kFilterFiles, settingsLocale().popupFilterFiles);
    AppendMenuW(filters, MF_STRING, kFilterOther, settingsLocale().popupFilterOther);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(filters),
                settingsLocale().popupFilter);
}

void applyFilterCommand(int command) {
    if (command == kFilterAll) {
        g_app->filterType = 0;
        g_app->pinnedOnly = false;
    } else if (command == kFilterText) {
        g_app->filterType = 1;
        g_app->pinnedOnly = false;
    } else if (command == kFilterImage) {
        g_app->filterType = 2;
        g_app->pinnedOnly = false;
    } else if (command == kFilterFiles) {
        g_app->filterType = 3;
        g_app->pinnedOnly = false;
    } else if (command == kFilterPinned) {
        g_app->filterType = 0;
        g_app->pinnedOnly = true;
    } else if (command == kFilterOther) {
        g_app->filterType = 4;
        g_app->pinnedOnly = false;
    }
}

void applyFontToChildren(HWND parent, HFONT font) {
    for (HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void invalidateThemeWindow(HWND hwnd) {
    if (!hwnd) return;
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
}

void refreshSettingsBrushes() {
    if (!g_app->settings) return;
    const bool highContrast = highContrastEnabled();
    const COLORREF background = highContrast ? GetSysColor(COLOR_WINDOW) :
        settingsThemeColor(RGB(240, 244, 248), RGB(21, 26, 34));
    const COLORREF card = highContrast ? GetSysColor(COLOR_WINDOW) :
        settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
    const COLORREF input = highContrast ? GetSysColor(COLOR_WINDOW) :
        settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
    if (g_app->settingsBackgroundBrush) DeleteObject(g_app->settingsBackgroundBrush);
    if (g_app->settingsCardBrush) DeleteObject(g_app->settingsCardBrush);
    if (g_app->settingsInputBrush) DeleteObject(g_app->settingsInputBrush);
    g_app->settingsBackgroundBrush = CreateSolidBrush(background);
    g_app->settingsCardBrush = CreateSolidBrush(card);
    g_app->settingsInputBrush = CreateSolidBrush(input);
}

void appendPopupPinMenu(HMENU menu) {
    AppendMenuW(menu, MF_STRING | (g_app->popupPinned ? MF_CHECKED : 0),
                kMenuPopupPinned,
                g_app->popupPinned
                    ? settingsLocale().popupUnpin : settingsLocale().popupPin);
}

void refreshPopupBrush() {
    if (!g_app->popup) return;
    if (g_app->popupInputBrush) DeleteObject(g_app->popupInputBrush);
    const COLORREF input = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOW)
        : settingsThemeColor(RGB(255, 255, 255), RGB(43, 47, 54));
    g_app->popupInputBrush = CreateSolidBrush(input);
}

void refreshSettingsFrame(HWND hwnd) {
    if (!hwnd) return;
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_USEWINDOWSTYLE;
    DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    const COLORREF border = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOW)
        : settingsThemeColor(RGB(240, 244, 248), RGB(21, 26, 34));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
    const COLORREF caption = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOW)
        : settingsThemeColor(RGB(240, 244, 248), RGB(21, 26, 34));
    const COLORREF captionText = highContrastEnabled()
        ? GetSysColor(COLOR_WINDOWTEXT)
        : settingsThemeColor(RGB(30, 41, 59), RGB(226, 232, 240));
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &captionText, sizeof(captionText));
}

bool isSettingsToggle(int id) {
    return id == kSettingWinV || id == kSettingDark || id == kSettingPause ||
           id == kSettingStartup || id == kSettingStartupSettings ||
           id == kSettingStartupNotification || id == kSettingRunAsAdministrator ||
           id == kSettingSearchImeCompatibility || id == kSettingPromotePastedItem ||
           id == kSettingEncrypt;
}

int settingsToggleAtPoint(int tab, int x, int y) {
    if (!g_app || !g_app->settings || g_app->settingsTab != tab) return 0;
    const SettingsLayout layout = buildSettingsLayout(g_app->settings);
    const int scroll = (tab == 0 || tab == kSettingsShortcutPage || tab == 2)
        ? ui(g_app->settingsScrollOffset) : 0;
    for (const SettingsCardLayout& card : layout.cards) {
        for (const SettingsRowLayout& row : card.rows) {
            for (std::size_t i = 0; i < row.controlIds.size(); ++i) {
                if (!isSettingsToggle(row.controlIds[i])) continue;
                const int left = ui(row.controlX[i]);
                const int top = ui(row.top) + (ui(row.height) - ui(row.controlHeights[i])) / 2 - scroll;
                const int right = left + ui(row.controlWidths[i]);
                const int bottom = top + ui(row.controlHeights[i]);
                if (x >= left && x < right && y >= top && y < bottom) return row.controlIds[i];
            }
        }
    }
    return 0;
}

int settingsThemeModeAtPoint(HWND hwnd, int x, int y) {
    if (!hwnd || y < ui(14) || y >= ui(44)) return -1;
    RECT client{};
    GetClientRect(hwnd, &client);
    const int left = client.right - ui(20) - ui(kSettingsThemeWidth);
    const int segmentLeft = left + ui(3);
    const int segmentRight = segmentLeft + ui(kSettingsThemeSegmentWidth) * 3;
    if (x < segmentLeft || x >= segmentRight) return -1;
    return (x - segmentLeft) / ui(kSettingsThemeSegmentWidth);
}

int settingsAccentAtPoint(HWND hwnd, int x, int y) {
    if (!hwnd || y < ui(20) || y >= ui(38)) return -1;
    RECT client{};
    GetClientRect(hwnd, &client);
    const int themeLeft = client.right - ui(20) - ui(kSettingsThemeWidth);
    const int right = themeLeft - ui(14);
    const int left = right - ui(14 * 4 + 6 * 3);
    if (x < left - ui(2) || x >= right + ui(2)) return -1;
    return std::clamp((x - left) / ui(20), 0, kAccentCount - 1);
}

void invalidateSettingsTheme(HWND hwnd) {
    if (!hwnd) return;
    refreshSettingsBrushes();
    refreshPopupBrush();
    invalidateThemeWindow(hwnd);
    invalidateThemeWindow(g_app->popup);
    invalidateThemeWindow(g_app->languageDropdown);
}

void setSettingsThemeMode(HWND hwnd, int mode) {
    if (!hwnd || !g_app) return;
    mode = std::clamp(mode, 0, 2);
    const bool fromDark = g_app->settingsData.dark;
    const bool toDark = mode == 2 || (mode == 0 && systemThemeIsDark());
    g_app->settingsData.themeMode = mode;
    g_app->settingsData.dark = toDark;
    setSettingsToggleValue(GetDlgItem(hwnd, kSettingDark), toDark);
    if (fromDark != toDark) animateSettingsTheme(hwnd, fromDark, toDark);
    invalidateSettingsTheme(hwnd);
    refreshSettingsFrame(hwnd);
    saveSettings(g_app->settingsData);
}

void setSettingsAccent(HWND hwnd, int accent) {
    if (!hwnd || !g_app) return;
    g_app->settingsData.accent = std::clamp(accent, 0, 3);
    invalidateSettingsTheme(hwnd);
    saveSettings(g_app->settingsData);
}

bool settingsToggleValue(HWND hwnd) {
    return GetWindowLongPtrW(hwnd, GWLP_USERDATA) != 0;
}

void setSettingsToggleValue(HWND hwnd, bool enabled) {
    if (!hwnd) return;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, enabled ? 1 : 0);
    SendMessageW(hwnd, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
}

int settingsLanguageSelection(HWND hwnd) {
    if (!hwnd) return 0;
    return std::clamp(static_cast<int>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)), 0, 2);
}

void setSettingsLanguageSelection(HWND hwnd, int selection) {
    if (!hwnd) return;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, std::clamp(selection, 0, 2));
}

std::wstring normalizeDataDirectory(const std::wstring& value) {
    if (value.empty()) return {};
    std::wstring input = value;
    const std::size_t first = input.find_first_not_of(L" \t\r\n");
    const std::size_t last = input.find_last_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    input = input.substr(first, last - first + 1);
    wchar_t fullPath[MAX_PATH]{};
    const DWORD length = GetFullPathNameW(input.c_str(), MAX_PATH, fullPath, nullptr);
    if (length == 0 || length >= MAX_PATH) return {};
    std::wstring normalized(fullPath, length);
    while (normalized.size() > 3 &&
           (normalized.back() == L'\\' || normalized.back() == L'/')) {
        normalized.pop_back();
    }
    return normalized;
}

std::wstring dataDirectoryFromStorePath(const std::wstring& filePath) {
    const std::size_t slash = filePath.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : filePath.substr(0, slash);
}

bool migrateDataDirectory(HWND hwnd, const std::wstring& requested, std::string& persisted) {
    const std::wstring defaultDirectory = normalizeDataDirectory(clipLiteDataDirectory());
    const std::wstring targetDirectory = normalizeDataDirectory(requested.empty()
        ? defaultDirectory : requested);
    if (targetDirectory.empty()) {
        appendDiagnosticLog("ERROR", "settings: invalid cache directory");
        MessageBoxW(hwnd, settingsLocale().chooseValidCache,
                    L"ClipLite", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (SHCreateDirectoryExW(nullptr, targetDirectory.c_str(), nullptr) != ERROR_SUCCESS &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        appendDiagnosticLog("ERROR", "settings: unable to create cache directory", GetLastError());
        MessageBoxW(hwnd, settingsLocale().unableCreateCache,
                    L"ClipLite", MB_OK | MB_ICONWARNING);
        return false;
    }

    const std::wstring currentFile = g_app->store.path();
    const std::wstring currentDirectory = dataDirectoryFromStorePath(currentFile);
    if (_wcsicmp(currentDirectory.c_str(), targetDirectory.c_str()) == 0) {
        persisted = _wcsicmp(defaultDirectory.c_str(), targetDirectory.c_str()) == 0
            ? std::string{} : wideToUtf8(targetDirectory.c_str(), targetDirectory.size());
        return true;
    }

    const std::wstring targetFile = targetDirectory + L"\\history.bin";
    const bool currentExists = GetFileAttributesW(currentFile.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool targetExists = GetFileAttributesW(targetFile.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (currentExists && targetExists) {
        appendDiagnosticLog("WARN", "settings: cache migration target already contains history");
        MessageBoxW(hwnd,
                    settingsLocale().targetContainsHistory,
                    L"ClipLite", MB_OK | MB_ICONWARNING);
        return false;
    }

    bool copied = false;
    if (currentExists) {
        if (!CopyFileW(currentFile.c_str(), targetFile.c_str(), TRUE)) {
            appendDiagnosticLog("ERROR", "settings: unable to copy history during cache migration",
                                GetLastError());
            MessageBoxW(hwnd, settingsLocale().unableMigrateHistory,
                        L"ClipLite", MB_OK | MB_ICONWARNING);
            return false;
        }
        copied = true;
    }
    if (!g_app->store.setDataDirectory(targetDirectory) || !g_app->store.open()) {
        appendDiagnosticLog("ERROR", "settings: unable to open migrated cache directory");
        DeleteFileW(targetFile.c_str());
        g_app->store.setDataDirectory(currentDirectory);
        g_app->store.open();
        MessageBoxW(hwnd, settingsLocale().unableOpenCache,
                    L"ClipLite", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (copied) DeleteFileW(currentFile.c_str());
    persisted = _wcsicmp(defaultDirectory.c_str(), targetDirectory.c_str()) == 0
        ? std::string{} : wideToUtf8(targetDirectory.c_str(), targetDirectory.size());
    return true;
}

void browseDataDirectory(HWND hwnd) {
    wchar_t displayName[MAX_PATH]{};
    BROWSEINFOW browse{ };
    browse.hwndOwner = hwnd;
    browse.pszDisplayName = displayName;
    browse.lpszTitle = settingsLocale().chooseCacheDirectory;
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
    if (!item) return;
    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(item, path)) {
        SetWindowTextW(GetDlgItem(hwnd, kSettingDataDirectory), path);
        scheduleSettingsSync(hwnd);
    }
    CoTaskMemFree(item);
}

void restoreSettingsRetentionControls(HWND hwnd, const Settings& settings) {
    if (!hwnd || !g_app) return;
    g_app->restoringSettingsControls = true;
    auto setValue = [](HWND control, int value) {
        if (!control) return;
        wchar_t text[32]{};
        swprintf_s(text, L"%d", value);
        SetWindowTextW(control, text);
    };
    setValue(GetDlgItem(hwnd, kSettingMaxItems), settings.maxItems);
    setValue(GetDlgItem(hwnd, kSettingRetentionDays), settings.retentionDays);
    setValue(GetDlgItem(hwnd, kSettingMaxDiskMb), settings.maxDiskMb);
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        const CategoryLimit& limit = settings.categoryLimits[static_cast<std::size_t>(i)];
        setValue(GetDlgItem(hwnd, kSettingCategoryMaxBase + i), limit.maxItems);
        setValue(GetDlgItem(hwnd, kSettingCategoryDiskBase + i), limit.maxDiskMb);
    }
    g_app->restoringSettingsControls = false;
    KillTimer(hwnd, kSettingsSyncTimer);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool settingsWouldPruneExisting(const Settings& previous, const Settings& next) {
    if (!g_app) return false;
    if (next.maxItems != previous.maxItems && next.maxItems > 0 &&
        g_app->store.activeCount() > static_cast<std::size_t>(next.maxItems)) {
        return true;
    }
    if (next.maxDiskMb != previous.maxDiskMb && next.maxDiskMb > 0 &&
        g_app->store.diskBytes() > static_cast<std::uint64_t>(next.maxDiskMb) * 1024ULL * 1024ULL) {
        return true;
    }
    if (next.retentionDays != previous.retentionDays && next.retentionDays > 0) {
        const std::uint64_t cutoff = nowUnix() -
            static_cast<std::uint64_t>(next.retentionDays) * 86400ULL;
        for (const ClipItem& item : g_app->store.items()) {
            if (!item.pinned && item.timestamp < cutoff) return true;
        }
    }
    const ClipType types[] = {ClipType::Text, ClipType::Image, ClipType::Files};
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        const CategoryLimit& before = previous.categoryLimits[static_cast<std::size_t>(i)];
        const CategoryLimit& after = next.categoryLimits[static_cast<std::size_t>(i)];
        if (after == before) continue;
        if (after.maxItems > 0 &&
            g_app->store.countType(types[i]) > static_cast<std::size_t>(after.maxItems)) {
            return true;
        }
        if (after.maxDiskMb > 0 &&
            g_app->store.bytesType(types[i]) > static_cast<std::uint64_t>(after.maxDiskMb) * 1024ULL * 1024ULL) {
            return true;
        }
    }
    return false;
}

bool syncSettingsFromControls(HWND hwnd, bool applyEncryption) {
    if (!hwnd) return false;
    HWND win = GetDlgItem(hwnd, kSettingWinV);
    HWND dark = GetDlgItem(hwnd, kSettingDark);
    HWND language = GetDlgItem(hwnd, kSettingLanguage);
    HWND pause = GetDlgItem(hwnd, kSettingPause);
    HWND maxItems = GetDlgItem(hwnd, kSettingMaxItems);
    HWND retentionDays = GetDlgItem(hwnd, kSettingRetentionDays);
    HWND maxDiskMb = GetDlgItem(hwnd, kSettingMaxDiskMb);
    HWND maxContentMb = GetDlgItem(hwnd, kSettingMaxContentMb);
    HWND dataDirectory = GetDlgItem(hwnd, kSettingDataDirectory);
    HWND ignoredApps = GetDlgItem(hwnd, kSettingIgnoredApps);
    HWND sensitiveExpiry = GetDlgItem(hwnd, kSettingSensitiveExpiry);
    HWND startup = GetDlgItem(hwnd, kSettingStartup);
    HWND startupSettings = GetDlgItem(hwnd, kSettingStartupSettings);
    HWND startupNotification = GetDlgItem(hwnd, kSettingStartupNotification);
    HWND runAsAdministrator = GetDlgItem(hwnd, kSettingRunAsAdministrator);
    HWND searchImeCompatibility = GetDlgItem(hwnd, kSettingSearchImeCompatibility);
    HWND promotePastedItem = GetDlgItem(hwnd, kSettingPromotePastedItem);
    HWND encrypt = GetDlgItem(hwnd, kSettingEncrypt);
    HWND categoryMax[kStorageCategoryCount]{};
    HWND categoryDisk[kStorageCategoryCount]{};
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        categoryMax[i] = GetDlgItem(hwnd, kSettingCategoryMaxBase + i);
        categoryDisk[i] = GetDlgItem(hwnd, kSettingCategoryDiskBase + i);
    }
    if (!win || !dark || !language || !pause || !maxItems || !retentionDays || !maxDiskMb ||
        !maxContentMb || !dataDirectory || !ignoredApps || !sensitiveExpiry || !startup ||
        !startupSettings || !startupNotification || !runAsAdministrator ||
        !searchImeCompatibility || !promotePastedItem || !encrypt) {
        return false;
    }
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        if (!categoryMax[i] || !categoryDisk[i]) return false;
    }

    Settings next = g_app->settingsData;
    next.winV = settingsToggleValue(win);
    next.dark = settingsToggleValue(dark);
    next.pauseMonitoring = settingsToggleValue(pause);
    next.startWithWindows = settingsToggleValue(startup);
    next.showSettingsOnStartup = settingsToggleValue(startupSettings);
    next.showStartupNotification = settingsToggleValue(startupNotification);
    next.runAsAdministrator = settingsToggleValue(runAsAdministrator);
    next.searchImeCompatibility = settingsToggleValue(searchImeCompatibility);
    next.promotePastedItem = settingsToggleValue(promotePastedItem);
    next.language = settingsLanguageSelection(language);
    next.language = next.language <= 0 ? -1 : next.language - 1;

    wchar_t value[32]{};
    GetWindowTextW(maxItems, value, 32);
    next.maxItems = static_cast<int>(std::clamp(std::wcstol(value, nullptr, 10), 0L, 100000L));
    GetWindowTextW(retentionDays, value, 32);
    next.retentionDays = static_cast<int>(std::clamp(std::wcstol(value, nullptr, 10), 0L, 36500L));
    GetWindowTextW(maxDiskMb, value, 32);
    next.maxDiskMb = static_cast<int>(std::clamp(std::wcstol(value, nullptr, 10), 0L, 102400L));
    GetWindowTextW(maxContentMb, value, 32);
    next.maxContentMb = static_cast<int>(std::clamp(std::wcstol(value, nullptr, 10), 1L, 32L));
    const int dataDirectoryLength = GetWindowTextLengthW(dataDirectory);
    std::wstring dataDirectoryText(static_cast<std::size_t>(dataDirectoryLength) + 1, L'\0');
    GetWindowTextW(dataDirectory, dataDirectoryText.data(), dataDirectoryLength + 1);
    dataDirectoryText.resize(static_cast<std::size_t>(dataDirectoryLength));
    const std::wstring normalizedDataDirectory = normalizeDataDirectory(dataDirectoryText);
    if (!dataDirectoryText.empty() && normalizedDataDirectory.empty()) {
        MessageBoxW(hwnd, settingsLocale().chooseValidCache,
                    L"ClipLite", MB_OK | MB_ICONWARNING);
        return false;
    }
    const std::wstring defaultDataDirectory = normalizeDataDirectory(clipLiteDataDirectory());
    next.dataDirectory = normalizedDataDirectory.empty() ||
        _wcsicmp(normalizedDataDirectory.c_str(), defaultDataDirectory.c_str()) == 0
        ? std::string{} : wideToUtf8(normalizedDataDirectory.c_str(), normalizedDataDirectory.size());
    GetWindowTextW(sensitiveExpiry, value, 32);
    next.sensitiveExpiryHours = static_cast<int>(std::clamp(std::wcstol(value, nullptr, 10), 0L, 720L));
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        GetWindowTextW(categoryMax[i], value, 32);
        next.categoryLimits[static_cast<std::size_t>(i)].maxItems =
            static_cast<int>(std::clamp(std::wcstol(value, nullptr, 10), 0L, 100000L));
        GetWindowTextW(categoryDisk[i], value, 32);
        next.categoryLimits[static_cast<std::size_t>(i)].maxDiskMb =
            static_cast<int>(std::clamp(std::wcstol(value, nullptr, 10), 0L, 102400L));
    }

    const int ignoredLength = GetWindowTextLengthW(ignoredApps);
    std::wstring ignoredText(static_cast<std::size_t>(ignoredLength) + 1, L'\0');
    GetWindowTextW(ignoredApps, ignoredText.data(), ignoredLength + 1);
    ignoredText.resize(static_cast<std::size_t>(ignoredLength));
    next.ignoredApps.clear();
    std::size_t lineStart = 0;
    while (lineStart <= ignoredText.size()) {
        const std::size_t lineEnd = ignoredText.find_first_of(L"\r\n", lineStart);
        std::wstring app = ignoredText.substr(lineStart, lineEnd == std::wstring::npos
            ? std::wstring::npos : lineEnd - lineStart);
        const std::size_t first = app.find_first_not_of(L" \t");
        const std::size_t last = app.find_last_not_of(L" \t");
        if (first != std::wstring::npos) {
            app = app.substr(first, last - first + 1);
            const std::string appUtf8 = wideToUtf8(app.c_str(), app.size());
            bool duplicate = false;
            for (const std::string& existing : next.ignoredApps) {
                if (_stricmp(existing.c_str(), appUtf8.c_str()) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (!appUtf8.empty() && !duplicate) next.ignoredApps.push_back(appUtf8);
        }
        if (lineEnd == std::wstring::npos) break;
        lineStart = ignoredText.find_first_not_of(L"\r\n", lineEnd);
        if (lineStart == std::wstring::npos) break;
    }

    const Settings& previous = g_app->settingsData;
    if (settingsWouldPruneExisting(previous, next) &&
        MessageBoxW(hwnd, settingsLocale().pruneConfirmationMessage,
                    settingsLocale().pruneConfirmationTitle,
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        restoreSettingsRetentionControls(hwnd, previous);
        return false;
    }

    const bool previousEncryption = g_app->store.encryptionEnabled();
    const bool desiredEncryption = applyEncryption
        ? settingsToggleValue(encrypt) : previousEncryption;
    if (desiredEncryption != previousEncryption && !g_app->store.rekey(desiredEncryption)) {
        appendDiagnosticLog("ERROR", "settings: history encryption migration failed");
        setSettingsToggleValue(encrypt, previousEncryption);
        MessageBoxW(hwnd, settingsLocale().unableChangeEncryption,
                    L"ClipLite", MB_OK | MB_ICONWARNING);
        return false;
    }
    next.encryptData = desiredEncryption;

    const bool dataDirectoryChanged = next.dataDirectory != previous.dataDirectory;
    if (dataDirectoryChanged) {
        if (!migrateDataDirectory(hwnd, normalizedDataDirectory, next.dataDirectory)) {
            if (desiredEncryption != previousEncryption) {
                g_app->store.rekey(previousEncryption);
                setSettingsToggleValue(encrypt, previousEncryption);
            }
            return false;
        }
    }
    if (next.dark != previous.dark) next.themeMode = next.dark ? 2 : 1;
    const bool previousDark = previous.dark;
    const bool darkChanged = next.dark != previous.dark;
    const bool themeModeChanged = next.themeMode != previous.themeMode;
    const bool winVChanged = next.winV != previous.winV;
    const bool startupChanged = next.startWithWindows != previous.startWithWindows;
    const bool startupSettingsChanged = next.showSettingsOnStartup != previous.showSettingsOnStartup;
    const bool startupNotificationChanged =
        next.showStartupNotification != previous.showStartupNotification;
    const bool adminModeChanged = next.runAsAdministrator != previous.runAsAdministrator;
    const bool searchImeCompatibilityChanged =
        next.searchImeCompatibility != previous.searchImeCompatibility;
    const bool promotePastedItemChanged = next.promotePastedItem != previous.promotePastedItem;
    const bool maxItemsChanged = next.maxItems != previous.maxItems;
    const bool retentionChanged = next.retentionDays != previous.retentionDays;
    const bool maxDiskChanged = next.maxDiskMb != previous.maxDiskMb;
    const bool maxContentChanged = next.maxContentMb != previous.maxContentMb;
    const bool categoryLimitsChanged = next.categoryLimits != previous.categoryLimits;
    const bool languageChanged = next.language != previous.language;
    const bool changed = darkChanged || themeModeChanged || winVChanged || startupChanged ||
        startupSettingsChanged || startupNotificationChanged || adminModeChanged || maxItemsChanged ||
        retentionChanged || maxDiskChanged || maxContentChanged || categoryLimitsChanged ||
        next.pauseMonitoring != previous.pauseMonitoring ||
        next.encryptData != previous.encryptData || next.language != previous.language ||
        dataDirectoryChanged ||
        next.sensitiveExpiryHours != previous.sensitiveExpiryHours || searchImeCompatibilityChanged ||
        promotePastedItemChanged ||
        next.ignoredApps != previous.ignoredApps;
    if (!changed) return true;

    g_app->settingsData = std::move(next);
    if (promotePastedItemChanged) {
        g_app->store.setSortByLastUsed(g_app->settingsData.promotePastedItem);
    }
    if (darkChanged) {
        animateSettingsTheme(hwnd, previousDark, g_app->settingsData.dark);
        invalidateSettingsTheme(hwnd);
    }
    if (maxContentChanged) {
        g_app->store.setMaxPayloadBytes(static_cast<std::uint32_t>(g_app->settingsData.maxContentMb) *
                                        1024u * 1024u);
    }
    if (maxItemsChanged) {
        g_app->store.setMaxItems(static_cast<std::size_t>(g_app->settingsData.maxItems));
    }
    if (startupChanged) updateStartupRegistration(g_app->settingsData.startWithWindows);
    if (winVChanged) registerHotkeys();
    if (languageChanged) {
        g_app->settingsActionFeedback.clear();
        refreshSettingsLocalizedControls(hwnd);
        updateSettingsTabControls(hwnd);
    }
    if (maxItemsChanged || retentionChanged || maxDiskChanged) {
        const std::uint64_t cutoff = g_app->settingsData.retentionDays > 0
            ? nowUnix() - static_cast<std::uint64_t>(g_app->settingsData.retentionDays) * 86400ULL
            : 0;
        g_app->store.prune(g_app->settingsData.maxItems,
                           static_cast<std::uint64_t>(g_app->settingsData.maxDiskMb) * 1024ULL * 1024ULL,
                           cutoff);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (categoryLimitsChanged) {
        const ClipType types[] = {ClipType::Text, ClipType::Image, ClipType::Files};
        for (int i = 0; i < kStorageCategoryCount; ++i) {
            const CategoryLimit& limit = g_app->settingsData.categoryLimits[static_cast<std::size_t>(i)];
            g_app->store.pruneCategory(types[i], static_cast<std::size_t>(limit.maxItems),
                                       static_cast<std::uint64_t>(limit.maxDiskMb) * 1024ULL * 1024ULL);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    saveSettings(g_app->settingsData);
    if (adminModeChanged && g_app->settingsData.runAsAdministrator && !processIsElevated()) {
        if (!launchElevatedRestart(L"--elevated-restart --settings")) {
            g_app->settingsData.runAsAdministrator = false;
            setSettingsToggleValue(runAsAdministrator, false);
            saveSettings(g_app->settingsData);
            MessageBoxW(hwnd, tr(L"Unable to restart ClipLite with administrator rights.",
                                  L"无法以管理员权限重新启动 ClipLite。"),
                        L"ClipLite", MB_OK | MB_ICONWARNING);
            return false;
        }
        PostQuitMessage(0);
    }
    return true;
}

void scheduleSettingsSync(HWND hwnd) {
    if (hwnd && g_app && !g_app->settingsClosing) {
        SetTimer(hwnd, kSettingsSyncTimer, kSettingsSyncDelayMs, nullptr);
    }
}

void scheduleSettingsEncryptionSync(HWND hwnd) {
    if (hwnd && g_app && !g_app->settingsClosing) {
        SetTimer(hwnd, kSettingsEncryptionTimer, kSettingsEncryptionDebounceMs, nullptr);
    }
}

LONGLONG settingsToggleClock() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

LONGLONG settingsToggleClockFrequency() {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart > 0 ? value.QuadPart : 1LL;
    }();
    return frequency;
}

float settingsThemeProgress() {
    if (!g_app || !g_app->settingsThemeAnimating) return 1.0f;
    return g_app->settingsThemeProgressValue;
}

bool advanceSettingsThemeAnimation() {
    if (!g_app || !g_app->settingsThemeAnimating) return true;
    const LONGLONG elapsed = settingsToggleClock() - g_app->settingsThemeStartTicks;
    const float linear = std::min(1.0f, static_cast<float>(elapsed) * 1000.0f /
        static_cast<float>(settingsToggleClockFrequency() * kSettingsThemeAnimationMs));
    g_app->settingsThemeProgressValue = linear * linear * (3.0f - 2.0f * linear);
    return linear >= 1.0f;
}

COLORREF settingsThemeColor(COLORREF light, COLORREF dark) {
    if (!g_app || !g_app->settingsThemeAnimating) {
        return g_app && g_app->settingsData.dark ? dark : light;
    }
    const float progress = settingsThemeProgress();
    const COLORREF from = g_app->settingsThemeFromDark ? dark : light;
    const COLORREF to = g_app->settingsThemeToDark ? dark : light;
    const auto blend = [progress](BYTE first, BYTE second) {
        return static_cast<BYTE>(first + (second - first) * progress + 0.5f);
    };
    return RGB(blend(GetRValue(from), GetRValue(to)),
               blend(GetGValue(from), GetGValue(to)),
               blend(GetBValue(from), GetBValue(to)));
}

COLORREF settingsAccentColor() {
    const bool dark = g_app && g_app->settingsData.dark;
    const int accent = g_app ? std::clamp(g_app->settingsData.accent, 0, 3) : 0;
    const COLORREF light[] = {
        RGB(37, 99, 235), RGB(124, 58, 237), RGB(39, 124, 97), RGB(217, 119, 6)
    };
    const COLORREF darkColors[] = {
        RGB(59, 130, 246), RGB(168, 85, 247), RGB(64, 157, 156), RGB(245, 158, 11)
    };
    return dark ? darkColors[accent] : light[accent];
}

COLORREF settingsAccentSoftColor() {
    const bool dark = g_app && g_app->settingsData.dark;
    const int accent = g_app ? std::clamp(g_app->settingsData.accent, 0, 3) : 0;
    const COLORREF light[] = {
        RGB(232, 240, 254), RGB(243, 237, 254), RGB(224, 242, 239), RGB(254, 243, 224)
    };
    const COLORREF darkColors[] = {
        RGB(39, 55, 82), RGB(61, 48, 82), RGB(57, 69, 67), RGB(79, 63, 38)
    };
    return dark ? darkColors[accent] : light[accent];
}

void animateSettingsTheme(HWND hwnd, bool fromDark, bool toDark) {
    if (!hwnd || fromDark == toDark) {
        if (g_app) g_app->settingsThemeAnimating = false;
        return;
    }
    g_app->settingsThemeAnimating = true;
    g_app->settingsThemeFromDark = fromDark;
    g_app->settingsThemeToDark = toDark;
    g_app->settingsThemeProgressValue = 0.0f;
    g_app->settingsThemeStartTicks = settingsToggleClock();
    SetTimer(hwnd, kSettingsThemeTimer, 16, nullptr);
}

bool settingsToggleAnimationFinished() {
    if (!g_app->toggleAnimationControl) return true;
    const LONGLONG elapsed = settingsToggleClock() - g_app->toggleAnimationStartTicks;
    return elapsed * 1000LL >= settingsToggleClockFrequency() * kSettingsToggleAnimationMs;
}

float settingsTogglePosition(HWND hwnd) {
    if (!hwnd || g_app->toggleAnimationControl != hwnd) {
        return settingsToggleValue(hwnd) ? 1.0f : 0.0f;
    }
    const LONGLONG elapsed = settingsToggleClock() - g_app->toggleAnimationStartTicks;
    const float progress = std::min(1.0f, static_cast<float>(elapsed) * 1000.0f /
        static_cast<float>(settingsToggleClockFrequency() * kSettingsToggleAnimationMs));
    if (progress >= 1.0f) return g_app->toggleAnimationTo;
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    return g_app->toggleAnimationFrom + (g_app->toggleAnimationTo - g_app->toggleAnimationFrom) * eased;
}

void animateSettingsToggle(HWND hwnd) {
    if (!hwnd) return;
    if (g_app->toggleAnimationControl && g_app->toggleAnimationControl != hwnd) {
        InvalidateRect(g_app->toggleAnimationControl, nullptr, FALSE);
    }
    const bool target = !settingsToggleValue(hwnd);
    const float from = settingsTogglePosition(hwnd);
    g_app->toggleAnimationControl = hwnd;
    g_app->toggleAnimationFrom = from;
    g_app->toggleAnimationTo = target ? 1.0f : 0.0f;
    g_app->toggleAnimationStartTicks = settingsToggleClock();
    setSettingsToggleValue(hwnd, target);
    if (HWND parent = GetParent(hwnd)) {
        SetTimer(parent, kSettingsToggleTimer, 8, nullptr);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

float languageDropdownProgress() {
    if (!g_app->languageDropdown) return 0.0f;
    const LONGLONG elapsed = settingsToggleClock() - g_app->languageDropdownStartTicks;
    const float progress = std::min(1.0f, static_cast<float>(elapsed) * 1000.0f /
        static_cast<float>(settingsToggleClockFrequency() * kSettingsDropdownAnimationMs));
    if (progress >= 1.0f) return g_app->languageDropdownTo;
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    return g_app->languageDropdownFrom +
        (g_app->languageDropdownTo - g_app->languageDropdownFrom) * eased;
}

void positionLanguageDropdown(HWND settings, float progress) {
    if (!g_app->languageDropdown) return;
    HWND combo = GetDlgItem(settings, kSettingLanguage);
    if (!combo) return;
    RECT comboRect{};
    GetWindowRect(combo, &comboRect);
    POINT topLeft{comboRect.left, comboRect.bottom + ui(4)};
    const int width = comboRect.right - comboRect.left;
    const int fullHeight = ui(90);
    const int height = std::max(1, static_cast<int>(fullHeight * progress + 0.5f));
    SetWindowPos(g_app->languageDropdown, HWND_TOP, topLeft.x, topLeft.y, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    BringWindowToTop(g_app->languageDropdown);
}

void animateLanguageDropdown(HWND settings, float target) {
    if (!g_app->languageDropdown) return;
    g_app->languageDropdownFrom = languageDropdownProgress();
    g_app->languageDropdownTo = target;
    g_app->languageDropdownStartTicks = settingsToggleClock();
    SetTimer(settings, kSettingsDropdownTimer, 8, nullptr);
    positionLanguageDropdown(settings, g_app->languageDropdownFrom);
    InvalidateRect(g_app->languageDropdown, nullptr, FALSE);
}

void toggleLanguageDropdown(HWND settings) {
    if (!g_app->languageDropdown) {
        g_app->languageDropdownHover = -1;
        g_app->languageDropdownFrom = 0.0f;
        g_app->languageDropdownTo = 1.0f;
        g_app->languageDropdownStartTicks = settingsToggleClock();
        HWND combo = GetDlgItem(settings, kSettingLanguage);
        if (!combo) return;
        RECT comboRect{};
        GetWindowRect(combo, &comboRect);
        POINT topLeft{comboRect.left, comboRect.bottom + ui(4)};
        const int width = comboRect.right - comboRect.left;
        g_app->languageDropdown = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"ClipLiteDropdown", L"", WS_POPUP | WS_VISIBLE,
            topLeft.x, topLeft.y, width, 1, settings, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        if (!g_app->languageDropdown) return;
        SetWindowPos(g_app->languageDropdown, HWND_TOP, topLeft.x, topLeft.y, width, 1,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        BringWindowToTop(g_app->languageDropdown);
        SetTimer(settings, kSettingsDropdownTimer, 8, nullptr);
        return;
    }
    animateLanguageDropdown(settings, g_app->languageDropdownTo > 0.5f ? 0.0f : 1.0f);
}

void drawSettingsToggle(const DRAWITEMSTRUCT& item) {
    if (!g_app || g_app->gdiplusToken == 0) return;
    const bool highContrast = highContrastEnabled();
    const bool hovered = g_app->hoveredSettingsControl == GetDlgCtrlID(item.hwndItem);
    const bool checked = settingsToggleValue(item.hwndItem);
    if (g_app->gdiplusToken != 0) {
        auto makeColor = [](COLORREF value) {
            return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
        };
        auto addCapsule = [](Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect) {
            const float radius = std::min(rect.Width, rect.Height) / 2.0f;
            const float diameter = radius * 2.0f;
            path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
            path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter,
                        270.0f, 90.0f);
            path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter,
                        diameter, diameter, 0.0f, 90.0f);
            path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter,
                        90.0f, 90.0f);
            path.CloseFigure();
        };
        const COLORREF surface = highContrast ? GetSysColor(COLOR_WINDOW) :
            settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
        const COLORREF knobSurface = highContrast ? GetSysColor(COLOR_WINDOW) :
            settingsThemeColor(RGB(255, 255, 255), RGB(47, 53, 60));
        const COLORREF accent = highContrast ? GetSysColor(COLOR_HIGHLIGHT) : settingsAccentColor();
        const COLORREF track = checked
            ? (hovered ? settingsThemeColor(RGB(51, 145, 115), RGB(82, 180, 177)) : accent)
            : (hovered ? settingsThemeColor(RGB(174, 191, 187), RGB(95, 105, 116))
                       : settingsThemeColor(RGB(197, 208, 208), RGB(75, 84, 95)));
        const COLORREF trackBorder = highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
            (checked ? track : settingsThemeColor(RGB(155, 171, 167), RGB(112, 123, 135)));
        const int centerY = (item.rcItem.top + item.rcItem.bottom) / 2;
        const float trackWidth = static_cast<float>(ui(kSettingsTrackWidth));
        const float trackHeight = static_cast<float>(ui(kSettingsTrackHeight));
        const float trackRight = static_cast<float>(item.rcItem.right);
        const float trackLeft = std::max(static_cast<float>(item.rcItem.left),
                                         trackRight - trackWidth);
        const float trackInset = 0.5f;
        const Gdiplus::RectF trackRect(trackLeft + trackInset,
                                       static_cast<float>(centerY) - trackHeight / 2.0f + trackInset,
                                       trackWidth - trackInset * 2.0f,
                                       trackHeight - trackInset * 2.0f);
        const float knobSize = static_cast<float>(ui(16));
        const float knobMargin = static_cast<float>(ui(2));
        const float position = settingsTogglePosition(item.hwndItem);
        const float knobTravel = trackRect.Width - knobMargin * 2.0f - knobSize;
        const float knobLeft = trackRect.X + knobMargin + knobTravel * position;
        const Gdiplus::RectF knobRect(knobLeft,
                                      static_cast<float>(centerY) - knobSize / 2.0f,
                                      knobSize, knobSize);

        const int bufferWidth = item.rcItem.right - item.rcItem.left;
        const int bufferHeight = item.rcItem.bottom - item.rcItem.top;
        HDC bufferDc = CreateCompatibleDC(item.hDC);
        HBITMAP bufferBitmap = bufferDc
            ? CreateCompatibleBitmap(item.hDC, bufferWidth, bufferHeight) : nullptr;
        HGDIOBJ oldBufferBitmap = bufferBitmap
            ? SelectObject(bufferDc, bufferBitmap) : nullptr;
        HDC renderDc = bufferBitmap ? bufferDc : item.hDC;
        Gdiplus::Graphics graphics(renderDc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        graphics.TranslateTransform(static_cast<Gdiplus::REAL>(-item.rcItem.left),
                                    static_cast<Gdiplus::REAL>(-item.rcItem.top));
        Gdiplus::SolidBrush surfaceBrush(makeColor(surface));
        graphics.FillRectangle(&surfaceBrush, static_cast<INT>(item.rcItem.left),
                               static_cast<INT>(item.rcItem.top), bufferWidth, bufferHeight);
        Gdiplus::GraphicsPath surfacePath;
        const Gdiplus::RectF surfaceRect(static_cast<float>(item.rcItem.left),
                                          static_cast<float>(item.rcItem.top),
                                          static_cast<float>(bufferWidth),
                                          static_cast<float>(bufferHeight));
        addCapsule(surfacePath, surfaceRect);
        graphics.FillPath(&surfaceBrush, &surfacePath);
        Gdiplus::SolidBrush trackBrush(makeColor(track));
        Gdiplus::Pen trackPen(makeColor(trackBorder), 1.0f);
        Gdiplus::GraphicsPath trackPath;
        addCapsule(trackPath, trackRect);
        graphics.FillPath(&trackBrush, &trackPath);
        graphics.DrawPath(&trackPen, &trackPath);
        Gdiplus::SolidBrush knobBrush(makeColor(knobSurface));
         Gdiplus::Pen knobPen(makeColor(highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
             settingsThemeColor(RGB(224, 231, 228), RGB(214, 220, 226))), 1.0f);
        graphics.FillEllipse(&knobBrush, knobRect);
        graphics.DrawEllipse(&knobPen, knobRect);
        if (bufferBitmap) {
            BitBlt(item.hDC, item.rcItem.left, item.rcItem.top, bufferWidth, bufferHeight,
                   bufferDc, 0, 0, SRCCOPY);
            SelectObject(bufferDc, oldBufferBitmap);
            DeleteObject(bufferBitmap);
            DeleteDC(bufferDc);
        }
        return;
    }
}

bool isSettingsActionButton(int id) {
    return id == kSettingClear || id == kSettingClearText ||
           id == kSettingClearImage || id == kSettingClearFiles ||
           id == kSettingBrowseDataDirectory || id == kSettingOpenLog ||
           id == kSettingSupportAuthor || id == kSettingJoinQqGroup;
}

bool isSettingsClearAction(int id) {
    return id == kSettingClear || id == kSettingClearText ||
           id == kSettingClearImage || id == kSettingClearFiles;
}

const wchar_t* settingsClearActionLabel(int id) {
    switch (id) {
    case kSettingClear: return settingsLocale().historyLabel;
    case kSettingClearText: return settingsLocale().textLabel;
    case kSettingClearImage: return settingsLocale().imagesLabel;
    case kSettingClearFiles: return settingsLocale().filesLabel;
    default: return settingsLocale().historyLabel;
    }
}

std::size_t settingsClearActionCount(int id) {
    if (id == kSettingClear) return g_app->store.activeCount();
    const ClipType type = id == kSettingClearText ? ClipType::Text :
        id == kSettingClearImage ? ClipType::Image :
        id == kSettingClearFiles ? ClipType::Files :
        ClipType::Html;
    return g_app->store.countType(type);
}

bool confirmSettingsClear(HWND hwnd, int id, std::size_t count) {
    if (count == 0) return true;
    wchar_t message[256]{};
    if (id == kSettingClear) {
        swprintf_s(message, settingsLocale().confirmClearAllFormat, count);
    } else {
        swprintf_s(message, settingsLocale().confirmClearTypeFormat,
                    count, settingsClearActionLabel(id));
    }
    return MessageBoxW(hwnd, message, settingsLocale().confirmClearTitle,
                       MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

bool clearSettingsAction(int id) {
    if (id == kSettingClear) return g_app->store.clear();
    const ClipType type = id == kSettingClearText ? ClipType::Text :
        id == kSettingClearImage ? ClipType::Image :
        id == kSettingClearFiles ? ClipType::Files :
        ClipType::Html;
    return g_app->store.clearType(type);
}

void openDiagnosticLog(HWND hwnd) {
    appendDiagnosticLog("INFO", "diagnostics: log file opened by user");
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", diagnosticLogPath().c_str(),
                                           nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(hwnd, settingsLocale().unableOpenLog, settingsLocale().startupFailureTitle,
                    MB_OK | MB_ICONWARNING);
    }
}

void setSettingsActionFeedback(HWND hwnd, const std::wstring& message, bool success) {
    g_app->settingsActionFeedback = message;
    g_app->settingsActionFeedbackSuccess = success;
    SetTimer(hwnd, kSettingsActionFeedbackTimer, kSettingsActionFeedbackMs, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void drawSettingsButton(const DRAWITEMSTRUCT& item) {
    const int id = GetDlgCtrlID(item.hwndItem);
    const bool neutral = id == kSettingBrowseDataDirectory || id == kSettingOpenLog ||
        id == kSettingSupportAuthor || id == kSettingJoinQqGroup;
    const bool hovered = g_app->hoveredSettingsControl == id;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0 || GetFocus() == item.hwndItem;
    const bool highContrast = highContrastEnabled();
    const COLORREF background = neutral
        ? (highContrast ? GetSysColor(COLOR_BTNFACE) :
           settingsThemeColor(pressed ? RGB(226, 232, 240) :
                              (hovered ? RGB(239, 246, 255) : RGB(248, 250, 252)),
                              pressed ? RGB(55, 65, 81) : RGB(30, 41, 59)))
        : (highContrast ? GetSysColor(pressed ? COLOR_HIGHLIGHT : COLOR_BTNFACE) :
           settingsThemeColor(pressed ? RGB(254, 226, 226) :
                              (hovered ? RGB(254, 242, 242) : RGB(255, 247, 247)),
                              pressed ? RGB(93, 42, 52) :
                              (hovered ? RGB(67, 35, 42) : RGB(52, 42, 48))));
    const COLORREF border = neutral
        ? (highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
           (hovered || pressed ? settingsAccentColor() :
                                 settingsThemeColor(RGB(200, 211, 222), RGB(74, 88, 104))))
        : (highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
           settingsThemeColor(hovered || pressed ? RGB(239, 68, 68) : RGB(252, 165, 165),
                              hovered || pressed ? RGB(248, 113, 113) : RGB(139, 70, 80)));
    const COLORREF text = neutral
        ? (highContrast ? GetSysColor(COLOR_WINDOWTEXT) : settingsAccentColor())
        : (highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
           settingsThemeColor(pressed ? RGB(153, 27, 27) : RGB(185, 28, 28),
                              RGB(248, 160, 160)));
    const int offset = pressed ? ui(1) : 0;
    RECT buttonRect = item.rcItem;
    OffsetRect(&buttonRect, 0, offset);
    drawGdiRoundedSurface(item.hDC, buttonRect, background, border, 7);
    wchar_t label[256]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(sizeof(label) / sizeof(label[0])));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text);
    DrawTextW(item.hDC, label, -1, &buttonRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (focused && !highContrast) {
        RECT focusRect = buttonRect;
        InflateRect(&focusRect, -ui(3), -ui(3));
        DrawFocusRect(item.hDC, &focusRect);
    }
}

void drawSettingsShortcut(const DRAWITEMSTRUCT& item) {
    const bool capturing = g_app->shortcutCaptureControl == item.hwndItem;
    const bool focused = (item.itemState & ODS_FOCUS) != 0 || GetFocus() == item.hwndItem;
    const bool hovered = g_app->hoveredSettingsControl == GetDlgCtrlID(item.hwndItem);
    const COLORREF background = settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
    const COLORREF border = highContrastEnabled() ? GetSysColor(COLOR_WINDOWTEXT) :
        (capturing || focused ? settingsAccentColor() :
         hovered ? settingsAccentColor() :
                   settingsThemeColor(RGB(200, 211, 222), RGB(74, 88, 104)));
    drawGdiRoundedSurface(item.hDC, item.rcItem, background, border, 6);
    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(sizeof(label) / sizeof(label[0])));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, capturing ? settingsAccentColor() :
                                    settingsThemeColor(settingsAccentColor(), RGB(238, 241, 245)));
    DrawTextW(item.hDC, label, -1, const_cast<RECT*>(&item.rcItem),
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void drawSettingsLanguage(const DRAWITEMSTRUCT& item) {
    const COLORREF input = settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
    const COLORREF selected = settingsAccentSoftColor();
    const COLORREF text = settingsThemeColor(RGB(30, 36, 46), RGB(238, 241, 245));
    const bool hovered = g_app->hoveredSettingsControl == GetDlgCtrlID(item.hwndItem);
    HBRUSH brush = CreateSolidBrush((item.itemState & ODS_SELECTED) ? selected :
                                    (hovered ? settingsAccentSoftColor() : input));
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
    textRect.left += ui(8);
    DrawTextW(item.hDC, value, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

int languageDropdownRowAt(int y) {
    const int row = y / ui(30);
    return row >= 0 && row < 3 ? row : -1;
}

void paintLanguageDropdown(HWND hwnd, HDC dc) {
    RECT windowRect{};
    GetWindowRect(hwnd, &windowRect);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    const int contentWidth = width;
    const int renderHeight = height;
    HDC bufferDc = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap = bufferDc ? CreateCompatibleBitmap(dc, contentWidth, renderHeight) : nullptr;
    HGDIOBJ oldBitmap = bufferBitmap ? SelectObject(bufferDc, bufferBitmap) : nullptr;
    HDC renderDc = bufferBitmap ? bufferDc : dc;
    const bool highContrast = highContrastEnabled();
    const COLORREF background = highContrast ? GetSysColor(COLOR_WINDOW) :
        settingsThemeColor(RGB(255, 255, 255), RGB(38, 43, 49));
    const COLORREF border = highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
        settingsThemeColor(RGB(205, 219, 215), RGB(82, 92, 102));
    const COLORREF text = highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
        settingsThemeColor(RGB(30, 36, 46), RGB(238, 241, 245));
    const COLORREF accent = highContrast ? GetSysColor(COLOR_HIGHLIGHT) : settingsAccentColor();

    if (g_app->gdiplusToken != 0) {
        auto makeColor = [](COLORREF value, BYTE alpha = 255) {
            return Gdiplus::Color(alpha, GetRValue(value), GetGValue(value), GetBValue(value));
        };
        auto addRoundedRect = [](Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect,
                                 float radius) {
            const float diameter = radius * 2.0f;
            path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
            path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter,
                        270.0f, 90.0f);
            path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter,
                        diameter, diameter, 0.0f, 90.0f);
            path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter,
                        90.0f, 90.0f);
            path.CloseFigure();
        };
        Gdiplus::Graphics graphics(renderDc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        Gdiplus::SolidBrush backgroundBrush(makeColor(background));
        graphics.FillRectangle(&backgroundBrush, 0, 0, contentWidth, renderHeight);
        const Gdiplus::RectF popupRect(0.5f, 0.5f,
                                       static_cast<float>(std::max(1, contentWidth - 1)),
                                       static_cast<float>(std::max(1, renderHeight - 1)));
        const float popupRadius = std::min(static_cast<float>(ui(6)),
                                           std::min(popupRect.Width, popupRect.Height) / 2.0f);
        Gdiplus::GraphicsPath popupPath;
        addRoundedRect(popupPath, popupRect, popupRadius);
        Gdiplus::SolidBrush popupBrush(makeColor(background));
        Gdiplus::Pen popupPen(makeColor(border), 1.0f);
        graphics.FillPath(&popupBrush, &popupPath);
        graphics.DrawPath(&popupPen, &popupPath);
        for (int row = 0; row < 3; ++row) {
            if (row * ui(30) >= renderHeight) break;
            const bool active = row == settingsLanguageSelection(
                GetDlgItem(g_app->settings, kSettingLanguage));
            const bool hovered = row == g_app->languageDropdownHover;
            if (!active && !hovered) continue;
            const BYTE alpha = active ? 42 : 24;
            Gdiplus::SolidBrush rowBrush(makeColor(accent, alpha));
            const Gdiplus::RectF rowRect(static_cast<float>(ui(2)),
                                         static_cast<float>(ui(row * 30 + 2)),
                                         static_cast<float>(contentWidth - ui(4)),
                                         static_cast<float>(ui(26)));
            Gdiplus::GraphicsPath rowPath;
            addRoundedRect(rowPath, rowRect, static_cast<float>(ui(4)));
            graphics.FillPath(&rowBrush, &rowPath);
        }
    }

    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(renderDc, g_app->settingsFont));
    SetBkMode(renderDc, TRANSPARENT);
    const wchar_t* labels[] = {settingsLocale().autoLanguage, L"English", L"简体中文"};
    const int selected = settingsLanguageSelection(
        GetDlgItem(g_app->settings, kSettingLanguage));
    for (int row = 0; row < 3; ++row) {
        if (row * ui(30) >= renderHeight) break;
        RECT rowRect{ui(10), ui(row * 30 + 3), contentWidth - ui(30), ui(row * 30 + 27)};
        SetTextColor(renderDc, text);
        DrawTextW(renderDc, labels[row], -1, &rowRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (row == selected) {
            RECT checkRect{contentWidth - ui(26), ui(row * 30 + 3),
                           contentWidth - ui(8), ui(row * 30 + 27)};
            SetTextColor(renderDc, accent);
            DrawTextW(renderDc, L"\x2713", -1, &checkRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    SelectObject(renderDc, oldFont);
    if (bufferBitmap) {
        BitBlt(dc, 0, 0, contentWidth, renderHeight, bufferDc, 0, 0, SRCCOPY);
        SelectObject(bufferDc, oldBitmap);
        DeleteObject(bufferBitmap);
        DeleteDC(bufferDc);
    }
}

void paintSettingsLanguageCombo(HWND hwnd, HDC dc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const bool highContrast = highContrastEnabled();
    const bool active = g_app->languageDropdown != nullptr;
    const bool hovered = g_app->hoveredSettingsControl == kSettingLanguage;
    const COLORREF background = highContrast ? GetSysColor(COLOR_WINDOW) :
        settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
    const COLORREF border = highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
        ((active || hovered) ? settingsAccentColor()
                             : settingsThemeColor(RGB(205, 219, 215), RGB(82, 92, 102)));
    const COLORREF text = highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
        settingsThemeColor(RGB(30, 36, 46), RGB(238, 241, 245));
    const COLORREF arrow = highContrast ? GetSysColor(COLOR_HIGHLIGHT) :
        settingsThemeColor(RGB(75, 85, 82), RGB(190, 199, 207));
    HDC bufferDc = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap = bufferDc ? CreateCompatibleBitmap(dc, width, height) : nullptr;
    HGDIOBJ oldBitmap = bufferBitmap ? SelectObject(bufferDc, bufferBitmap) : nullptr;
    HDC renderDc = bufferBitmap ? bufferDc : dc;

    if (g_app->gdiplusToken != 0) {
        auto makeColor = [](COLORREF value) {
            return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
        };
        auto addRoundedRect = [](Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect,
                                 float radius) {
            const float diameter = radius * 2.0f;
            path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
            path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter,
                        270.0f, 90.0f);
            path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter,
                        diameter, diameter, 0.0f, 90.0f);
            path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter,
                        90.0f, 90.0f);
            path.CloseFigure();
        };
        Gdiplus::Graphics graphics(renderDc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        Gdiplus::SolidBrush backgroundBrush(makeColor(background));
        graphics.FillRectangle(&backgroundBrush, 0, 0, width, height);
        const Gdiplus::RectF box(0.5f, 0.5f, static_cast<float>(width - 1),
                                 static_cast<float>(height - 1));
        Gdiplus::GraphicsPath boxPath;
        addRoundedRect(boxPath, box, static_cast<float>(ui(4)));
        Gdiplus::SolidBrush boxBrush(makeColor(background));
        Gdiplus::Pen boxPen(makeColor(border), 1.0f);
        graphics.FillPath(&boxBrush, &boxPath);
        graphics.DrawPath(&boxPen, &boxPath);
        Gdiplus::GraphicsPath arrowPath;
        const float arrowX = static_cast<float>(width - ui(18));
        const float arrowY = static_cast<float>(height / 2 - ui(2));
        arrowPath.AddLine(arrowX - ui(4), arrowY, arrowX + ui(4), arrowY);
        arrowPath.AddLine(arrowX + ui(4), arrowY, arrowX, arrowY + ui(5));
        arrowPath.AddLine(arrowX, arrowY + ui(5), arrowX - ui(4), arrowY);
        Gdiplus::SolidBrush arrowBrush(makeColor(arrow));
        graphics.FillPath(&arrowBrush, &arrowPath);
    }

    wchar_t value[64]{};
    const int index = settingsLanguageSelection(hwnd);
    const wchar_t* labels[] = {settingsLocale().autoLanguage, L"English", L"简体中文"};
    wcscpy_s(value, labels[index]);
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(renderDc, g_app->settingsFont));
    SetBkMode(renderDc, TRANSPARENT);
    SetTextColor(renderDc, text);
    RECT textRect{ui(10), 0, width - ui(28), height};
    DrawTextW(renderDc, value, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(renderDc, oldFont);
    if (bufferBitmap) {
        BitBlt(dc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
        SelectObject(bufferDc, oldBitmap);
        DeleteObject(bufferBitmap);
        DeleteDC(bufferDc);
    }
}

void paintSettingsEdit(HWND hwnd, HDC dc, WNDPROC oldProc) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC bufferDc = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap = bufferDc ? CreateCompatibleBitmap(dc, width, height) : nullptr;
    if (!bufferDc || !bufferBitmap) {
        if (bufferBitmap) DeleteObject(bufferBitmap);
        if (bufferDc) DeleteDC(bufferDc);
        CallWindowProcW(oldProc, hwnd, WM_PRINTCLIENT,
                        reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(bufferDc, bufferBitmap);
    const bool highContrast = highContrastEnabled();
    const int id = GetDlgCtrlID(hwnd);
    const bool focused = GetFocus() == hwnd;
    const bool hovered = g_app->hoveredSettingsControl == id;
    const COLORREF background = highContrast ? GetSysColor(COLOR_WINDOW) :
        settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
    const COLORREF border = highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
        (focused ? settingsAccentColor()
                 : hovered ? settingsAccentColor()
                           : settingsThemeColor(RGB(200, 211, 222), RGB(74, 88, 104)));
    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(bufferDc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);

    HRGN clip = CreateRoundRectRgn(0, 0, width + 1, height + 1, ui(12), ui(12));
    SelectClipRgn(bufferDc, clip);
    CallWindowProcW(oldProc, hwnd, WM_PRINTCLIENT,
                    reinterpret_cast<WPARAM>(bufferDc), PRF_CLIENT);
    SelectClipRgn(bufferDc, nullptr);
    if (clip) DeleteObject(clip);

    if (id == kSettingIgnoredApps && GetWindowTextLengthW(hwnd) == 0 && !focused) {
        RECT placeholder{ui(10), ui(8), width - ui(10), height - ui(8)};
        SetBkMode(bufferDc, TRANSPARENT);
        SetTextColor(bufferDc, highContrast ? GetSysColor(COLOR_GRAYTEXT) :
            settingsThemeColor(RGB(150, 160, 157), RGB(145, 155, 166)));
        DrawTextW(bufferDc, settingsLocale().ignoredAppsPlaceholder, -1,
                  &placeholder, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (g_app->gdiplusToken != 0) {
        auto makeColor = [](COLORREF value) {
            return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
        };
        Gdiplus::Graphics graphics(bufferDc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::GraphicsPath path;
        const Gdiplus::RectF box(1.0f, 1.0f, static_cast<float>(width - 2),
                                 static_cast<float>(height - 2));
        const float radius = static_cast<float>(ui(6));
        const float diameter = radius * 2.0f;
        path.AddArc(box.X, box.Y, diameter, diameter, 180.0f, 90.0f);
        path.AddArc(box.X + box.Width - diameter, box.Y, diameter, diameter, 270.0f, 90.0f);
        path.AddArc(box.X + box.Width - diameter, box.Y + box.Height - diameter,
                    diameter, diameter, 0.0f, 90.0f);
        path.AddArc(box.X, box.Y + box.Height - diameter, diameter, diameter, 90.0f, 90.0f);
        path.CloseFigure();
        if (focused && !highContrast) {
            const COLORREF shadowColor = settingsAccentColor();
            Gdiplus::Color shadow(72, GetRValue(shadowColor), GetGValue(shadowColor),
                                  GetBValue(shadowColor));
            Gdiplus::Pen shadowPen(shadow, 2.0f);
            graphics.DrawPath(&shadowPen, &path);
        }
        graphics.Flush(Gdiplus::FlushIntentionSync);
        Gdiplus::Pen borderPen(makeColor(border), 1.0f);
        graphics.DrawPath(&borderPen, &path);
    }
    BitBlt(dc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);
    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bufferBitmap);
    DeleteDC(bufferDc);
}

bool settingsNumericEditRange(int id, long& minimum, long& maximum) {
    if (id == kSettingMaxItems ||
        (id >= kSettingCategoryMaxBase && id < kSettingCategoryMaxBase + kStorageCategoryCount)) {
        minimum = 0;
        maximum = 100000;
        return true;
    }
    if (id == kSettingRetentionDays) {
        minimum = 0;
        maximum = 36500;
        return true;
    }
    if (id == kSettingMaxDiskMb ||
        (id >= kSettingCategoryDiskBase && id < kSettingCategoryDiskBase + kStorageCategoryCount)) {
        minimum = 0;
        maximum = 102400;
        return true;
    }
    if (id == kSettingMaxContentMb) {
        minimum = 1;
        maximum = 32;
        return true;
    }
    if (id == kSettingSensitiveExpiry) {
        minimum = 0;
        maximum = 720;
        return true;
    }
    return false;
}

bool normalizeSettingsNumericEdit(HWND hwnd) {
    if (!hwnd) return false;
    long minimum = 0;
    long maximum = 0;
    if (!settingsNumericEditRange(GetDlgCtrlID(hwnd), minimum, maximum)) return false;
    wchar_t value[32]{};
    GetWindowTextW(hwnd, value, static_cast<int>(sizeof(value) / sizeof(value[0])));
    wchar_t* end = nullptr;
    const long parsed = value[0] == L'\0' ? minimum : std::wcstol(value, &end, 10);
    const long normalized = value[0] == L'\0' || end == value
        ? minimum : std::clamp(parsed, minimum, maximum);
    wchar_t normalizedText[32]{};
    swprintf_s(normalizedText, L"%ld", normalized);
    if (std::wcscmp(value, normalizedText) == 0) return false;
    SetWindowTextW(hwnd, normalizedText);
    return true;
}

void stripSettingsNumericLeadingZeros(HWND hwnd) {
    if (!hwnd) return;
    long minimum = 0;
    long maximum = 0;
    if (!settingsNumericEditRange(GetDlgCtrlID(hwnd), minimum, maximum)) return;
    (void)minimum;
    (void)maximum;
    wchar_t value[64]{};
    const int length = GetWindowTextW(hwnd, value,
                                      static_cast<int>(sizeof(value) / sizeof(value[0])));
    if (length <= 1) return;
    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
                 reinterpret_cast<LPARAM>(&selectionEnd));
    if (selectionStart != selectionEnd) return;
    int firstNonZero = 0;
    while (firstNonZero < length && value[firstNonZero] == L'0') ++firstNonZero;
    if (firstNonZero == 0) return;
    if (firstNonZero == length) {
        SetWindowTextW(hwnd, L"0");
        SendMessageW(hwnd, EM_SETSEL, 1, 1);
        return;
    }
    const int newLength = length - firstNonZero;
    std::memmove(value, value + firstNonZero,
                 static_cast<std::size_t>(newLength + 1) * sizeof(wchar_t));
    SetWindowTextW(hwnd, value);
    const DWORD caret = selectionStart > static_cast<DWORD>(firstNonZero)
        ? selectionStart - static_cast<DWORD>(firstNonZero) : 0;
    SendMessageW(hwnd, EM_SETSEL, caret, caret);
}

bool settingsEditBorderRect(HWND edit, HWND parent, RECT& rect) {
    if (!edit || !parent || !IsWindowVisible(edit)) return false;
    GetWindowRect(edit, &rect);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
    InflateRect(&rect, ui(1), ui(1));
    return true;
}

void invalidateSettingsEditBorder(HWND edit) {
    HWND parent = edit ? GetParent(edit) : nullptr;
    RECT rect{};
    if (settingsEditBorderRect(edit, parent, rect)) InvalidateRect(parent, &rect, FALSE);
}

void paintSettingsEditBorders(HWND hwnd, HDC dc) {
    const int ids[] = {kSettingMaxItems, kSettingRetentionDays, kSettingMaxDiskMb,
                       kSettingMaxContentMb, kSettingIgnoredApps, kSettingSensitiveExpiry};
    const bool highContrast = highContrastEnabled();
    for (const int id : ids) {
        HWND edit = GetDlgItem(hwnd, id);
        RECT rect{};
        if (!settingsEditBorderRect(edit, hwnd, rect)) continue;
        const bool focused = GetFocus() == edit;
        const bool hovered = g_app->hoveredSettingsControl == id;
        const COLORREF border = highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
            (focused ? settingsAccentColor() :
             hovered ? settingsThemeColor(RGB(120, 145, 137), RGB(119, 132, 145)) :
                       settingsThemeColor(RGB(205, 219, 215), RGB(82, 92, 102)));
        if (g_app->gdiplusToken != 0) {
            auto makeColor = [](COLORREF value) {
                return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value));
            };
            Gdiplus::Graphics graphics(dc);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const Gdiplus::RectF box(static_cast<float>(rect.left) + 0.5f,
                                     static_cast<float>(rect.top) + 0.5f,
                                     static_cast<float>(rect.right - rect.left - 1),
                                     static_cast<float>(rect.bottom - rect.top - 1));
            const float radius = static_cast<float>(ui(4));
            const float diameter = radius * 2.0f;
            Gdiplus::GraphicsPath path;
            path.AddArc(box.X, box.Y, diameter, diameter, 180.0f, 90.0f);
            path.AddArc(box.X + box.Width - diameter, box.Y, diameter, diameter,
                        270.0f, 90.0f);
            path.AddArc(box.X + box.Width - diameter, box.Y + box.Height - diameter,
                        diameter, diameter, 0.0f, 90.0f);
            path.AddArc(box.X, box.Y + box.Height - diameter, diameter, diameter,
                        90.0f, 90.0f);
            path.CloseFigure();
            Gdiplus::Pen pen(makeColor(border), 1.0f);
            graphics.DrawPath(&pen, &path);
        }
    }
}

void configureSettingsEdit(HWND hwnd) {
    if (!hwnd) return;
    const int id = GetDlgCtrlID(hwnd);
    UINT textLimit = 0;
    if (id == kSettingMaxItems || id >= kSettingCategoryMaxBase &&
        id < kSettingCategoryMaxBase + kStorageCategoryCount) {
        textLimit = 6;
    } else if (id == kSettingRetentionDays) {
        textLimit = 5;
    } else if (id == kSettingMaxDiskMb || id >= kSettingCategoryDiskBase &&
               id < kSettingCategoryDiskBase + kStorageCategoryCount) {
        textLimit = 6;
    } else if (id == kSettingMaxContentMb) {
        textLimit = 2;
    } else if (id == kSettingSensitiveExpiry) {
        textLimit = 3;
    }
    if (textLimit != 0) SendMessageW(hwnd, EM_LIMITTEXT, textLimit, 0);
    SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(ui(8), ui(8)));
    RECT client{};
    GetClientRect(hwnd, &client);
    RECT format{ui(8), ui(5), client.right - ui(8), client.bottom - ui(5)};
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (id != kSettingIgnoredApps) {
        HDC dc = GetDC(hwnd);
        TEXTMETRICW metrics{};
        if (dc) {
            HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
            HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
            GetTextMetricsW(dc, &metrics);
            if (oldFont) SelectObject(dc, oldFont);
            ReleaseDC(hwnd, dc);
        }
        const int clientHeight = static_cast<int>(client.bottom);
        const int textHeight = std::max(ui(16), static_cast<int>(metrics.tmHeight));
        const int top = std::max(ui(2), (clientHeight - textHeight) / 2);
        format.top = top;
        format.bottom = std::min(clientHeight - ui(3), top + textHeight + ui(2));
    } else if ((style & ES_MULTILINE) == 0) {
        format.top = 0;
        format.bottom = client.bottom;
    } else {
        format.top = ui(6);
        format.bottom = client.bottom - ui(6);
    }
    SendMessageW(hwnd, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&format));
    InvalidateRect(hwnd, nullptr, FALSE);
    invalidateSettingsEditBorder(hwnd);
}

LRESULT CALLBACK settingsControlProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    const WNDPROC oldProc = reinterpret_cast<WNDPROC>(GetPropW(hwnd, L"ClipLiteOldProc"));
    if (!oldProc) return DefWindowProcW(hwnd, message, wParam, lParam);
    const int id = GetDlgCtrlID(hwnd);
    wchar_t className[32]{};
    GetClassNameW(hwnd, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
    if (std::wcscmp(className, L"Edit") == 0 && message == WM_ERASEBKGND) {
        return 1;
    }
    if (isSettingsToggle(id) && message == WM_ERASEBKGND) return 1;
    if (id == kSettingLanguage && message == WM_ERASEBKGND) return 1;
    if (isSettingsShortcut(id)) {
        if (message == WM_LBUTTONDOWN) {
            beginSettingsShortcutCapture(GetParent(hwnd), hwnd);
            return 0;
        }
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
            captureSettingsShortcut(GetParent(hwnd), hwnd, static_cast<UINT>(wParam));
            return 0;
        }
        if (message == WM_KILLFOCUS && g_app->shortcutCaptureControl == hwnd) {
            cancelSettingsShortcutCapture(GetParent(hwnd));
            return 0;
        }
    }
    if (message == WM_MOUSEWHEEL && id != kSettingIgnoredApps) {
        SendMessageW(GetParent(hwnd), message, wParam, lParam);
        return 0;
    }
    if (std::wcscmp(className, L"Edit") == 0 && id != kSettingIgnoredApps) {
        if (message == WM_CHAR && wParam >= L'0' && wParam <= L'9') {
            wchar_t value[8]{};
            GetWindowTextW(hwnd, value, static_cast<int>(sizeof(value) / sizeof(value[0])));
            if (std::wcscmp(value, L"0") == 0 && wParam == L'0') return 0;
            stripSettingsNumericLeadingZeros(hwnd);
        }
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wParam == VK_RETURN) return 0;
        if (message == WM_CHAR && (wParam == VK_RETURN || wParam == L'\n')) return 0;
    }
    if (id == kSettingLanguage && message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        paintSettingsLanguageCombo(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (id == kSettingLanguage && message == WM_LBUTTONDOWN) {
        SendMessageW(hwnd, CB_SHOWDROPDOWN, FALSE, 0);
        SetFocus(hwnd);
        toggleLanguageDropdown(GetParent(hwnd));
        return 0;
    }
    if (id == kSettingLanguage && message == WM_LBUTTONDBLCLK) {
        SendMessageW(hwnd, CB_SHOWDROPDOWN, FALSE, 0);
        SetFocus(hwnd);
        toggleLanguageDropdown(GetParent(hwnd));
        return 0;
    }
    if (id == kSettingLanguage &&
        (message == WM_LBUTTONUP || message == WM_NCLBUTTONDOWN || message == WM_NCLBUTTONDBLCLK)) {
        SendMessageW(hwnd, CB_SHOWDROPDOWN, FALSE, 0);
        return 0;
    }
    if (id == kSettingLanguage && message == WM_KEYDOWN &&
        (wParam == VK_SPACE || wParam == VK_RETURN || wParam == VK_DOWN || wParam == VK_F4)) {
        SendMessageW(hwnd, CB_SHOWDROPDOWN, FALSE, 0);
        toggleLanguageDropdown(GetParent(hwnd));
        return 0;
    }
    if (message == WM_LBUTTONDOWN && g_app->languageDropdown) {
        animateLanguageDropdown(GetParent(hwnd), 0.0f);
    }
    if (message == WM_MOUSEMOVE) {
        SetCursor(std::wcscmp(className, L"Edit") == 0
            ? LoadCursorW(nullptr, IDC_IBEAM) : LoadCursorW(nullptr, IDC_HAND));
        if (g_app->hoveredSettingsControl != id) {
            const int previous = g_app->hoveredSettingsControl;
            g_app->hoveredSettingsControl = id;
            if (previous != 0) invalidateSettingsEditBorder(GetDlgItem(GetParent(hwnd), previous));
            invalidateSettingsEditBorder(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
    } else if (message == WM_MOUSELEAVE) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        if (g_app->hoveredSettingsControl == id) {
            g_app->hoveredSettingsControl = 0;
            invalidateSettingsEditBorder(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (message == WM_SETCURSOR) {
        SetCursor(std::wcscmp(className, L"Edit") == 0
            ? LoadCursorW(nullptr, IDC_IBEAM) : LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    } else if (message == WM_PAINT && std::wcscmp(className, L"Edit") == 0) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc) paintSettingsEdit(hwnd, dc, oldProc);
        EndPaint(hwnd, &ps);
        return 0;
    } else if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        const LRESULT result = CallWindowProcW(oldProc, hwnd, message, wParam, lParam);
        if (std::wcscmp(className, L"Edit") == 0) {
            if (message == WM_KILLFOCUS) {
                long minimum = 0;
                long maximum = 0;
                if (settingsNumericEditRange(id, minimum, maximum)) {
                    normalizeSettingsNumericEdit(hwnd);
                    if (!g_app->settingsClosing) scheduleSettingsSync(GetParent(hwnd));
                }
            }
            configureSettingsEdit(hwnd);
        }
        if (std::wcscmp(className, L"Edit") == 0) invalidateSettingsEditBorder(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    } else if (message == WM_NCDESTROY) {
        const LRESULT result = CallWindowProcW(oldProc, hwnd, message, wParam, lParam);
        RemovePropW(hwnd, L"ClipLiteOldProc");
        if (g_app->hoveredSettingsControl == id) g_app->hoveredSettingsControl = 0;
        return result;
    }
    return CallWindowProcW(oldProc, hwnd, message, wParam, lParam);
}

void subclassSettingsControls(HWND hwnd) {
    for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        wchar_t className[32]{};
        GetClassNameW(child, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
        if (std::wcscmp(className, L"Button") != 0 &&
            std::wcscmp(className, L"ComboBox") != 0 &&
            std::wcscmp(className, L"Edit") != 0 &&
            !(std::wcscmp(className, L"Static") == 0 &&
              GetDlgCtrlID(child) == kSettingLanguage)) {
            continue;
        }
        if (GetPropW(child, L"ClipLiteOldProc")) continue;
        const WNDPROC oldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(settingsControlProc)));
        SetPropW(child, L"ClipLiteOldProc", reinterpret_cast<HANDLE>(oldProc));
    }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == kPopupSearchCompleteMessage) {
        std::unique_ptr<PopupSearchResult> result(
            reinterpret_cast<PopupSearchResult*>(wParam));
        if (!result || !g_app || g_app->popup != result->popup ||
            result->generation != g_app->searchGeneration) {
            return 0;
        }
        if (result->storeRevision != g_app->store.revision()) {
            startPopupSearch(result->popup);
            return 0;
        }
        g_app->searchCancellation.reset();
        applyVisibleCandidates(result->candidates);
        return 0;
    }
    if (hwnd == g_app->settings && message == WM_DRAWITEM) {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_BUTTON && isSettingsToggle(GetDlgCtrlID(item->hwndItem))) {
            drawSettingsToggle(*item);
            return TRUE;
        }
        if (item && item->CtlType == ODT_BUTTON && isSettingsShortcut(GetDlgCtrlID(item->hwndItem))) {
            drawSettingsShortcut(*item);
            return TRUE;
        }
        if (item && item->CtlType == ODT_BUTTON && isSettingsActionButton(GetDlgCtrlID(item->hwndItem))) {
            drawSettingsButton(*item);
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
            g_app->settingsControlsTab = -1;
            g_app->settingsFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_app->settingsMultilineFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            createSettingsPaintFonts();
            const bool highContrast = highContrastEnabled();
            const COLORREF background = highContrast ? GetSysColor(COLOR_WINDOW) :
                settingsThemeColor(RGB(240, 244, 248), RGB(21, 26, 34));
            const COLORREF card = highContrast ? GetSysColor(COLOR_WINDOW) :
                settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
            const COLORREF input = highContrast ? GetSysColor(COLOR_WINDOW) :
                settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48));
            g_app->settingsBackgroundBrush = CreateSolidBrush(background);
            g_app->settingsCardBrush = CreateSolidBrush(card);
            g_app->settingsInputBrush = CreateSolidBrush(input);
            createSettingsControlsModern(hwnd);
            subclassSettingsControls(hwnd);
            updateSettingsTabControls(hwnd);
            applyFontToChildren(hwnd, g_app->settingsFont);
            for (HWND child = GetWindow(hwnd, GW_CHILD); child;
                 child = GetWindow(child, GW_HWNDNEXT)) {
                wchar_t childClass[32]{};
                GetClassNameW(child, childClass,
                              static_cast<int>(sizeof(childClass) / sizeof(childClass[0])));
                if (std::wcscmp(childClass, L"Edit") == 0) configureSettingsEdit(child);
                if (GetDlgCtrlID(child) == kSettingIgnoredApps && g_app->settingsMultilineFont) {
                    SendMessageW(child, WM_SETFONT,
                                 reinterpret_cast<WPARAM>(g_app->settingsMultilineFont), TRUE);
                }
            }
            return 0;
        }
        if (std::wcscmp(className, L"ClipLitePopup") == 0) {
            g_app->popupFont = CreateFontW(-ui(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            createPopupPaintFonts();
            g_app->popupInputBrush = CreateSolidBrush(highContrastEnabled()
                ? GetSysColor(COLOR_WINDOW)
                : settingsThemeColor(RGB(255, 255, 255), RGB(43, 47, 54)));
            const int searchEditWidth = kPopupSearchRight - kPopupSearchLeft - 30;
            g_app->searchEdit = CreateWindowExW(0, L"EDIT", L"",
                                                 WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOHSCROLL,
                                                 ui(kPopupSearchLeft + 27), ui(15), ui(searchEditWidth), ui(30), hwnd,
                                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEdit)),
                                                 GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_app->searchEdit, WM_SETFONT,
                         reinterpret_cast<WPARAM>(g_app->popupFont), TRUE);
            SendMessageW(g_app->searchEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(ui(0), ui(2)));
            HDC editDc = GetDC(g_app->searchEdit);
            TEXTMETRICW textMetrics{};
            if (editDc) {
                HGDIOBJ oldFont = SelectObject(editDc, g_app->popupFont);
                GetTextMetricsW(editDc, &textMetrics);
                SelectObject(editDc, oldFont);
                ReleaseDC(g_app->searchEdit, editDc);
            }
            const int editHeight = ui(30);
            const int textHeight = textMetrics.tmHeight > 0
                ? static_cast<int>(textMetrics.tmHeight) : ui(14);
            const int verticalPadding = std::max(ui(4),
                (editHeight - textHeight) / 2);
            RECT editFormat{0, verticalPadding, ui(searchEditWidth - 2),
                            editHeight - verticalPadding};
            SendMessageW(g_app->searchEdit, EM_SETRECTNP, 0,
                         reinterpret_cast<LPARAM>(&editFormat));
            SendMessageW(g_app->searchEdit, EM_SETCUEBANNER, FALSE,
            reinterpret_cast<LPARAM>(settingsLocale().popupSearchPlaceholder));
            g_app->oldEditProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                g_app->searchEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editProc)));
            return 0;
        }
    }
    if (message == WM_ERASEBKGND && (hwnd == g_app->settings || hwnd == g_app->popup)) {
        return 1;
    }
    if (hwnd == g_app->languageDropdown && message == WM_ERASEBKGND) return 1;
    if ((message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORBTN ||
         message == WM_CTLCOLORLISTBOX) &&
        (hwnd == g_app->settings || hwnd == g_app->popup)) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const bool highContrast = highContrastEnabled();
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, highContrast ? GetSysColor(COLOR_WINDOWTEXT) :
            settingsThemeColor(RGB(30, 41, 59), RGB(226, 232, 240)));
        if (hwnd == g_app->settings && message == WM_CTLCOLOREDIT) {
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, highContrast ? GetSysColor(COLOR_WINDOW) :
                settingsThemeColor(RGB(255, 255, 255), RGB(30, 37, 48)));
            return reinterpret_cast<LRESULT>(g_app->settingsInputBrush);
        }
        if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, highContrast ? GetSysColor(COLOR_WINDOW) :
                settingsThemeColor(RGB(255, 255, 255), RGB(43, 47, 54)));
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
    if (hwnd == g_app->languageDropdown) {
        if (message == WM_MOUSEMOVE) {
            RECT client{};
            GetClientRect(hwnd, &client);
            const int row = languageDropdownRowAt(GET_Y_LPARAM(lParam));
            if (row != g_app->languageDropdownHover) {
                const int previous = g_app->languageDropdownHover;
                g_app->languageDropdownHover = row;
                RECT previousRect{0, previous < 0 ? 0 : ui(previous * 30),
                                  0, previous < 0 ? 0 : ui((previous + 1) * 30)};
                RECT currentRect{0, row < 0 ? 0 : ui(row * 30),
                                 0, row < 0 ? 0 : ui((row + 1) * 30)};
                if (previous >= 0) {
                    previousRect.right = client.right;
                    InvalidateRect(hwnd, &previousRect, FALSE);
                }
                if (row >= 0) {
                    currentRect.right = client.right;
                    InvalidateRect(hwnd, &currentRect, FALSE);
                }
            }
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            if (g_app->languageDropdownHover >= 0) {
                RECT rect{0, ui(g_app->languageDropdownHover * 30),
                           0, ui((g_app->languageDropdownHover + 1) * 30)};
                RECT client{};
                GetClientRect(hwnd, &client);
                rect.right = client.right;
                InvalidateRect(hwnd, &rect, FALSE);
                g_app->languageDropdownHover = -1;
            }
            return 0;
        }
        if (message == WM_SETCURSOR) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        if (message == WM_LBUTTONUP) {
            const int row = languageDropdownRowAt(GET_Y_LPARAM(lParam));
            if (row >= 0) {
                HWND combo = GetDlgItem(g_app->settings, kSettingLanguage);
                setSettingsLanguageSelection(combo, row);
                InvalidateRect(combo, nullptr, FALSE);
                scheduleSettingsSync(g_app->settings);
            }
            animateLanguageDropdown(g_app->settings, 0.0f);
            return 0;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            paintLanguageDropdown(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (message == WM_DESTROY) {
            g_app->languageDropdown = nullptr;
            g_app->languageDropdownHover = -1;
            return 0;
        }
    }
    if (message == WM_DESTROY && hwnd == g_app->popup) {
        cancelPopupSearch();
        clearPopupImagePreviews();
        if (g_app->popupFont) DeleteObject(g_app->popupFont);
        if (g_app->popupInputBrush) DeleteObject(g_app->popupInputBrush);
        releasePaintFonts(g_app->popupTitleFont, g_app->popupFilterFont,
                          g_app->popupPreviewFont, g_app->popupMetaFont);
        g_app->popupFont = nullptr;
        g_app->popupInputBrush = nullptr;
        KillTimer(hwnd, kPopupOpenGuardTimer);
        KillTimer(hwnd, kPopupDeactivateTimer);
        if (g_app->popupMouseHook) {
            UnhookWindowsHookEx(g_app->popupMouseHook);
            g_app->popupMouseHook = nullptr;
        }
        g_app->popup = nullptr;
        g_app->searchEdit = nullptr;
        g_app->popupSearchInputActive = false;
        g_app->popupSearchControlDown = false;
        g_app->popupSuppressImeTriggerSpace = false;
        g_app->popupImeMode = false;
        g_app->popupOpening = false;
        g_app->popupActivated = false;
        g_app->popupOpenedByWinV = false;
        g_app->popupOpenInputTick = 0;
    }
    if (message == WM_DESTROY && hwnd == g_app->settings) {
        closeSupportProcess();
        if (g_app->languageDropdown) {
            DestroyWindow(g_app->languageDropdown);
            g_app->languageDropdown = nullptr;
        }
        if (g_app->settingsFont) DeleteObject(g_app->settingsFont);
        if (g_app->settingsMultilineFont) DeleteObject(g_app->settingsMultilineFont);
        if (g_app->settingsBackgroundBrush) DeleteObject(g_app->settingsBackgroundBrush);
        if (g_app->settingsCardBrush) DeleteObject(g_app->settingsCardBrush);
        if (g_app->settingsInputBrush) DeleteObject(g_app->settingsInputBrush);
        releasePaintFonts(g_app->settingsNavFont, g_app->settingsTitleFont,
                          g_app->settingsCardTitleFont, g_app->settingsBodyFont);
        g_app->settingsFont = nullptr;
        g_app->settingsMultilineFont = nullptr;
        g_app->settingsBackgroundBrush = nullptr;
        g_app->settingsCardBrush = nullptr;
        g_app->settingsInputBrush = nullptr;
        KillTimer(hwnd, kSettingsToggleTimer);
        KillTimer(hwnd, kSettingsDropdownTimer);
        KillTimer(hwnd, kSettingsSyncTimer);
        KillTimer(hwnd, kSettingsEncryptionTimer);
        KillTimer(hwnd, kSettingsThemeTimer);
        g_app->toggleAnimationControl = nullptr;
        g_app->settingsThemeAnimating = false;
        g_app->shortcutCaptureControl = nullptr;
        g_app->languageDropdown = nullptr;
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
        if (message == kClosePopupMessage) {
            if (!g_app->popupPinned) closePopup();
            return 0;
        }
        if (message == WM_HOTKEY) {
            if (wParam == kHotkeySettings) {
                openSettings();
            } else if (wParam == kHotkeyPause) {
                g_app->settingsData.pauseMonitoring = !g_app->settingsData.pauseMonitoring;
                if (g_app->settings) {
                    setSettingsToggleValue(GetDlgItem(g_app->settings, kSettingPause),
                                           g_app->settingsData.pauseMonitoring);
                    InvalidateRect(g_app->settings, nullptr, FALSE);
                }
                saveSettings(g_app->settingsData);
            } else if (wParam == kHotkeyAltV || wParam == kHotkeyWinV) {
                showPopup();
            }
            return 0;
        }
        if (message == kShowPopupMessage) {
            showPopup(wParam != 0);
            return 0;
        }
        if (message == kPopupKeyboardMessage && g_app->popup) {
            PostMessageW(g_app->popup, WM_KEYDOWN, wParam, 0);
            return 0;
        }
        if (message == kPopupEnterImeMessage) {
            std::unique_ptr<PopupImeKeyEvent> event(
                reinterpret_cast<PopupImeKeyEvent*>(wParam));
            if (event && g_app->popup) {
                enterPopupImeMode();
                if (g_app->popupImeMode) {
                    g_app->popupSuppressImeTriggerSpace = event->key.vkCode == VK_SPACE;
                    forwardPopupSearchKey(event->key, event->hookMessage);
                }
            }
            return 0;
        }
        if (message == kRunPopupImageBenchmarkMessage) {
            runPopupImageScrollBenchmark();
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
        if (message == WM_TIMER && wParam == kWinVReleaseTimer) {
            const bool winDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
            const bool vDown = (GetAsyncKeyState('V') & 0x8000) != 0;
            if (winDown || vDown) return 0;
            KillTimer(hwnd, kWinVReleaseTimer);
            if (g_app->pendingWinVPopup) {
                resetWinKeyState(false);
                PostMessageW(hwnd, kShowPopupMessage, 1, 0);
            }
            return 0;
        }
        if (message == WM_TIMER && wParam == kExpiryTimer) {
            if (g_app->store.pruneExpired(nowUnix()) && g_app->popup) {
                refreshVisible(true);
                InvalidateRect(g_app->popup, nullptr, FALSE);
            }
            return 0;
        }
        if (message == WM_CLIPBOARDUPDATE) {
            if (!g_app->settingsData.pauseMonitoring) {
                g_app->clipboardCapturePending = true;
                SetTimer(hwnd, kClipboardCaptureTimer, 35, nullptr);
            }
            return 0;
        }
        if (message == WM_TIMER && wParam == kClipboardCaptureTimer) {
            KillTimer(hwnd, kClipboardCaptureTimer);
            if (g_app->settingsData.pauseMonitoring) return 0;
            if (g_app->clipboardCaptureRunning->load(std::memory_order_acquire)) {
                SetTimer(hwnd, kClipboardCaptureTimer, 35, nullptr);
                return 0;
            }
            startClipboardCapture();
            return 0;
        }
        if (message == kClipboardCaptureCompleteMessage) {
            std::unique_ptr<ClipboardCaptureResult> result(
                reinterpret_cast<ClipboardCaptureResult*>(wParam));
            if (result && result->captured && !g_app->settingsData.pauseMonitoring) {
                const ClipType type = result->type;
                const std::string& payload = result->payload;
                const std::string& source = result->source;
                const std::size_t maxPayload = static_cast<std::size_t>(g_app->settingsData.maxContentMb) * 1024u * 1024u;
                if (payload.size() <= maxPayload && !isIgnoredClipboardSource(source)) {
                    const auto hash = clipboardDedupHash(type, payload);
                    const bool ignoredSelfWrite = GetTickCount64() <= g_app->ignoredClipboardUntil &&
                        (hash == g_app->ignoredClipboardHash ||
                         hash == g_app->ignoredClipboardTextHash);
                    if (!ignoredSelfWrite) {
                        const std::uint64_t expiresAt = g_app->settingsData.sensitiveExpiryHours > 0 &&
                            containsSensitiveMarker(type, payload)
                            ? nowUnix() + static_cast<std::uint64_t>(g_app->settingsData.sensitiveExpiryHours) * 3600ULL
                            : 0;
                        if (!g_app->store.appendOrUpdate(type, payload, hash, source, expiresAt)) {
                            appendDiagnosticLog("ERROR", "clipboard: unable to append history record");
                        }
                    } else {
                        g_app->ignoredClipboardHash = 0;
                        g_app->ignoredClipboardTextHash = 0;
                        g_app->ignoredClipboardUntil = 0;
                    }
                }
            }
            if (g_app->popup) refreshVisible(true);
            if (g_app->clipboardCapturePending && !g_app->settingsData.pauseMonitoring) {
                SetTimer(hwnd, kClipboardCaptureTimer, 35, nullptr);
            }
            return 0;
        }
    }

    if (hwnd == g_app->settings) {
        if (message == WM_SIZE) {
            g_app->settingsScrollOffset = std::clamp(g_app->settingsScrollOffset,
                                                      0, settingsScrollMax(hwnd));
            updateSettingsTabControls(hwnd);
            invalidatePopupList(hwnd);
            return 0;
        }
        if (message == WM_TIMER && wParam == kSettingsDropdownTimer) {
            if (!g_app->languageDropdown) {
                KillTimer(hwnd, kSettingsDropdownTimer);
                return 0;
            }
            if (settingsToggleClock() - g_app->languageDropdownStartTicks >=
                settingsToggleClockFrequency() * kSettingsDropdownAnimationMs / 1000LL) {
                const float target = g_app->languageDropdownTo;
                if (target <= 0.0f) {
                    DestroyWindow(g_app->languageDropdown);
                } else {
                    positionLanguageDropdown(hwnd, 1.0f);
                }
                KillTimer(hwnd, kSettingsDropdownTimer);
            } else {
                positionLanguageDropdown(hwnd, languageDropdownProgress());
                InvalidateRect(g_app->languageDropdown, nullptr, FALSE);
            }
            return 0;
        }
        if (message == WM_TIMER && wParam == kSettingsSyncTimer) {
            KillTimer(hwnd, kSettingsSyncTimer);
            if (!g_app->settingsClosing) syncSettingsFromControls(hwnd, false);
            return 0;
        }
        if (message == WM_TIMER && wParam == kSettingsEncryptionTimer) {
            KillTimer(hwnd, kSettingsEncryptionTimer);
            if (!g_app->settingsClosing) syncSettingsFromControls(hwnd, true);
            return 0;
        }
        if (message == WM_TIMER && wParam == kSettingsActionFeedbackTimer) {
            KillTimer(hwnd, kSettingsActionFeedbackTimer);
            g_app->settingsActionFeedback.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (message == WM_TIMER && wParam == kSettingsToggleTimer) {
            HWND toggle = g_app->toggleAnimationControl;
            if (!toggle || settingsToggleAnimationFinished()) {
                if (toggle) InvalidateRect(toggle, nullptr, FALSE);
                g_app->toggleAnimationControl = nullptr;
                KillTimer(hwnd, kSettingsToggleTimer);
            } else {
                InvalidateRect(toggle, nullptr, FALSE);
            }
            return 0;
        }
        if (message == WM_MOUSEMOVE) {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const int themeMode = settingsThemeModeAtPoint(hwnd, x, y);
            if (themeMode != g_app->hoveredSettingsThemeMode) {
                g_app->hoveredSettingsThemeMode = themeMode;
                RECT client{};
                GetClientRect(hwnd, &client);
                RECT themeRect{ui(0), ui(10), client.right, ui(48)};
                InvalidateRect(hwnd, &themeRect, FALSE);
            }
            const bool interactive = settingsThemeModeAtPoint(hwnd, x, y) >= 0 ||
                settingsAccentAtPoint(hwnd, x, y) >= 0 ||
                (x >= ui(8) && x < ui(180) && y >= ui(50) && y < ui(50 + 5 * 38));
            SetCursor(LoadCursorW(nullptr, interactive ? IDC_HAND : IDC_ARROW));
            const int tab = x >= ui(8) && x < ui(180) && y >= ui(50) && y < ui(50 + 5 * 38)
                ? std::clamp((y - ui(50)) / ui(38), 0, 4) : -1;
            if (tab != g_app->hoveredSettingsTab) {
                const int previousTab = g_app->hoveredSettingsTab;
                g_app->hoveredSettingsTab = tab;
                invalidateSettingsNav(hwnd, previousTab);
                invalidateSettingsNav(hwnd, tab);
            }
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            return 0;
        }
        if (message == WM_TIMER && wParam == kSettingsThemeTimer) {
            const bool finished = advanceSettingsThemeAnimation();
            if (finished) {
                g_app->settingsThemeAnimating = false;
                g_app->settingsThemeProgressValue = 1.0f;
                KillTimer(hwnd, kSettingsThemeTimer);
                refreshSettingsFrame(hwnd);
            }
            refreshSettingsBrushes();
            refreshPopupBrush();
            invalidateThemeWindow(hwnd);
            invalidateThemeWindow(g_app->popup);
            invalidateThemeWindow(g_app->languageDropdown);
            return 0;
        }
        if (message == WM_MOUSEWHEEL) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &point);
            if ((g_app->settingsTab == 0 ||
                 g_app->settingsTab == kSettingsShortcutPage ||
                 g_app->settingsTab == 2) &&
                point.x >= ui(kSettingsSidebarWidth) &&
                point.y >= ui(kSettingsHeaderHeight)) {
                const int direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -48 : 48;
                const int next = std::clamp(g_app->settingsScrollOffset + direction,
                                            0, settingsScrollMax(hwnd));
                if (next != g_app->settingsScrollOffset) {
                    g_app->settingsScrollOffset = next;
                    updateSettingsTabControls(hwnd);
                    RedrawWindow(hwnd, nullptr, nullptr,
                                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_NOERASE);
                }
                return 0;
            }
        }
        if (message == WM_MOUSELEAVE) {
            if (g_app->hoveredSettingsThemeMode != -1) {
                g_app->hoveredSettingsThemeMode = -1;
                RECT client{};
                GetClientRect(hwnd, &client);
                RECT themeRect{ui(0), ui(10), client.right, ui(48)};
                InvalidateRect(hwnd, &themeRect, FALSE);
            }
            if (g_app->hoveredSettingsTab != -1) {
                const int previousTab = g_app->hoveredSettingsTab;
                g_app->hoveredSettingsTab = -1;
                invalidateSettingsNav(hwnd, previousTab);
            }
            return 0;
        }
        if (message == WM_SETCURSOR) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            if (settingsThemeModeAtPoint(hwnd, point.x, point.y) >= 0 ||
                settingsAccentAtPoint(hwnd, point.x, point.y) >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (point.x >= ui(8) && point.x < ui(180) &&
                point.y >= ui(50) && point.y < ui(50 + 5 * 38)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (message == WM_LBUTTONDOWN) {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const int themeMode = settingsThemeModeAtPoint(hwnd, x, y);
            if (themeMode >= 0) {
                setSettingsThemeMode(hwnd, themeMode);
                return 0;
            }
            const int accent = settingsAccentAtPoint(hwnd, x, y);
            if (accent >= 0) {
                setSettingsAccent(hwnd, accent);
                return 0;
            }
            SetFocus(hwnd);
            if (g_app->languageDropdown) animateLanguageDropdown(hwnd, 0.0f);
            const int toggleId = settingsToggleAtPoint(g_app->settingsTab, x, y);
            if (toggleId != 0) {
                HWND toggle = GetDlgItem(hwnd, toggleId);
                animateSettingsToggle(toggle);
                if (toggleId == kSettingEncrypt) scheduleSettingsEncryptionSync(hwnd);
                else syncSettingsFromControls(hwnd, false);
                return 0;
            }
            if (x >= ui(8) && x < ui(kSettingsSidebarWidth) &&
                y >= ui(50) && y < ui(50 + 5 * 38)) {
                g_app->settingsTab = std::clamp((y - ui(50)) / ui(38), 0, 4);
                g_app->settingsScrollOffset = 0;
                updateSettingsTabControls(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        if (message == WM_COMMAND) {
            if (HIWORD(wParam) == EN_CHANGE &&
                (LOWORD(wParam) == kSettingMaxItems ||
                 LOWORD(wParam) == kSettingRetentionDays ||
                 LOWORD(wParam) == kSettingMaxDiskMb ||
                 LOWORD(wParam) == kSettingMaxContentMb ||
                   LOWORD(wParam) == kSettingDataDirectory ||
                   LOWORD(wParam) == kSettingIgnoredApps ||
                   LOWORD(wParam) == kSettingSensitiveExpiry ||
                    (LOWORD(wParam) >= kSettingCategoryMaxBase &&
                     LOWORD(wParam) < kSettingCategoryDiskBase + kStorageCategoryCount))) {
                if (g_app->restoringSettingsControls) return 0;
                InvalidateRect(reinterpret_cast<HWND>(lParam), nullptr, FALSE);
                long minimum = 0;
                long maximum = 0;
                if (!settingsNumericEditRange(LOWORD(wParam), minimum, maximum)) {
                    scheduleSettingsSync(hwnd);
                }
                return 0;
            }
            if (LOWORD(wParam) == kSettingLanguage &&
                (HIWORD(wParam) == CBN_DROPDOWN || HIWORD(wParam) == CBN_SELENDOK ||
                 HIWORD(wParam) == CBN_SELENDCANCEL)) {
                HWND language = GetDlgItem(hwnd, kSettingLanguage);
                SendMessageW(language, CB_SHOWDROPDOWN, FALSE, 0);
                if (HIWORD(wParam) == CBN_DROPDOWN && !g_app->languageDropdown) {
                    toggleLanguageDropdown(hwnd);
                }
                if (HIWORD(wParam) == CBN_SELENDOK) scheduleSettingsSync(hwnd);
                return 0;
            }
            if (isSettingsToggle(LOWORD(wParam)) &&
                (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == BN_DOUBLECLICKED)) {
                HWND toggle = GetDlgItem(hwnd, LOWORD(wParam));
                animateSettingsToggle(toggle);
                if (LOWORD(wParam) == kSettingEncrypt) scheduleSettingsEncryptionSync(hwnd);
                else syncSettingsFromControls(hwnd, false);
                return 0;
            }
            const int actionId = LOWORD(wParam);
            if (actionId == kSettingOpenLog && HIWORD(wParam) == BN_CLICKED) {
                openDiagnosticLog(hwnd);
                return 0;
            }
            if (actionId == kSettingSupportAuthor) {
                openSupportWindow(false);
                return 0;
            }
            if (actionId == kSettingJoinQqGroup) {
                openSupportWindow(true);
                return 0;
            }
            if (actionId == kSettingBrowseDataDirectory && HIWORD(wParam) == BN_CLICKED) {
                browseDataDirectory(hwnd);
                return 0;
            }
            if (isSettingsClearAction(actionId)) {
                const std::size_t count = settingsClearActionCount(actionId);
                if (!confirmSettingsClear(hwnd, actionId, count)) {
                    setSettingsActionFeedback(hwnd, settingsLocale().clearCancelled, true);
                    return 0;
                }
                if (clearSettingsAction(actionId)) {
                    wchar_t feedback[160]{};
                    if (count == 0) {
                        swprintf_s(feedback, settingsLocale().noRecordsFormat,
                                   settingsClearActionLabel(actionId));
                    } else {
                        swprintf_s(feedback, settingsLocale().clearedRecordsFormat,
                                   count, settingsClearActionLabel(actionId));
                    }
                    setSettingsActionFeedback(hwnd, feedback, true);
                } else {
                    appendDiagnosticLog("ERROR", "settings: history clear operation failed");
                    setSettingsActionFeedback(hwnd, settingsLocale().clearFailed, false);
                }
                if (g_app->popup) refreshVisible(true);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (message == WM_CLOSE) {
            g_app->settingsClosing = true;
            KillTimer(hwnd, kSettingsSyncTimer);
            KillTimer(hwnd, kSettingsEncryptionTimer);
            KillTimer(hwnd, kSettingsActionFeedbackTimer);
            g_app->settingsActionFeedback.clear();
            syncSettingsFromControls(hwnd, true);
            closeSupportProcess();
            DestroyWindow(hwnd);
            g_app->settings = nullptr;
            g_app->settingsClosing = false;
            return 0;
        }
    }

    if (hwnd == g_app->popup) {
        if (message == WM_MOUSEACTIVATE) {
            return g_app->popupImeMode ? MA_ACTIVATE : MA_NOACTIVATE;
        }
        if (message == WM_SETCURSOR) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            RECT client{};
            GetClientRect(hwnd, &client);
            if (g_app->filterDragging) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            if (g_app->scrollDragging || popupScrollThumbAt(point.x, point.y)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                return TRUE;
            }
            const int row = popupRowAt(point.y);
            const int rowTop = row >= 0 ? popupRowTop(row) : -1;
            const bool deleteHover = row >= 0 && point.x >= client.right - ui(30) &&
                point.x < client.right - ui(8) && point.y >= rowTop + ui(5) &&
                point.y < rowTop + ui(30);
            const bool pinHover = row >= 0 && point.x >= client.right - ui(45) &&
                point.x < client.right - ui(10) && point.y >= rowTop + ui(kPopupCardHeight - 30) &&
                point.y < rowTop + ui(kPopupCardHeight);
            if (point.x >= ui(kPopupSearchLeft) && point.x < ui(kPopupSearchRight) &&
                point.y >= ui(12) && point.y < ui(48)) {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                return TRUE;
            }
            if (point.x >= ui(kPopupClearLeft) && point.x < ui(kPopupClearRight) &&
                point.y >= ui(12) && point.y < ui(46)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (point.x >= ui(kPopupPinLeft) && point.x < ui(kPopupPinRight) &&
                point.y >= ui(12) && point.y < ui(46)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (point.x >= ui(kPopupCloseLeft) && point.x < client.right - ui(16) &&
                point.y >= ui(12) && point.y < ui(46)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (popupPointInHeader(client, point.x, point.y)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                return TRUE;
            }
            if (popupFilterAt(point.x, point.y) >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (point.y >= ui(kPopupFilterTop) && point.y < ui(kPopupFilterBottom)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            if (deleteHover || pinHover || (row >= 0 &&
                point.x >= ui(16) && point.x < client.right - ui(16))) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        if (message == WM_ACTIVATEAPP && !wParam) {
            if (g_app->popupPinned || g_app->popupOpenedByWinV) {
                const HWND target = GetForegroundWindow();
                if (IsWindow(target) && target != hwnd) rememberPasteTarget(target, false);
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            if (g_app->popupOpening || !g_app->popupActivated) {
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            if (g_app->filterDragging || g_app->scrollDragging) return 0;
            SetTimer(hwnd, kPopupDeactivateTimer, kPopupDeactivateDelayMs, nullptr);
            return 0;
        }
        if (message == WM_ACTIVATEAPP && wParam) {
            KillTimer(hwnd, kPopupDeactivateTimer);
            if (!g_app->popupOpening && GetForegroundWindow() == hwnd) {
                g_app->popupActivated = true;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        if (message == WM_TIMER && wParam == kPopupDeactivateTimer) {
            KillTimer(hwnd, kPopupDeactivateTimer);
            const HWND foreground = GetForegroundWindow();
            if (g_app->popup != hwnd || g_app->popupOpening ||
                g_app->popupPinned || g_app->popupOpenedByWinV ||
                g_app->filterDragging || g_app->scrollDragging ||
                foreground == hwnd) {
                return 0;
            }
            if (popupSearchHasFocus()) {
                SetTimer(hwnd, kPopupDeactivateTimer, kPopupDeactivateDelayMs, nullptr);
                return 0;
            }
            if (!g_app->popupOpenedByWinV && !g_app->popupActivated) return 0;
            if (g_app->popupOpenedByWinV && g_app->popupOpenInputTick != 0 &&
                lastInputTick() == g_app->popupOpenInputTick &&
                (!IsWindow(g_app->targetWindow) || foreground == g_app->targetWindow)) {
                SetTimer(hwnd, kPopupDeactivateTimer, kPopupDeactivateDelayMs, nullptr);
                return 0;
            }
            closePopup();
            return 0;
        }
        if (message == WM_TIMER && wParam == kPopupOpenGuardTimer) {
            KillTimer(hwnd, kPopupOpenGuardTimer);
            if (g_app->popup == hwnd) {
                g_app->popupOpening = false;
                g_app->popupActivated = GetForegroundWindow() == hwnd;
                if (!g_app->popupActivated && !g_app->popupOpenedByWinV) {
                    SetTimer(hwnd, kPopupDeactivateTimer, kPopupDeactivateDelayMs, nullptr);
                }
            }
            return 0;
        }
        if (message == WM_TIMER && wParam == kPopupSearchTimer) {
            KillTimer(hwnd, kPopupSearchTimer);
            startPopupSearch(hwnd);
            return 0;
        }
        if (message == WM_NCACTIVATE && !wParam) {
            if (g_app->popupPinned || g_app->popupOpenedByWinV) {
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            if (g_app->popupOpening || !g_app->popupActivated) {
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            if (g_app->filterDragging || g_app->scrollDragging) return 0;
            SetTimer(hwnd, kPopupDeactivateTimer, kPopupDeactivateDelayMs, nullptr);
            return 0;
        }
        if (message == WM_NCACTIVATE && wParam) {
            KillTimer(hwnd, kPopupDeactivateTimer);
            if (!g_app->popupOpening && GetForegroundWindow() == hwnd) {
                g_app->popupActivated = true;
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        if (message == WM_COMMAND && LOWORD(wParam) == kSearchEdit &&
            HIWORD(wParam) == EN_CHANGE) {
            wchar_t value[512]{};
            GetWindowTextW(g_app->searchEdit, value, 512);
            g_app->query = wideToUtf8(value, std::wcslen(value));
            cancelPopupSearch();
            KillTimer(hwnd, kPopupSearchTimer);
            SetTimer(hwnd, kPopupSearchTimer, kPopupSearchDelayMs, nullptr);
            return 0;
        }
        if (message == WM_KEYDOWN) {
            if (shortcutMatches(g_app->settingsData.popupCloseHotkey, static_cast<UINT>(wParam))) {
                closePopup();
            }
            else if (shortcutMatches(g_app->settingsData.popupPlainPasteHotkey,
                                     static_cast<UINT>(wParam))) {
                sendPaste(PasteMode::PlainText);
            }
            else if (shortcutMatches(g_app->settingsData.popupRichPasteHotkey,
                                     static_cast<UINT>(wParam))) {
                sendPaste(PasteMode::RichText);
            }
            else if (shortcutMatches(g_app->settingsData.popupClearFilterHotkey,
                                     static_cast<UINT>(wParam))) {
                applyFilterCommand(kFilterAll);
                refreshVisible();
            }
            else if (wParam == VK_UP) { --g_app->selected; refreshVisible(); }
            else if (wParam == VK_DOWN) { ++g_app->selected; refreshVisible(); }
            else if (wParam == VK_PRIOR) scrollPopup(-ui(96));
            else if (wParam == VK_NEXT) scrollPopup(ui(96));
            else if (wParam == VK_HOME) {
                g_app->selected = 0;
                refreshVisible();
            } else if (wParam == VK_END) {
                g_app->selected = static_cast<int>(g_app->visible.size()) - 1;
                refreshVisible();
            } else if (wParam == VK_TAB) {
                ++g_app->selected;
                refreshVisible();
            }
            else if (shortcutMatches(g_app->settingsData.popupPasteHotkey,
                                     static_cast<UINT>(wParam))) sendPaste();
            else if (shortcutMatches(g_app->settingsData.popupDeleteHotkey,
                                     static_cast<UINT>(wParam)) && !g_app->visible.empty()) {
                g_app->store.remove(g_app->visible[static_cast<std::size_t>(g_app->selected)]);
                refreshVisible();
            } else if (shortcutMatches(g_app->settingsData.popupSettingsHotkey,
                                       static_cast<UINT>(wParam))) openSettings();
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
            const int direction = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -ui(96) : ui(96);
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
        if (message == WM_MOUSEMOVE && g_app->scrollDragging) {
            int trackTop = 0;
            int trackBottom = 0;
            int thumbTop = 0;
            int thumbHeight = 0;
            int maxOffset = 0;
            if (popupScrollMetrics(trackTop, trackBottom, thumbTop, thumbHeight, maxOffset)) {
                const int trackTravel = std::max(1, trackBottom - trackTop - thumbHeight);
                const int delta = GET_Y_LPARAM(lParam) - g_app->scrollDragStartY;
                setPopupScrollPosition(g_app->scrollDragStartOffset + delta * maxOffset / trackTravel);
                invalidatePopupList(hwnd);
            }
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
            RECT client{};
            GetClientRect(hwnd, &client);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            const int filter = popupFilterAt(x, y);
            const int row = (filter < 0 && x >= ui(16) && x < client.right - ui(16))
                ? popupRowAt(y) : -1;
            const int rowTop = row >= 0 ? popupRowTop(row) : -1;
            const int deleteRow = row >= 0 && x >= client.right - ui(30) &&
                x < client.right - ui(8) && y >= rowTop + ui(5) && y < rowTop + ui(30)
                ? row : -1;
            const int pinRow = row >= 0 && x >= client.right - ui(45) &&
                x < client.right - ui(10) && y >= rowTop + ui(kPopupCardHeight - 30) &&
                y < rowTop + ui(kPopupCardHeight) ? row : -1;
            const bool headerHover = popupPointInHeader(client, x, y);
            int headerButton = -1;
            if (x >= ui(kPopupClearLeft) && x < ui(kPopupClearRight) &&
                y >= ui(12) && y < ui(46)) {
                headerButton = 7;
            } else if (x >= ui(kPopupPinLeft) && x < ui(kPopupPinRight) &&
                       y >= ui(12) && y < ui(46)) {
                headerButton = 8;
            } else if (x >= ui(kPopupCloseLeft) && x < client.right - ui(16) &&
                       y >= ui(12) && y < ui(46)) {
                headerButton = 9;
            }
            const int nextFilter = filter >= 0 ? filter : headerButton;
            if (row != g_app->hoveredRow || nextFilter != g_app->hoveredFilter ||
                deleteRow != g_app->hoveredDeleteRow || pinRow != g_app->hoveredPinRow ||
                headerHover != g_app->hoveredHeader) {
                const int previousRow = g_app->hoveredRow;
                const int previousFilter = g_app->hoveredFilter;
                const bool previousHeader = g_app->hoveredHeader;
                g_app->hoveredRow = row;
                g_app->hoveredFilter = nextFilter;
                g_app->hoveredDeleteRow = deleteRow;
                g_app->hoveredPinRow = pinRow;
                g_app->hoveredHeader = headerHover;
                invalidatePopupHover(hwnd, previousRow, previousFilter, previousHeader);
                invalidatePopupHover(hwnd, row, nextFilter, headerHover);
            }
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            if (g_app->hoveredRow != -1 || g_app->hoveredFilter != -1 ||
                g_app->hoveredDeleteRow != -1 || g_app->hoveredPinRow != -1 || g_app->hoveredHeader) {
                const int previousRow = g_app->hoveredRow;
                const int previousFilter = g_app->hoveredFilter;
                const bool previousHeader = g_app->hoveredHeader;
                g_app->hoveredRow = -1;
                g_app->hoveredFilter = -1;
                g_app->hoveredDeleteRow = -1;
                g_app->hoveredPinRow = -1;
                g_app->hoveredHeader = false;
                invalidatePopupHover(hwnd, previousRow, previousFilter, previousHeader);
            }
            return 0;
        }
        if (message == WM_LBUTTONUP && g_app->scrollDragging) {
            g_app->scrollDragging = false;
            ReleaseCapture();
            return 0;
        }
        if (message == WM_LBUTTONUP && g_app->filterDragging) {
            const int clickX = GET_X_LPARAM(lParam);
            const int clickY = GET_Y_LPARAM(lParam);
            const bool isClick = std::abs(clickX - g_app->filterDragStartX) < ui(4);
            g_app->filterDragging = false;
            ReleaseCapture();
            if (isClick && clickY >= ui(kPopupFilterTop) && clickY < ui(kPopupFilterBottom)) {
                for (int slot = 0; slot < 6; ++slot) {
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
        if (message == WM_LBUTTONUP && g_app->pressedRow >= 0) {
            const int pressedRow = g_app->pressedRow;
            g_app->pressedRow = -1;
            const int row = popupRowAt(GET_Y_LPARAM(lParam));
            if (row == pressedRow && row < static_cast<int>(g_app->visible.size())) {
                g_app->selected = row;
                sendPaste();
            }
            return 0;
        }
        if (message == WM_LBUTTONDOWN) {
            g_app->pressedRow = -1;
            RECT client{};
            GetClientRect(hwnd, &client);
            const int clickX = GET_X_LPARAM(lParam);
            const int clickY = GET_Y_LPARAM(lParam);
            const bool inSearch = clickX >= ui(kPopupSearchLeft) && clickX < ui(kPopupSearchRight) &&
                clickY >= ui(12) && clickY < ui(48);
            if (!inSearch) SetFocus(hwnd);
            if (GET_Y_LPARAM(lParam) >= ui(12) && GET_Y_LPARAM(lParam) < ui(46) &&
                GET_X_LPARAM(lParam) >= ui(kPopupClearLeft) &&
                GET_X_LPARAM(lParam) < ui(kPopupClearRight)) {
                SetWindowTextW(g_app->searchEdit, L"");
                g_app->query.clear();
                SetFocus(g_app->searchEdit);
                refreshVisible();
                return 0;
            }
            if (GET_Y_LPARAM(lParam) >= ui(12) && GET_Y_LPARAM(lParam) < ui(46) &&
                GET_X_LPARAM(lParam) >= ui(kPopupPinLeft) &&
                GET_X_LPARAM(lParam) < ui(kPopupPinRight)) {
                setPopupPinned(!g_app->popupPinned);
                return 0;
            }
            if (GET_Y_LPARAM(lParam) >= ui(12) && GET_Y_LPARAM(lParam) < ui(46) &&
                GET_X_LPARAM(lParam) >= ui(kPopupCloseLeft) &&
                GET_X_LPARAM(lParam) < client.right - ui(16)) {
                closePopup();
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
            if (popupScrollThumbAt(clickX, clickY)) {
                g_app->scrollDragging = true;
                g_app->scrollDragStartY = clickY;
                g_app->scrollDragStartOffset = g_app->scrollPosition;
                g_app->hoveredRow = -1;
                g_app->hoveredDeleteRow = -1;
                g_app->hoveredPinRow = -1;
                SetCapture(hwnd);
                invalidatePopupList(hwnd);
                return 0;
            }
            const int row = popupRowAt(clickY);
            if (row >= 0 && row < static_cast<int>(g_app->visible.size())) {
                const int rowTop = popupRowTop(row);
                const bool deleteClick = GET_X_LPARAM(lParam) >= client.right - ui(30) &&
                    GET_X_LPARAM(lParam) < client.right - ui(8) &&
                    GET_Y_LPARAM(lParam) >= rowTop + ui(5) &&
                    GET_Y_LPARAM(lParam) < rowTop + ui(30);
                const bool pinClick = GET_X_LPARAM(lParam) >= client.right - ui(45) &&
                    GET_X_LPARAM(lParam) < client.right - ui(10) &&
                    GET_Y_LPARAM(lParam) >= rowTop + ui(kPopupCardHeight - 30) &&
                    GET_Y_LPARAM(lParam) < rowTop + ui(kPopupCardHeight);
                if (deleteClick) {
                    g_app->store.remove(g_app->visible[static_cast<std::size_t>(row)]);
                    refreshVisible();
                    return 0;
                }
                if (pinClick) {
                    if (g_app->store.togglePinned(g_app->visible[static_cast<std::size_t>(row)])) {
                        if (g_app->pinnedOnly) refreshVisible();
                        else invalidatePopupHover(hwnd, row, -1, false);
                    }
                    return 0;
                }
                g_app->selected = row;
                g_app->pressedRow = row;
                invalidatePopupList(hwnd);
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
                const ClipItem& item = g_app->store.items()[g_app->visible[static_cast<std::size_t>(row)]];
                appendPasteMenu(menu, item);
                appendPopupPinMenu(menu);
                AppendMenuW(menu, MF_STRING, kMenuDelete, settingsLocale().popupDelete);
                appendFilterMenu(menu);
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd, &point);
                const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                   point.x, point.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                const std::size_t index = g_app->visible[static_cast<std::size_t>(g_app->selected)];
                if (command == kMenuPopupPinned) setPopupPinned(!g_app->popupPinned);
                else if (command == kMenuPaste) sendPaste();
                else if (command == kMenuPastePlain) sendPaste(PasteMode::PlainText);
                else if (command == kMenuPasteRich) sendPaste(PasteMode::RichText);
                else if (command == kMenuDelete) g_app->store.remove(index);
                else if (command >= kFilterAll && command <= kFilterOther) applyFilterCommand(command);
                refreshVisible();
            } else {
                HMENU menu = CreatePopupMenu();
                appendPopupPinMenu(menu);
                appendFilterMenu(menu);
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd, &point);
                const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                                   point.x, point.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (command == kMenuPopupPinned) {
                    setPopupPinned(!g_app->popupPinned);
                } else {
                    applyFilterCommand(command);
                    refreshVisible();
                }
            }
            return 0;
        }
        if (message == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            paintPopup(hwnd, dc, &ps.rcPaint);
            EndPaint(hwnd, &ps);
            g_app->fastImagePreview = false;
            return 0;
        }
        if (message == WM_ACTIVATE &&
            (LOWORD(wParam) == WA_ACTIVE || LOWORD(wParam) == WA_CLICKACTIVE)) {
            KillTimer(hwnd, kPopupDeactivateTimer);
            if (!g_app->popupOpening && GetForegroundWindow() == hwnd) {
                g_app->popupActivated = true;
            }
        }
        if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
            if (g_app->popupPinned || g_app->popupOpenedByWinV) {
                const HWND target = reinterpret_cast<HWND>(lParam);
                if (IsWindow(target) && target != hwnd) rememberPasteTarget(target, false);
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            if (g_app->popupOpening || !g_app->popupActivated) {
                return DefWindowProcW(hwnd, message, wParam, lParam);
            }
            if (g_app->filterDragging) return 0;
            SetTimer(hwnd, kPopupDeactivateTimer, kPopupDeactivateDelayMs, nullptr);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void openSettings() {
    if (g_app->settings) {
        SetWindowPos(g_app->settings, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetForegroundWindow(g_app->settings);
        SetActiveWindow(g_app->settings);
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    g_uiDpi = monitorDpi(monitor);
    g_app->hoveredSettingsTab = -1;
    g_app->hoveredSettingsThemeMode = -1;
    g_app->hoveredSettingsControl = 0;
    g_app->settingsScrollOffset = 0;
    g_app->toggleAnimationControl = nullptr;
    g_app->settingsThemeAnimating = false;
    const int width = ui(kSettingsWidth);
    const int height = std::min(ui(kSettingsHeight),
                                static_cast<int>(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - ui(40)));
    const int x = monitorInfo.rcWork.left +
        (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    const int y = monitorInfo.rcWork.top +
        (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    g_app->settings = CreateWindowExW(WS_EX_APPWINDOW, L"ClipLiteSettings",
                                      settingsLocale().windowTitle,
                                       WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                           WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                                      x, y, width, height,
                                       nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (g_app->settings) {
        refreshSettingsFrame(g_app->settings);
        SendMessageW(g_app->settings, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(clipLiteIcon()));
        SendMessageW(g_app->settings, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(clipLiteIcon()));
    }
    ShowWindow(g_app->settings, SW_SHOW);
    UpdateWindow(g_app->settings);
    SetWindowPos(g_app->settings, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(g_app->settings);
    SetActiveWindow(g_app->settings);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ScopedComInitialization comInitialization;
    AppState app;
    g_app = &app;
    const bool commandExit = wcsstr(commandLine, L"--exit") || wcsstr(commandLine, L"/exit");
    const bool commandHistory = wcsstr(commandLine, L"--history") || wcsstr(commandLine, L"/history");
    const bool commandSettings = wcsstr(commandLine, L"--settings") || wcsstr(commandLine, L"/settings");
    const bool commandImageBenchmark = wcsstr(commandLine, L"--benchmark-image-scroll") != nullptr;
    const bool commandElevatedRestart = wcsstr(commandLine, L"--elevated-restart") != nullptr;
    const bool commandSupportPayment = wcsstr(commandLine, L"--support-payment") != nullptr;
    const bool commandSupportQq = wcsstr(commandLine, L"--support-qq") != nullptr;
    if (commandSupportPayment || commandSupportQq) {
        loadSettings(app.settingsData);
        const wchar_t* ownerArgument = wcsstr(commandLine, L"--support-owner=");
        if (!ownerArgument) return 1;
        ownerArgument += std::wcslen(L"--support-owner=");
        const auto ownerValue = static_cast<ULONG_PTR>(_wcstoui64(ownerArgument, nullptr, 10));
        return runSupportWindowProcess(instance, commandSupportQq,
                                       reinterpret_cast<HWND>(ownerValue));
    }
    BenchmarkDirectoryGuard benchmarkDirectory;
    if (commandImageBenchmark) {
        if (!createImageBenchmarkDirectory(benchmarkDirectory)) return 1;
        g_diagnosticLogPath = benchmarkDirectory.path + L".log";
    }
    appendDiagnosticLog("INFO", "startup: process initialization started");
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (commandImageBenchmark) {
        app.settingsData.maxItems = 0;
        app.settingsData.maxDiskMb = 0;
        app.settingsData.maxContentMb = 32;
        app.settingsData.pauseMonitoring = true;
        app.settingsData.showSettingsOnStartup = false;
        app.settingsData.showStartupNotification = false;
    } else {
        loadSettings(app.settingsData);
    }
    app.popupPinned = app.settingsData.historyWindowPinned;
    appendDiagnosticLog("INFO", "startup: settings loaded");
    app.store.setMaxItems(static_cast<std::size_t>(app.settingsData.maxItems));
    app.store.setSortByLastUsed(app.settingsData.promotePastedItem);
    app.store.setEncryption(app.settingsData.encryptData);
    app.store.setMaxPayloadBytes(static_cast<std::uint32_t>(app.settingsData.maxContentMb) * 1024u * 1024u);
    if (commandImageBenchmark) {
        if (!app.store.setDataDirectory(benchmarkDirectory.path)) return 1;
    } else if (!app.settingsData.dataDirectory.empty() &&
        !app.store.setDataDirectory(utf8ToWide(app.settingsData.dataDirectory))) {
        app.settingsData.dataDirectory.clear();
    }
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"ClipLite.SingleInstance");
    DWORD mutexError = GetLastError();
    if (!mutex) {
        appendDiagnosticLog("ERROR", "startup: single-instance lock creation failed", mutexError);
        showStartupFailure(settingsLocale().unableCreateMutex);
        return 1;
    }
    if (mutexError == ERROR_ALREADY_EXISTS && commandElevatedRestart) {
        CloseHandle(mutex);
        mutex = nullptr;
        for (int attempt = 0; attempt < 50; ++attempt) {
            Sleep(100);
            mutex = CreateMutexW(nullptr, TRUE, L"ClipLite.SingleInstance");
            if (!mutex) break;
            mutexError = GetLastError();
            if (mutexError != ERROR_ALREADY_EXISTS) break;
            CloseHandle(mutex);
            mutex = nullptr;
        }
        if (!mutex || mutexError == ERROR_ALREADY_EXISTS) {
            appendDiagnosticLog("ERROR", "admin: elevated restart could not acquire single-instance lock");
            if (mutex) CloseHandle(mutex);
            return 1;
        }
    }
    if (mutexError == ERROR_ALREADY_EXISTS) {
        if (commandImageBenchmark) {
            CloseHandle(mutex);
            return 2;
        }
        appendDiagnosticLog("INFO", "startup: existing instance received launch request");
        HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, L"ClipLiteHidden", nullptr);
        if (existing) {
            if (commandExit) {
                PostMessageW(existing, kExitMessage, 0, 0);
            } else if (commandHistory) {
                PostMessageW(existing, kShowPopupMessage, 0, 0);
            } else if (commandSettings || app.settingsData.showSettingsOnStartup) {
                PostMessageW(existing, kShowSettingsMessage, 0, 0);
            }
        }
        CloseHandle(mutex);
        return 0;
    }
    if (app.settingsData.runAsAdministrator && !processIsElevated() && !commandElevatedRestart) {
        if (launchElevatedRestart(commandHistory ? L"--elevated-restart --history" :
                                   (commandSettings ? L"--elevated-restart --settings" :
                                                       L"--elevated-restart"))) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
            return 0;
        }
        appendDiagnosticLog("WARN", "admin: continuing without administrator rights");
        app.settingsData.runAsAdministrator = false;
        saveSettings(app.settingsData);
    }
    const bool storeOpened = commandImageBenchmark ? populateBenchmarkImages() : app.store.open();
    if (!storeOpened) {
        appendDiagnosticLog("ERROR", "startup: history storage open failed");
        showStartupFailure(settingsLocale().unableOpenStore);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }
    appendDiagnosticLog("INFO", "startup: history storage opened");
    app.store.pruneExpired(nowUnix());
    const std::uint64_t cutoff = app.settingsData.retentionDays > 0
        ? nowUnix() - static_cast<std::uint64_t>(app.settingsData.retentionDays) * 86400ULL
        : 0;
    app.store.prune(app.settingsData.maxItems,
                    static_cast<std::uint64_t>(app.settingsData.maxDiskMb) * 1024ULL * 1024ULL,
                    cutoff);
    const ClipType categoryTypes[] = {ClipType::Text, ClipType::Image, ClipType::Files};
    for (int i = 0; i < kStorageCategoryCount; ++i) {
        const CategoryLimit& limit = app.settingsData.categoryLimits[static_cast<std::size_t>(i)];
        app.store.pruneCategory(categoryTypes[i], static_cast<std::size_t>(limit.maxItems),
                                static_cast<std::uint64_t>(limit.maxDiskMb) * 1024ULL * 1024ULL);
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW popupClass{};
    popupClass.style = 0;
    popupClass.lpfnWndProc = windowProc;
    popupClass.hInstance = instance;
    popupClass.hIcon = clipLiteIcon();
    popupClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    popupClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    popupClass.lpszClassName = L"ClipLitePopup";
    RegisterClassW(&popupClass);
    WNDCLASSW settingsClass = popupClass;
    settingsClass.style = 0;
    settingsClass.lpszClassName = L"ClipLiteSettings";
    RegisterClassW(&settingsClass);
    WNDCLASSW dropdownClass = popupClass;
    dropdownClass.style = CS_HREDRAW | CS_VREDRAW;
    dropdownClass.hbrBackground = nullptr;
    dropdownClass.lpszClassName = L"ClipLiteDropdown";
    RegisterClassW(&dropdownClass);
    WNDCLASSW supportClass = popupClass;
    supportClass.style = CS_HREDRAW | CS_VREDRAW;
    supportClass.lpfnWndProc = supportWindowProc;
    supportClass.hbrBackground = nullptr;
    supportClass.lpszClassName = L"ClipLiteSupport";
    RegisterClassW(&supportClass);
    WNDCLASSW hiddenClass = popupClass;
    hiddenClass.lpszClassName = L"ClipLiteHidden";
    RegisterClassW(&hiddenClass);

    app.hidden = CreateWindowExW(0, L"ClipLiteHidden", L"ClipLite", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, instance, nullptr);
    if (!app.hidden) {
        appendDiagnosticLog("ERROR", "startup: background window creation failed", GetLastError());
        showStartupFailure(settingsLocale().unableCreateBackground);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&app.gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        app.gdiplusToken = 0;
    }
    SetTimer(app.hidden, kExpiryTimer, 60000, nullptr);
    if (!AddClipboardFormatListener(app.hidden)) {
        appendDiagnosticLog("ERROR", "startup: clipboard listener registration failed", GetLastError());
        showStartupFailure(settingsLocale().unableMonitorClipboard);
        DestroyWindow(app.hidden);
        if (app.gdiplusToken != 0) Gdiplus::GdiplusShutdown(app.gdiplusToken);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }
    appendDiagnosticLog("INFO", "startup: clipboard listener registered");
    registerHotkeys();
    if (!addTrayIcon()) {
        appendDiagnosticLog("WARN", "startup: tray icon registration failed", GetLastError());
    }
    if (app.settingsData.showStartupNotification) showStartupNotification();
    if (commandImageBenchmark) {
        PostMessageW(app.hidden, kRunPopupImageBenchmarkMessage, 0, 0);
    } else if (commandHistory) {
        PostMessageW(app.hidden, kShowPopupMessage, 0, 0);
    } else if (commandSettings || app.settingsData.showSettingsOnStartup) {
        PostMessageW(app.hidden, kShowSettingsMessage, 0, 0);
    }
    appendDiagnosticLog("INFO", "startup: initialization completed");

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    appendDiagnosticLog("INFO", "shutdown: process cleanup started");
    closeSupportProcess();
    RemoveClipboardFormatListener(app.hidden);
    KillTimer(app.hidden, kExpiryTimer);
    removeTrayIcon();
    UnregisterHotKey(app.hidden, kHotkeyAltV);
    UnregisterHotKey(app.hidden, kHotkeyWinV);
    if (app.keyboardHook) UnhookWindowsHookEx(app.keyboardHook);
    if (app.popupMouseHook) UnhookWindowsHookEx(app.popupMouseHook);
    if (app.gdiplusToken != 0) Gdiplus::GdiplusShutdown(app.gdiplusToken);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return app.benchmarkExitCode;
}
