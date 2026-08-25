# Changelog

## [1.0.5] - 2026-08-26

Fixes history search focus handling and adds an optional Chinese IME compatibility mode.

### Fixes and Experience

- Search focuses its input by default, preventing search text from being sent to the original application.
- Fixed history closing when search input was incorrectly treated as an external action.
- Compatibility mode keeps the original app in the foreground, uses `Ctrl+Space` to enter real IME focus, and suppresses the trigger space from the search text.
- Added an auto-saved Search input compatibility setting, disabled by default.
- Widened the history window to `400px`, reduced the gap between filters and the history list, and retained more clipboard preview space.
- Increased the text and HTML history preview limit from `160` to `256` bytes.
- Preserved line breaks and tabs in text previews, and fixed inconsistent formatting between runtime capture and restart recovery.

### Build and Verification

- Unified the application, resources, CMake, installer, package script, and both READMEs on version `1.0.5`.
- x64 Release build, `ClipLiteStore` CTest, and the history-window stress test `100/100` passed.
- Latest Release measurements: main executable `549.5 KB`, startup `56.12 ms`, Working Set `13.51 MB`, Private Bytes `2.73 MB`, GDI `13`, and USER `10`.

## [1.0.4] - 2026-08-25

Reduces documentation assets in installer and portable packages by serving UI screenshots from versioned GitHub URLs.

### Release Adjustments

- Both READMEs now use fixed `v1.0.4` GitHub Raw URLs for UI screenshots.
- Installer and portable packages no longer copy `docs/screenshots`; the repository images remain the online source.
- Package tooling now detects per-user Inno Setup installations automatically.
- Unified the application, Windows resources, build configuration, installer, package script, and both READMEs on version `1.0.4`.

## [1.0.3] - 2026-08-25

Improves the settings window, history-encryption guidance, and support workflow while reducing bundled release assets.

### Features and Experience

- The settings window can now be minimized from its title bar or by clicking its taskbar button, while remaining fixed-size and non-maximizable.
- Windows user encryption now uses an 800 ms debounce, so repeated toggles migrate history only once to the final state; closing settings applies it immediately.
- Privacy settings now explain migration, cross-user/device recovery limits, and the possible clipboard and scrolling performance impact of large encrypted images.
- Support and QQ group windows now run in an isolated helper process that is closed with the settings window or main application.
- Support images are downloaded asynchronously, removing bundled payment and QQ group images from installer and portable packages.
- History wheel input now updates the scroll position directly, removing the unused interpolation timer.

### Build and Verification

- Unified CMake, Windows resources, installer, package script, About page, and both READMEs on version `1.0.3`.
- Updated bilingual UI screenshots and enabled additional MSVC Release function-level and non-incremental linker optimizations.
- x64 Debug and Release builds and the isolated `ClipLiteStore` CTest pass.

## [1.0.2] - 2026-08-24

Fixed the history popup taking input focus, bringing paste interaction closer to the Windows clipboard experience.

Fixed text previews being shown as invalid when a UTF-8 character was split at the preview boundary; the full text and paste content were unaffected.

### Fixes and Verification

- The history popup is now non-activating, so the original application's input focus and caret remain intact.
- A temporary keyboard hook routes list controls such as navigation, paging, Enter, and Esc to the history popup.
- Typing ordinary content closes the history popup while allowing the characters to continue into the original application.
- Removed UI Automation text rewriting and the unused `uiautomationcore` link; paste continues through standard Ctrl+V input.
- UTF-8 preview-boundary coverage was added to the storage tests, including restart recovery.
- x64 Release and isolated `ClipLiteStore` CTest pass.

## [1.0.1] - 2026-08-24

Fixes duplicate text records caused by staged clipboard notifications and hardens malformed clipboard input, storage failure paths, and search task lifetime handling.

### Fixes and Verification

- The history popup caches recently displayed image thumbnails to avoid rereading and decoding source images while scrolling; large-image CRC checks now use a lookup table.
- Added a self-contained image-scroll benchmark that creates 100 images in a process-specific temporary directory and records frame times and GDI object counts.
- History-item clicks now trigger paste after mouse release and include target-input focus diagnostics.
- Plain text and HTML now share a stable semantic hash, preserving richer HTML and promoting the record to the front; self-written paste data remains filtered.
- DIB/DIBV5 validation now checks headers, compression, palettes, stride, pixel length, and integer bounds, with the same validation used by previews.
- Clipboard restoration checks `GlobalLock`, `SetClipboardData`, and ownership transfer; append, rebuild, rekey, and settings writes preserve the previous data on failure.
- Search is limited to one cancellable task per popup and is joined during close; settings now use temporary-file writes followed by atomic replacement.
- Fixed history order reversal after rebuilds, rekeying, and duplicate-record updates followed by restart.
- Fixed HTML searches matching internal format headers, HTML attributes, or offset numbers instead of visible text.
- x64 Release and isolated `ClipLiteStore` CTest pass; storage tests cover duplicate text, HTML enrichment, and restart ordering.

## [1.0.0] - 2026-08-23

The first stable Windows x64 release of ClipLite, including installer and portable packages.

### User Features

- Clipboard history capture, search, filtering, pinning, deletion, clearing, and automatic paste.
- Plain text, HTML, file list, DIB, and DIBV5 capture and restoration.
- `Alt+V`, optional `Win+V` replacement, and configurable shortcuts.
- English and Simplified Chinese interfaces with automatic, light, and dark themes.
- Record count, disk usage, retention, item size, and monitoring pause controls.
- Source application filtering, sensitive text expiry, and optional Windows DPAPI user encryption.
- Installer and portable distributions with SHA-256 verification.

### Reliability and Privacy

- Append-only v4 history storage with length checks, CRC validation, truncation recovery, and atomic replacement during rebuilds.
- Metadata and short previews are loaded at startup; full payloads and images are read on demand.
- Unicode search and temporary background search tasks for larger histories.
- Diagnostic logs exclude clipboard payloads and are size limited.
- Windows UIPI, elevated-window, UAC secure-desktop, and alternate-desktop limitations are documented.

### Documentation

- Added PolyForm Noncommercial License 1.0.0, contribution guidance, and security reporting guidance.
- Added Chinese and English user, contributor, and security documentation.
- Added UI screenshots and bilingual documentation.
- Added a measured Windows x64 Release baseline for memory, startup time, GDI/USER resources, and executable size to both READMEs.
- Added a user-facing positioning comparison covering lightweight usage, native runtime, local-first data, portability, and clipboard format support.
- Added the project motivation to both READMEs: conserve storage and memory as AI applications increase resource demand.
- Unified the CMake, Windows resource, installer, package script, about page, and HTML prototype version to `1.0.0`.

### Verification

- x64 Release build passed.
- `ctest --test-dir build-x64 -C Release --output-on-failure` passed.
- Storage tests cover normal I/O, restart recovery, Unicode search, CRC corruption, tail truncation, DPAPI, and 10,000-record pressure scenarios.
