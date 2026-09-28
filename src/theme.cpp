#include "theme.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>

namespace {

constexpr const char* kColorNames[kThemeColorCount] = {
    "windowBackground", "sidebarBackground", "surfaceBackground", "inputBackground",
    "previewBackground", "text", "secondaryText", "disabledText", "border", "divider",
    "hoverBackground", "selectedBackground", "pressedBackground", "accent", "accentSoft",
    "success", "warning", "error", "pin",
};

COLORREF rgb(int red, int green, int blue) {
    return RGB(red, green, blue);
}

ThemePalette lightPalette() {
    ThemePalette palette;
    palette.windowBackground = rgb(240, 244, 248);
    palette.sidebarBackground = rgb(232, 238, 245);
    palette.surfaceBackground = rgb(255, 255, 255);
    palette.inputBackground = rgb(255, 255, 255);
    palette.previewBackground = rgb(248, 250, 252);
    palette.text = rgb(30, 41, 59);
    palette.secondaryText = rgb(95, 113, 131);
    palette.disabledText = rgb(167, 178, 188);
    palette.border = rgb(200, 211, 222);
    palette.divider = rgb(226, 232, 240);
    palette.hoverBackground = rgb(247, 251, 250);
    palette.selectedBackground = rgb(232, 240, 254);
    palette.pressedBackground = rgb(214, 225, 242);
    palette.accent = rgb(37, 99, 235);
    palette.accentSoft = rgb(232, 240, 254);
    palette.success = rgb(39, 124, 97);
    palette.warning = rgb(217, 119, 6);
    palette.error = rgb(185, 28, 28);
    palette.pin = rgb(245, 158, 11);
    return palette;
}

ThemePalette darkPalette() {
    ThemePalette palette;
    palette.windowBackground = rgb(21, 26, 34);
    palette.sidebarBackground = rgb(26, 33, 44);
    palette.surfaceBackground = rgb(30, 37, 48);
    palette.inputBackground = rgb(43, 47, 54);
    palette.previewBackground = rgb(37, 44, 54);
    palette.text = rgb(226, 232, 240);
    palette.secondaryText = rgb(143, 161, 179);
    palette.disabledText = rgb(111, 123, 137);
    palette.border = rgb(74, 88, 104);
    palette.divider = rgb(52, 62, 75);
    palette.hoverBackground = rgb(57, 64, 73);
    palette.selectedBackground = rgb(53, 63, 78);
    palette.pressedBackground = rgb(44, 52, 62);
    palette.accent = rgb(59, 130, 246);
    palette.accentSoft = rgb(39, 55, 82);
    palette.success = rgb(64, 157, 156);
    palette.warning = rgb(245, 158, 11);
    palette.error = rgb(255, 176, 176);
    palette.pin = rgb(245, 158, 11);
    return palette;
}

ThemePalette eyeCarePalette() {
    ThemePalette palette = lightPalette();
    palette.windowBackground = rgb(238, 243, 234);
    palette.sidebarBackground = rgb(227, 236, 224);
    palette.surfaceBackground = rgb(248, 251, 245);
    palette.inputBackground = rgb(244, 248, 240);
    palette.previewBackground = rgb(234, 241, 230);
    palette.text = rgb(41, 51, 43);
    palette.secondaryText = rgb(102, 115, 106);
    palette.disabledText = rgb(150, 161, 150);
    palette.border = rgb(200, 212, 196);
    palette.divider = rgb(217, 227, 213);
    palette.hoverBackground = rgb(225, 235, 221);
    palette.selectedBackground = rgb(213, 229, 210);
    palette.pressedBackground = rgb(201, 220, 198);
    palette.accent = rgb(92, 141, 90);
    palette.accentSoft = rgb(221, 235, 218);
    palette.success = rgb(79, 138, 90);
    palette.warning = rgb(178, 122, 40);
    palette.error = rgb(185, 74, 72);
    palette.pin = rgb(198, 138, 34);
    return palette;
}

ThemePalette eyeCareNightPalette() {
    ThemePalette palette = darkPalette();
    palette.windowBackground = rgb(29, 38, 32);
    palette.sidebarBackground = rgb(35, 47, 39);
    palette.surfaceBackground = rgb(41, 54, 44);
    palette.inputBackground = rgb(48, 62, 50);
    palette.previewBackground = rgb(35, 48, 39);
    palette.text = rgb(218, 230, 215);
    palette.secondaryText = rgb(153, 173, 151);
    palette.disabledText = rgb(111, 132, 112);
    palette.border = rgb(78, 103, 82);
    palette.divider = rgb(57, 76, 61);
    palette.hoverBackground = rgb(57, 78, 61);
    palette.selectedBackground = rgb(57, 88, 62);
    palette.pressedBackground = rgb(67, 101, 70);
    palette.accent = rgb(121, 177, 109);
    palette.accentSoft = rgb(48, 75, 53);
    palette.success = rgb(121, 177, 109);
    palette.warning = rgb(220, 171, 87);
    palette.error = rgb(246, 150, 145);
    palette.pin = rgb(220, 171, 87);
    return palette;
}

ThemePalette paperPalette() {
    ThemePalette palette = lightPalette();
    palette.windowBackground = rgb(245, 241, 232);
    palette.sidebarBackground = rgb(237, 230, 218);
    palette.surfaceBackground = rgb(252, 249, 242);
    palette.inputBackground = rgb(250, 247, 239);
    palette.previewBackground = rgb(245, 238, 226);
    palette.text = rgb(61, 51, 41);
    palette.secondaryText = rgb(107, 83, 68);
    palette.disabledText = rgb(157, 141, 125);
    palette.border = rgb(196, 184, 168);
    palette.divider = rgb(222, 211, 194);
    palette.hoverBackground = rgb(237, 230, 218);
    palette.selectedBackground = rgb(237, 218, 194);
    palette.pressedBackground = rgb(224, 199, 169);
    palette.accent = rgb(184, 90, 60);
    palette.accentSoft = rgb(246, 225, 211);
    palette.success = rgb(62, 125, 90);
    palette.warning = rgb(184, 134, 11);
    palette.error = rgb(166, 63, 45);
    palette.pin = rgb(184, 134, 11);
    return palette;
}

ThemePalette midnightPalette() {
    ThemePalette palette = darkPalette();
    palette.windowBackground = rgb(13, 27, 42);
    palette.sidebarBackground = rgb(18, 37, 57);
    palette.surfaceBackground = rgb(27, 38, 59);
    palette.inputBackground = rgb(27, 38, 59);
    palette.previewBackground = rgb(20, 38, 58);
    palette.text = rgb(224, 225, 221);
    palette.secondaryText = rgb(156, 172, 188);
    palette.disabledText = rgb(104, 124, 145);
    palette.border = rgb(65, 90, 119);
    palette.divider = rgb(38, 61, 86);
    palette.hoverBackground = rgb(35, 57, 82);
    palette.selectedBackground = rgb(35, 72, 97);
    palette.pressedBackground = rgb(45, 91, 116);
    palette.accent = rgb(56, 198, 228);
    palette.accentSoft = rgb(28, 64, 82);
    palette.success = rgb(154, 219, 164);
    palette.warning = rgb(242, 206, 107);
    palette.error = rgb(255, 176, 176);
    palette.pin = rgb(242, 206, 107);
    return palette;
}

ThemeDefinition makeBuiltIn(const char* id, const wchar_t* name,
                            ThemeAppearance appearance, const ThemePalette& palette) {
    ThemeDefinition theme;
    theme.id = id;
    theme.name = name;
    theme.palette = palette;
    theme.appearance = appearance;
    theme.builtIn = true;
    theme.explicitColors.fill(true);
    return theme;
}

std::string lowerAscii(std::string value) {
    for (char& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

std::string trimAscii(std::string value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool validThemeId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char ch : id) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.')) {
            return false;
        }
    }
    return true;
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

int colorIndex(const std::string& key) {
    const std::string normalized = lowerAscii(key);
    for (std::size_t i = 0; i < kThemeColorCount; ++i) {
        if (lowerAscii(kColorNames[i]) == normalized) return static_cast<int>(i);
    }
    return -1;
}

void copyExplicitColors(ThemeDefinition& target, const ThemeDefinition& source) {
    for (std::size_t i = 0; i < kThemeColorCount; ++i) {
        if (!target.explicitColors[i]) {
            setThemePaletteColor(target.palette, i, themePaletteColor(source.palette, i));
        }
    }
    if (target.baseId.empty()) target.baseId = source.id;
}

} // namespace

