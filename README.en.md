# ClipLite

A small but full-featured clipboard history manager for Windows.

ClipLite keeps copied text, images, files, and web content available for later search and pasting. It runs in the system tray while keeping its program size, background usage, and data reads under control.

Current release: **1.5.3 (Windows x64)**

![Windows](https://img.shields.io/badge/Windows-10%2B-0078D6?style=flat-square&logo=windows&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)
![Architecture](https://img.shields.io/badge/architecture-x64-444444?style=flat-square)
![License](https://img.shields.io/badge/license-PolyForm%20Noncommercial-orange?style=flat-square)

[Download](#download-and-run) · [中文文档](README.md) · [Changelog](CHANGELOG.en.md)

## Small but Full-Featured

ClipLite keeps the common features needed for clipboard history while using a native Windows implementation. It does not require an additional runtime, and images and full content are read only when needed.

| Item | Reference value |
| --- | --- |
| Main executable | About `862 KB` |
| Idle memory | About `0.2 MB+` in Task Manager |
| Startup time | About `55 ms` |
| Runtime | Native Win32, with no additional runtime |
| Content reads | Images and full content are loaded on demand |

Actual values vary with Windows version, DPI, history data, and runtime state.

## Technical Details

The project uses a native Windows implementation:

| Area | Implementation |
| --- | --- |
| Language and build | C++17, CMake, Visual Studio, with Windows x64 as the target |
| UI and rendering | Win32 API, native controls, and GDI/GDI+; no WebView, Electron, WinUI, or Qt |
| History storage | Append-only v4 binary records with length limits, CRC checks, truncated-file recovery, and on-demand reads |
| Background work | Clipboard archiving, content reads, cleanup, and compaction run through short-lived worker tasks |
| Data protection | DPAPI is disabled by default; when enabled, history payloads are recoverable by the same Windows user |
| Cache strategy | Images and full content are read on demand; thumbnails are cached separately and can be reclaimed |

## Features

### History

- Press `Alt+V` to open history and search text, images, files, and web content.
- Combine filters for type, time range, content length, source application, and sort order.
- Favorites and pins are independent. The history window can remember its last category.
- Choose Compact, Standard, or Adaptive list density.
- Limit record count, disk usage, retention period, and item size, or pause monitoring temporarily.

### Preview, Editing, and Paste

- Preview images on hover, zoom with the wheel, drag them, and load the original image on demand. Long text is scrollable.
- Paste as plain text or rich text. Text and HTML records can be edited as plain text.
- Save edits in place, save them as a new record, or save and paste immediately.
- Add notes, favorites, and pins to history records, or clean up records in batches.
- Missing file paths are marked in the list and checked again before pasting.

### Appearance and Windows Integration

- Includes light, dark, green, night-green, warm-paper, and midnight-blue themes, plus custom themes.
- Follow Windows light and dark mode changes, with matching high-contrast colors.
- Includes Simplified Chinese and English interfaces.
- Runs in the system tray and restores its icon after Explorer restarts or taskbar refreshes.
- The history shortcut can be changed to `Win+V`, subject to Windows and integrity-level restrictions.

## Screenshots

<table>
  <tr>
    <td align="center"><strong>History</strong><br><img src="https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/history.png" alt="ClipLite history window" width="100%"></td>
    <td align="center"><strong>Settings</strong><br><img src="https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/settings.png" alt="ClipLite settings window" width="100%"></td>
  </tr>
  <tr>
    <td colspan="2" align="center"><strong>Appearance</strong><br><img src="https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/appearance.png" alt="ClipLite appearance settings" width="70%"></td>
  </tr>
</table>

## Download and Run

The Windows x64 release provides two packages:

| Package | File | Use case |
| --- | --- | --- |
| Installer | `ClipLite-Setup-1.5.3-x64.exe` | Installs ClipLite with Start menu, desktop shortcut, and standard uninstall support |
| Portable | `ClipLite-1.5.3-portable-win-x64.zip` | Extract the complete directory and run `ClipLite.exe`; suitable for a USB drive or portable folder |

Portable data is stored in the package's `data\\` directory. Keep that directory when updating; do not replace only `ClipLite.exe`.

Use `SHA256SUM.txt` in the portable package to verify `ClipLite.exe`. Verify the download source before running the application.

## Shortcuts

| Shortcut | Action |
| --- | --- |
| `Alt+V` | Open clipboard history |
| `Enter` or left click | Paste the selected record |
| `Ctrl+Shift+V` | Paste as plain text |
| `Delete` | Delete the selected record |
| `Esc` | Close the history window |
| `F10` | Open Settings |
| `Ctrl+0` | Clear the history filter |
| `Ctrl+F` | Focus the search box |
| `Ctrl+Shift+F` | Open the history filter |
| `Ctrl+E` | Edit the selected text record |
| `Ctrl+Alt+P` | Pin or unpin the history window |
| `Ctrl+Alt+F` | Toggle showing favorites in other categories |
| `F2` | Hold to preview the current record when hold-key preview is enabled |

Shortcuts can be changed in Settings. Duplicate combinations are rejected, and Settings reports conflicts with system-wide shortcuts used by Windows or other applications.

## Updates, Permissions, and Privacy

- The About page supports manual update checks. Optional background checks show new versions through a tray notification.
- Installed upgrades preserve the "Start with Windows" setting. Installer and portable packages use their corresponding update packages.
- Source-application filtering, sensitive-text expiry, and Windows DPAPI user-level encryption are available.
- Administrator mode can be enabled when required. After the first authorization, later launches use a per-user highest-privilege scheduled task.
- Administrator mode cannot bypass Windows UIPI, the secure desktop, or protected-window restrictions.

Installer mode stores data by default at:

```text
%LOCALAPPDATA%\\ClipLite\\history.bin
%LOCALAPPDATA%\\ClipLite\\settings.ini
%LOCALAPPDATA%\\ClipLite\\cliplite.log
```

Portable mode uses the package's `data\\` directory, and another data directory can be selected in Settings. ClipLite does not write clipboard payloads to diagnostic logs. Uninstalling the installed version keeps user data by default; after uninstall, you can choose to remove history, cache, settings, and logs.

## Build

Requirements: Windows, Visual Studio 2019 or later, and CMake 3.15 or later. The project targets x64.

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

Create the portable package and, when Inno Setup is available, the installer:

```pwsh
powershell -ExecutionPolicy Bypass -File packaging/package.ps1
```

Build output is written to `build-x64\\Release\\` and `out\\`. See [CONTRIBUTING.en.md](CONTRIBUTING.en.md) for development details and [SECURITY.en.md](SECURITY.en.md) for security issues.

<details>
<summary>Command-line options</summary>

```text
ClipLite.exe --history    Start and open clipboard history
ClipLite.exe --settings   Start and open Settings
ClipLite.exe --exit       Ask the running ClipLite instance to exit
```

</details>

## License

ClipLite is provided under the [PolyForm Noncommercial License 1.0.0](LICENSE.md). The license permits personal, educational, research, and other noncommercial use, study, modification, and distribution. Selling, paid distribution, commercial integration, and other commercial use are prohibited.

This is a source-available license, not an OSI-approved open source license. Commercial use or distribution requires a separate written license from the copyright holder. Third-party dependencies and assets may have separate terms.
