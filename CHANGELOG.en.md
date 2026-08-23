# Changelog

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
- Added privacy-safe UI screenshots generated from isolated synthetic text and image data.
- Added a measured Windows x64 Release baseline for memory, startup time, GDI/USER resources, and executable size to both READMEs.
- Unified the CMake, Windows resource, installer, package script, about page, and HTML prototype version to `1.0.0`.

### Verification

- x64 Release build passed.
- `ctest --test-dir build-x64 -C Release --output-on-failure` passed.
- Storage tests cover normal I/O, restart recovery, Unicode search, CRC corruption, tail truncation, DPAPI, and 10,000-record pressure scenarios.