const char* themeColorName(std::size_t index) {
    return index < kThemeColorCount ? kColorNames[index] : "";
}

COLORREF themePaletteColor(const ThemePalette& palette, std::size_t index) {
    const COLORREF values[kThemeColorCount] = {
        palette.windowBackground, palette.sidebarBackground, palette.surfaceBackground,
        palette.inputBackground, palette.previewBackground, palette.text, palette.secondaryText,
        palette.disabledText, palette.border, palette.divider, palette.hoverBackground,
        palette.selectedBackground, palette.pressedBackground, palette.accent, palette.accentSoft,
        palette.success, palette.warning, palette.error, palette.pin,
    };
    return index < kThemeColorCount ? values[index] : RGB(0, 0, 0);
}

void setThemePaletteColor(ThemePalette& palette, std::size_t index, COLORREF value) {
    COLORREF* values[kThemeColorCount] = {
        &palette.windowBackground, &palette.sidebarBackground, &palette.surfaceBackground,
        &palette.inputBackground, &palette.previewBackground, &palette.text,
        &palette.secondaryText, &palette.disabledText, &palette.border, &palette.divider,
        &palette.hoverBackground, &palette.selectedBackground, &palette.pressedBackground,
        &palette.accent, &palette.accentSoft, &palette.success, &palette.warning,
        &palette.error, &palette.pin,
    };
    if (index < kThemeColorCount) *values[index] = value;
}

