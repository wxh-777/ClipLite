# ClipLite

ClipLite is a lightweight native clipboard history manager for Windows. It is built with C++17, the Win32 API, and GDI/GDI+, without WebView, Electron, WinUI, Qt, or another large runtime. The project prioritizes low idle memory usage, reliable clipboard format handling, and portable distribution.

Current release: **1.3.0** (Windows x64)

中文文档：[README.md](README.md)

## Why ClipLite Exists

As AI applications continue to grow, storage costs keep rising and memory and device resources are becoming more valuable. Many desktop tools consume a large amount of space and memory even when they are only sitting in the background.

ClipLite was designed to be more restrained: keep the program small, use less memory in the background, and read history only when it is needed. Every resource saved can remain available for the applications and work that matter.

## Features

- Open clipboard history with `Alt+V`, with an optional `Win+V` replacement mode.
- Capture and restore plain text, HTML, file lists, DIB, and DIBV5 images; image files in file lists show on-demand thumbnails.
- Search and combine type, time range, content length, source application, and sort filters; pin, delete, clear, and paste history items.
- History items show clipboard content previews so you can confirm the target before pasting.
- Supports a separate original-content preview window; choose automatic preview, hold-key preview, or disabled preview in Settings or the history context menu. Hold-key preview uses `F2` by default. Long text scrolls with the mouse wheel, and images zoom around the mouse position and support dragging.
- Automatic preview is enabled by default; an existing configuration that explicitly disabled it remains disabled.
- Search focuses the input by default; an optional compatibility mode keeps the original app in the foreground and uses `Ctrl+Space` to enter Chinese IME input.
- Long settings pages show a scrollbar on the right, with wheel scrolling, page jumps on the track, and a draggable thumb.
- Paste as plain text or rich text; images are loaded on demand with bounded memory and disk thumbnail caches.
- Automatic, light, and dark themes with blue, purple, green, and orange accents.
- English and Simplified Chinese interfaces.
- Runs in the system tray and restores its tray icon after Explorer restarts or refreshes the taskbar.
- Limits for record count, disk usage, retention period, item size, and monitoring pause.
- Pinned records are protected from automatic count, capacity, expiry, and startup cleanup, and are not overwritten by new clipboard content.
- Source application filtering, sensitive text expiry, and optional Windows DPAPI user encryption.
- Installer and portable distribution modes; clipboard archiving, history reads, and usage statistics run through short-lived worker tasks without keeping the full history payload in memory.
- The history window does not take input focus from the original application; left-clicking an item pastes it without requiring the input field to be selected again.
- When "Run ClipLite as administrator" is enabled, Windows authorization is required once; later launches use a per-user highest-privilege scheduled task and do not show the UAC prompt again. Disabling the option removes the task.
- Running as administrator can improve clipboard access and paste compatibility with some elevated windows where Windows permits it; ClipLite still cannot bypass UIPI, the secure desktop, or protected-window restrictions.

## Screenshots

History window with an image record:

![ClipLite history window](https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/history.png)

Settings window:

![ClipLite settings window](https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/settings.png)

Idle memory shown in Task Manager:

