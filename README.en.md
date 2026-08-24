# ClipLite

ClipLite is a lightweight native clipboard history manager for Windows. It is built with C++17, the Win32 API, and GDI/GDI+, without WebView, Electron, WinUI, Qt, or another large runtime. The project prioritizes low idle memory usage, reliable clipboard format handling, and portable distribution.

Current release: **1.0.2** (Windows x64)

中文文档：[README.md](README.md)

## Why ClipLite Exists

As AI applications continue to grow, storage costs keep rising and memory and device resources are becoming more valuable. Many desktop tools consume a large amount of space and memory even when they are only sitting in the background.

ClipLite was designed to be more restrained: keep the program small, use less memory in the background, and read history only when it is needed. Every resource saved can remain available for the applications and work that matter.

## Features

- Open clipboard history with `Alt+V`, with an optional `Win+V` replacement mode.
- Capture and restore plain text, HTML, file lists, DIB, and DIBV5 images.
- Search, filter by type, pin, delete, clear, and paste history items.
- Paste as plain text or rich text; images are loaded on demand only when visible.
- Automatic, light, and dark themes with blue, purple, green, and orange accents.
- English and Simplified Chinese interfaces.
- Limits for record count, disk usage, retention period, item size, and monitoring pause.
- Source application filtering, sensitive text expiry, and optional Windows DPAPI user encryption.
- Installer and portable distribution modes with no resident worker thread and no full history payload kept in memory.

## Screenshots

History window with an image record:

![ClipLite history window](docs/screenshots/history.png)

Settings window:

![ClipLite settings window](docs/screenshots/settings.png)

## Performance and Size

ClipLite is designed to stay quietly in the tray. It uses less memory than many clipboard history tools, making it suitable for long-running background use without requiring a separate runtime.

- The Windows Task Manager showed about `2.4 MB` of memory in a local idle observation.
- The main executable is about `535 KB`, smaller than an ordinary phone photo.
- Startup took about `0.06 seconds`, while full history and images are read only when needed.
- The history popup caches recently displayed image thumbnails, so continuous scrolling does not reread and decode the same source image.

This makes ClipLite suitable for long-running background use and portable folders such as a USB drive.

<details>
<summary>Show detailed measurements</summary>

Reference measurements from the current Windows x64 Release build on this machine: Working Set `13.35 MB`, Private Bytes `2.40 MB`, main executable `535.5 KB`, startup time `60.12 ms`, GDI `13`, and USER `10`. Task Manager and measurement tools use different memory definitions, so their displayed values can differ.

The values were sampled by `tools/measure.ps1` about one second after startup while idle, with the history window closed. Actual values vary with Windows version, DPI, system state, history data, and runtime scenario. These figures describe the local Release baseline and are not a fixed guarantee for every device.

</details>

## Why ClipLite

ClipLite is for people who want clipboard history to be easy to find without making the computer feel slower.

| Comparison | ClipLite | Common clipboard tools |
| --- | --- | --- |
| Background usage | Lightweight by design; about `2.4 MB` in a local idle observation | Resident usage often grows with feature scope |
| Program size | Main executable about `535 KB` | Usually larger when bundled with runtimes or extra features |
| Runtime | Native Win32, without WebView or a large runtime | Some tools depend on an additional runtime or framework |
| Data control | History stays local by default, with optional DPAPI encryption | Data location and privacy policies vary |
| Distribution | Installer and portable packages, including USB-friendly use | Usually installer-first |
| Clipboard formats | Text, HTML, images, and file lists | Format coverage varies by tool |
| Loading strategy | Full content and images are read on demand | Large histories may cause more reading and memory pressure |
| Product focus | Public source, noncommercial license, focused clipboard history | Many alternatives are commercial or broader utility suites |

Features and versions vary across tools. This table describes product positioning rather than a benchmark ranking under identical conditions.

## Download

The Windows x64 release provides:

- Installer: `ClipLite-Setup-1.0.2-x64.exe`, with a selectable installation directory, shortcuts, and standard uninstall support.
- Portable package: `ClipLite-1.0.2-portable-win-x64`, which can be moved as a directory. Data is stored in its `data\\` directory.

Use `SHA256SUM.txt` in the portable package to verify `ClipLite.exe`. Verify the download source before running the application.

## Shortcuts

- `Alt+V`: Open clipboard history.
- `Enter` or left click: Paste the selected item.
- `Ctrl+Shift+V`: Paste as plain text.
- `Ctrl+Shift+R`: Paste as rich text when available.
- `Delete`: Delete the selected item.
- `Esc`: Close the history window.
- `F10`: Open settings.
- `Ctrl+0`: Clear the history filter.

History, settings, and monitoring shortcuts can be changed in Settings. `Win+V` replacement depends on Windows, the current integrity level, and shortcut conflicts. `Alt+V` remains available when registration fails.

## Data and Privacy

The default locations in installer mode are:

- History: `%LOCALAPPDATA%\\ClipLite\\history.bin`
- Settings: `%LOCALAPPDATA%\\ClipLite\\settings.ini`
- Diagnostic log: `%LOCALAPPDATA%\\ClipLite\\cliplite.log`

Portable mode uses the package's `data\\` directory. The cache directory can be changed in Settings. ClipLite does not write clipboard payloads to diagnostic logs and does not write user data into the source tree.

DPAPI encryption is disabled by default. When enabled, history payloads can only be recovered by the same Windows user. Clearing history removes the local history file; pausing monitoring prevents new content from being recorded. Sensitive-content expiry is disabled by default and only checks text containing explicit sensitive markers.

ClipLite cannot bypass Windows UIPI. Automatic pasting into elevated or protected windows may fail. The UAC secure desktop, sign-in screen, and other desktop sessions are outside the supported scope.

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

Build output is written to `build-x64\\Release\\` and `out\\`. See [CONTRIBUTING.en.md](CONTRIBUTING.en.md) for development details.

## Command-Line Options

- `ClipLite.exe --history`: Start and open clipboard history.
- `ClipLite.exe --settings`: Start and open Settings.
- `ClipLite.exe --exit`: Ask the running ClipLite instance to exit.

## License

ClipLite is provided under the [PolyForm Noncommercial License 1.0.0](LICENSE.md). The license permits personal, educational, research, and other noncommercial use, study, modification, and distribution. Selling, paid distribution, commercial integration, and other commercial use are prohibited.

This is a source-available license, not an OSI-approved open source license. Commercial use or distribution requires a separate written license from the copyright holder. Third-party dependencies and assets may have separate terms.

## Support and Security

For general issues, provide the ClipLite version, Windows version, reproduction steps, and a redacted log excerpt. Remove clipboard contents, tokens, paths, and other sensitive information before posting.

For security issues, read [SECURITY.en.md](SECURITY.en.md) and do not publish exploit details in a public issue.