std::string themeColorToHex(COLORREF color) {
    char value[8]{};
    sprintf_s(value, "%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return value;
}

bool parseThemeColor(const std::string& value, COLORREF& color) {
    std::string text = trimAscii(value);
    if (!text.empty() && text.front() == '#') text.erase(text.begin());
    if (text.size() != 6) return false;
    unsigned long parsed = 0;
    for (char ch : text) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        parsed = parsed * 16 + (ch <= '9' ? static_cast<unsigned long>(ch - '0') :
            static_cast<unsigned long>(std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10));
    }
    color = RGB((parsed >> 16) & 0xFF, (parsed >> 8) & 0xFF, parsed & 0xFF);
    return true;
}

ThemeRegistry::ThemeRegistry() {
    resetBuiltInThemes();
}

void ThemeRegistry::resetBuiltInThemes() {
    themes_.clear();
    themes_.push_back(makeBuiltIn("cliplite-light", L"ClipLite Light",
                                  ThemeAppearance::Light, lightPalette()));
    themes_.push_back(makeBuiltIn("cliplite-dark", L"ClipLite Dark",
                                  ThemeAppearance::Dark, darkPalette()));
    themes_.push_back(makeBuiltIn("eye-care", L"护眼绿",
                                  ThemeAppearance::Light, eyeCarePalette()));
    themes_.push_back(makeBuiltIn("eye-care-night", L"护眼夜间",
                                  ThemeAppearance::Dark, eyeCareNightPalette()));
    themes_.push_back(makeBuiltIn("paper", L"纸张暖色",
                                  ThemeAppearance::Light, paperPalette()));
    themes_.push_back(makeBuiltIn("midnight", L"午夜蓝",
                                  ThemeAppearance::Dark, midnightPalette()));
}

const ThemeDefinition* ThemeRegistry::find(const std::string& id) const {
    const auto found = std::find_if(themes_.begin(), themes_.end(), [&id](const ThemeDefinition& theme) {
        return theme.id == id;
    });
    return found == themes_.end() ? nullptr : &*found;
}

bool ThemeRegistry::hasCustomTheme(const std::string& id) const {
    const ThemeDefinition* theme = find(id);
    return theme && !theme->builtIn;
}

const ThemeDefinition* ThemeRegistry::resolveSelection(const ThemeSelection& selection,
                                                       bool systemDark) const {
    const std::string& requested = selection.followSystem
        ? (systemDark ? selection.systemDarkThemeId : selection.systemLightThemeId)
        : selection.fixedThemeId;
    if (const ThemeDefinition* theme = find(requested)) return theme;
    const char* fallback = systemDark ? "cliplite-dark" : "cliplite-light";
    return find(fallback);
}

void ThemeRegistry::resolveCustomThemes() {
    for (ThemeDefinition& theme : themes_) {
        if (theme.builtIn) continue;
        const ThemeDefinition* base = find(theme.baseId);
        if (!base || base == &theme) base = find("cliplite-light");
        if (base) {
            if (!theme.explicitAppearance) theme.appearance = base->appearance;
            copyExplicitColors(theme, *base);
            if (theme.baseId.empty()) theme.baseId = base->id;
        }
        theme.explicitColors.fill(true);
    }
}

