#ifndef CLIPLITE_THEME_H
#define CLIPLITE_THEME_H

#include <windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class ThemeAppearance {
    Light,
    Dark,
};

constexpr std::size_t kThemeColorCount = 19;

struct ThemePalette {
    COLORREF windowBackground = RGB(240, 244, 248);
    COLORREF sidebarBackground = RGB(232, 238, 245);
    COLORREF surfaceBackground = RGB(255, 255, 255);
    COLORREF inputBackground = RGB(255, 255, 255);
    COLORREF previewBackground = RGB(248, 250, 252);
    COLORREF text = RGB(30, 41, 59);
    COLORREF secondaryText = RGB(95, 113, 131);
    COLORREF disabledText = RGB(167, 178, 188);
    COLORREF border = RGB(200, 211, 222);
    COLORREF divider = RGB(226, 232, 240);
    COLORREF hoverBackground = RGB(247, 251, 250);
    COLORREF selectedBackground = RGB(232, 240, 254);
    COLORREF pressedBackground = RGB(214, 225, 242);
    COLORREF accent = RGB(37, 99, 235);
    COLORREF accentSoft = RGB(232, 240, 254);
    COLORREF success = RGB(39, 124, 97);
    COLORREF warning = RGB(217, 119, 6);
    COLORREF error = RGB(185, 28, 28);
    COLORREF pin = RGB(245, 158, 11);
};

struct ThemeDefinition {
    std::string id;
    std::wstring name;
    std::string baseId;
    ThemePalette palette;
    std::array<bool, kThemeColorCount> explicitColors{};
    ThemeAppearance appearance = ThemeAppearance::Light;
    bool explicitAppearance = false;
    bool builtIn = false;
};

struct ThemeSelection {
    bool followSystem = false;
    std::string fixedThemeId = "cliplite-light";
    std::string systemLightThemeId = "cliplite-light";
    std::string systemDarkThemeId = "cliplite-dark";
};

class ThemeRegistry {
public:
    ThemeRegistry();

    bool load(const std::wstring& path);
    bool saveCustomTheme(const ThemeDefinition& theme, const std::wstring& path);
    bool removeCustomTheme(const std::string& id, const std::wstring& path);

    const std::vector<ThemeDefinition>& themes() const { return themes_; }
    const ThemeDefinition* find(const std::string& id) const;
    const ThemeDefinition* resolveSelection(const ThemeSelection& selection,
                                             bool systemDark) const;
    bool hasCustomTheme(const std::string& id) const;

private:
    void resetBuiltInThemes();
    void resolveCustomThemes();

    std::vector<ThemeDefinition> themes_;
};

const char* themeColorName(std::size_t index);
COLORREF themePaletteColor(const ThemePalette& palette, std::size_t index);
void setThemePaletteColor(ThemePalette& palette, std::size_t index, COLORREF value);
std::string themeColorToHex(COLORREF color);
bool parseThemeColor(const std::string& value, COLORREF& color);

#endif
