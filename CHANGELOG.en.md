# Changelog

## [1.0.1] - 2026-08-24

Fixes duplicate text records caused by staged clipboard notifications and hardens malformed clipboard input, storage failure paths, and search task lifetime handling.

### Fixes and Verification

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