bool ThemeRegistry::load(const std::wstring& path) {
    resetBuiltInThemes();
    std::ifstream file(path, std::ios::binary);
    if (!file) return true;

    ThemeDefinition current;
    bool inTheme = false;
    auto finalize = [&]() {
        if (inTheme && validThemeId(current.id)) {
            const auto existing = std::find_if(themes_.begin(), themes_.end(),
                [&current](const ThemeDefinition& theme) { return theme.id == current.id; });
            if (existing == themes_.end()) {
                themes_.push_back(current);
            } else if (!existing->builtIn) {
                *existing = current;
            }
        }
        current = ThemeDefinition{};
        inTheme = false;
    };
    std::string line;
    while (std::getline(file, line)) {
        line = trimAscii(line);
        if (line.empty() || line.front() == ';') continue;
        if (line.front() == '[') {
            finalize();
            inTheme = line == "[theme]";
            continue;
        }
        if (!inTheme) continue;
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = trimAscii(line.substr(0, separator));
        const std::string value = trimAscii(line.substr(separator + 1));
        const std::string normalized = lowerAscii(key);
        if (normalized == "id") current.id = value;
        else if (normalized == "name") current.name = utf8ToWide(value);
        else if (normalized == "base") current.baseId = value;
        else if (normalized == "appearance") {
            current.appearance = lowerAscii(value) == "dark"
                ? ThemeAppearance::Dark : ThemeAppearance::Light;
            current.explicitAppearance = true;
        } else {
            const int index = colorIndex(key);
            COLORREF color = 0;
            if (index >= 0 && parseThemeColor(value, color)) {
                setThemePaletteColor(current.palette, static_cast<std::size_t>(index), color);
                current.explicitColors[static_cast<std::size_t>(index)] = true;
            }
        }
    }
    finalize();
    resolveCustomThemes();
    return true;
}

bool ThemeRegistry::saveCustomTheme(const ThemeDefinition& theme, const std::wstring& path) {
    if (!validThemeId(theme.id) || theme.builtIn || theme.name.empty()) return false;
    std::vector<ThemeDefinition> saved;
    for (const ThemeDefinition& current : themes_) {
        if (!current.builtIn && current.id == theme.id) continue;
        if (!current.builtIn) saved.push_back(current);
    }
    saved.push_back(theme);

    const std::wstring tempPath = path + L".tmp";
    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << "; ClipLite custom themes. Colors use RRGGBB.\n";
    for (const ThemeDefinition& current : saved) {
        file << "\n[theme]\n"
             << "id=" << current.id << "\n"
             << "name=" << wideToUtf8(current.name) << "\n"
             << "base=" << (current.baseId.empty() ? "cliplite-light" : current.baseId) << "\n"
             << "appearance=" << (current.appearance == ThemeAppearance::Dark ? "dark" : "light") << "\n";
        for (std::size_t i = 0; i < kThemeColorCount; ++i) {
            file << themeColorName(i) << "=" << themeColorToHex(themePaletteColor(current.palette, i)) << "\n";
        }
    }
    file.flush();
    file.close();
    if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return load(path);
}

bool ThemeRegistry::removeCustomTheme(const std::string& id, const std::wstring& path) {
    if (!hasCustomTheme(id)) return false;
    const std::wstring tempPath = path + L".tmp";
    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << "; ClipLite custom themes. Colors use RRGGBB.\n";
    for (const ThemeDefinition& current : themes_) {
        if (current.builtIn || current.id == id) continue;
        file << "\n[theme]\n"
             << "id=" << current.id << "\n"
             << "name=" << wideToUtf8(current.name) << "\n"
             << "base=" << (current.baseId.empty() ? "cliplite-light" : current.baseId) << "\n"
             << "appearance=" << (current.appearance == ThemeAppearance::Dark ? "dark" : "light") << "\n";
        for (std::size_t i = 0; i < kThemeColorCount; ++i) {
            file << themeColorName(i) << "=" << themeColorToHex(themePaletteColor(current.palette, i)) << "\n";
        }
    }
    file.flush();
    file.close();
    if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return load(path);
}