![ClipLite Task Manager memory usage](https://raw.githubusercontent.com/wxh-777/ClipLite/main/docs/screenshots/task-manager-memory.png)

## Performance and Size

ClipLite is designed to stay quietly in the tray. It uses less memory than many clipboard history tools, making it suitable for long-running background use without requiring a separate runtime.

Clipboard capture, history archiving, and pre-paste history reads run in the background. When an application delays providing HTML, text, or image formats after copying, disk writes and decryption do not block ClipLite's history window, paste flow, or global shortcuts.

- Task Manager showed about `0.7 MB` of idle memory in a local screenshot measurement.
- A separate local Release measurement showed about `2.5 MB` of Private Bytes; this uses a different accounting method.
- The main executable is under `1 MB`.
- Startup took about `0.05 seconds`, while full history and images are read only when needed.
- The history popup caches recently displayed image thumbnails, so continuous scrolling does not reread and decode the same source image.

This makes ClipLite suitable for long-running background use and portable folders such as a USB drive.

<details>
<summary>Show detailed measurements</summary>

Reference measurements from the current Windows x64 Release build on this machine: Task Manager idle memory about `0.7 MB`, Working Set `13.73 MB`, Private Bytes `2.50 MB`, main executable under `1 MB`, startup time `53.03 ms`, GDI `13`, and USER `10`. Task Manager, Working Set, and Private Bytes use different memory definitions, so their displayed values can differ; the `0.7 MB` value is the idle Task Manager observation.

The values were sampled by `tools/measure.ps1` about one second after startup while idle, with the history window closed. Actual values vary with Windows version, DPI, system state, history data, and runtime scenario. These figures describe the local Release baseline and are not a fixed guarantee for every device.

</details>

## Why ClipLite

ClipLite is for people who want clipboard history to be easy to find without making the computer feel slower.

| Comparison | ClipLite | Common clipboard tools |
| --- | --- | --- |
| Background usage | About `0.7 MB` idle memory shown by Task Manager; about `2.5 MB` Private Bytes | Resident usage often grows with feature scope |
| Program size | Main executable under `1 MB` | Usually larger when bundled with runtimes or extra features |
| Runtime | Native Win32, without WebView or a large runtime | Some tools depend on an additional runtime or framework |
| Data control | History stays local by default, with optional DPAPI encryption | Data location and privacy policies vary |
| Distribution | Installer and portable packages, including USB-friendly use | Usually installer-first |
| Clipboard formats | Text, HTML, images, and file lists | Format coverage varies by tool |
| Loading strategy | Full content and images are read on demand | Large histories may cause more reading and memory pressure |
| Product focus | Public source, noncommercial license, focused clipboard history | Many alternatives are commercial or broader utility suites |

Features and versions vary across tools. This table describes product positioning rather than a benchmark ranking under identical conditions.

## Download

The Windows x64 release provides:

- Installer: `ClipLite-Setup-1.3.0-x64.exe`, with English and Simplified Chinese wizard languages, automatic selection based on the Windows UI language, a selectable installation directory, shortcuts, and standard uninstall support.
- Portable package: `ClipLite-1.3.0-portable-win-x64.zip`, which can be extracted and moved as a directory. Data is stored in its `data\\` directory.

Use `SHA256SUM.txt` in the portable package to verify `ClipLite.exe`. Verify the download source before running the application.

## Shortcuts

- `Alt+V`: Open clipboard history.
- Left click: Paste the selected item without taking input focus from the original application.
- `Ctrl+Shift+V`: Paste as plain text.
- `Ctrl+Shift+R`: Paste as rich text when available.
- `Delete`: Delete the selected item.
- `Esc`: Close the history window.
- `F10`: Open settings.
- `Ctrl+0`: Clear the history filter.
- `F2`: Hold to preview the current history item when hold-key preview is enabled.

History, settings, and monitoring shortcuts can be changed in Settings. The registration status shows whether ClipLite shortcuts conflict internally, and duplicate combinations are rejected. If a system-wide shortcut is occupied by Windows or another application, Settings identifies the affected action and restores its default combination when available; otherwise it asks you to choose another shortcut. `Win+V` replacement also depends on the current integrity level; while it is enabled, the history shortcut cannot also be set to `Win+V`.
Settings descriptions automatically reserve enough space for multiple lines.

The General settings section includes a "Move pasted item to top" setting. When enabled, history is ordered by recent use activity; when disabled, it is ordered by recent copy time. Each record also stores a stable ID, first-captured time, millisecond copy time, last-used time, copy count, use count, and logical content length for sorting and filters.

## Data and Privacy

The default locations in installer mode are:

- History: `%LOCALAPPDATA%\\ClipLite\\history.bin`
- Settings: `%LOCALAPPDATA%\\ClipLite\\settings.ini`
- Diagnostic log: `%LOCALAPPDATA%\\ClipLite\\cliplite.log`

Portable mode uses the package's `data\\` directory. The cache directory can be changed in Settings. ClipLite does not write clipboard payloads to diagnostic logs and does not write user data into the source tree.
When uninstalling the installed version, the program files and shortcuts are removed first, while all user data is kept by default. After the uninstall completes, confirm the separate prompt if you also want to delete ClipLite files, settings, and logs from the default or configured data directory.

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
