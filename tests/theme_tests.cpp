#include "theme.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path directory = fs::temp_directory_path() /
        ("ClipLiteThemeTests-" + std::to_string(GetCurrentProcessId()));
    std::error_code error;
    fs::remove_all(directory, error);
    fs::create_directories(directory, error);
    if (error) return 2;

    const fs::path path = directory / "themes.ini";
    {
        std::ofstream file(path, std::ios::binary);
        file << "[theme]\n"
                "id=custom-night\n"
                "name=Custom Night\n"
                "base=eye-care-night\n"
                "accent=#12aBcD\n"
                "\n[theme]\n"
                "id=invalid-color\n"
                "name=Invalid Color\n"
                "base=cliplite-light\n"
                "accent=12XYZ9\n";
        file << "\n[theme]\n"
                "id=custom-mixed\n"
                "name=Custom Mixed\n"
                "base=eye-care-night\n"
                "accent=#12aBcD\n"
                "\n[theme]\n"
                "id=custom-night\n"
                "name=Custom Night Revised\n"
                "base=eye-care-night\n"
                "accent=445566\n";
    }

    ThemeRegistry registry;
    check(registry.load(path.wstring()), "theme file loads");
    check(registry.themes().size() == 9, "built-in and unique custom themes are registered");
    const ThemeDefinition* custom = registry.find("custom-night");
    check(custom && custom->appearance == ThemeAppearance::Dark,
          "appearance inherits from the base theme");
    check(custom && custom->palette.accent == RGB(68, 85, 102) &&
          custom->name == L"Custom Night Revised", "later duplicate IDs replace earlier sections");
    const ThemeDefinition* mixed = registry.find("custom-mixed");
    check(mixed && mixed->palette.accent == RGB(18, 171, 205),
          "hex colors with hash and mixed case parse");
    const ThemeDefinition* invalid = registry.find("invalid-color");
    check(invalid && invalid->palette.accent == RGB(37, 99, 235),
          "invalid color retains inherited base value");

    ThemeDefinition saved;
    saved.id = "saved-custom";
    saved.name = L"Saved Custom";
    saved.baseId = "cliplite-light";
    saved.palette = registry.find("cliplite-light")->palette;
    saved.palette.accent = RGB(1, 2, 3);
    saved.appearance = ThemeAppearance::Light;
    saved.builtIn = false;
    check(registry.saveCustomTheme(saved, path.wstring()), "custom theme saves atomically");
    const ThemeDefinition* reloaded = registry.find("saved-custom");
    check(reloaded && reloaded->palette.accent == RGB(1, 2, 3),
          "custom theme colors survive reload");

    ThemeSelection selection;
    selection.followSystem = true;
    selection.systemLightThemeId = "eye-care";
    selection.systemDarkThemeId = "custom-night";
    const ThemeDefinition* light = registry.resolveSelection(selection, false);
    const ThemeDefinition* dark = registry.resolveSelection(selection, true);
    check(light && light->id == "eye-care", "system light choice resolves independently");
    check(dark && dark->id == "custom-night", "system dark choice resolves independently");

    check(registry.removeCustomTheme("invalid-color", path.wstring()), "custom theme deletes");
    check(registry.find("invalid-color") == nullptr, "deleted custom theme disappears");
    check(registry.find("cliplite-light") != nullptr, "built-in theme cannot be removed");

    fs::remove_all(directory, error);
    std::cout << "Theme tests: " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
