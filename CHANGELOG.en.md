# Changelog

## [1.5.2] - 2026-10-11

- Improved history-window interaction: held Up/Down keys now move one item at a fixed interval, search-result text editing reliably receives focus, and optional category memory and list-density modes are available.
- Updated history paste menus and settings categories; plain-text paste is shown by item type and the ineffective rich-text paste shortcut was removed.
- Improved cleanup, thumbnail reclamation, history compaction, and storage statistics; fixed cleanup confirmation and compressed encrypted-thumbnail recovery issues.

## [1.5.1] - 2026-10-10

- Fixed blank images pasted from some QQ clipboard history items.
- Fixed system-theme notifications and native tray-menu light/dark mode handling.

## [1.5.0] - 2026-10-10

- Added built-in themes, a custom theme editor, history notes, and plain-text editing for Text/HTML items.
- Added editing, pinning, cross-category favorites, and filter shortcuts, with expanded history context-menu actions and cleanup scopes.
- Improved history recovery, replacement, compaction, settings scrolling, high-DPI layout, and idle resource cleanup.
- Added GitHub Releases update checks, a source-code entry, and installed/portable package detection.

## [1.4.0] - 2026-09-28

- Improved image detail previews with zooming, panning, and on-demand original-image loading.
- Optimized image decoding and preview cleanup; fixed single-instance forwarding and filter-menu interactions.

## [1.3.0] - 2026-09-23

- Added per-user scheduled-task handling for administrator launches to reduce repeated UAC prompts.
- Fixed administrator-mode command arguments and refined settings scrolling, theme switching, and control layout.

## [1.2.6] - 2026-09-20

- Fixed automatic cleanup and duplicate-content updates affecting pinned records.
- Enabled automatic preview by default while preserving an existing explicit opt-out.

## [1.2.5] - 2026-09-16

- Added an option to remove history, cache, settings, and logs after uninstall.
- Fixed uninstaller startup and completed English and Simplified Chinese installer messages.

## [1.2.4] - 2026-09-15

- Added English and Simplified Chinese installer languages with automatic Windows-language selection.
- Fixed uninstall shutdown races that could leave the main executable locked.

## [1.2.3] - 2026-09-15

- Fixed the tray icon disappearing after Explorer or taskbar refreshes.
- Added shortcut conflict detection and registration status details.

## [1.2.2] - 2026-09-14

- Moved history archiving, capacity cleanup, and pre-paste reads to background storage tasks to reduce UI stalls.
- Improved clipboard-capture shutdown, worker cleanup, and synchronized storage access.

## [1.2.1] - 2026-09-11

- Improved long-text detail previews and scrolling; fixed accidental preview dismissal.
- Refined portable and installer distribution support.

## [1.2.0] - 2026-09-10

- Added original-content previews, image zooming and panning, long-text scrolling, file-image thumbnails, and a disk thumbnail cache.
- Added a settings scrollbar and improved high-DPI layout, history deletion, image loading, and paste responsiveness.
- Fixed storage limits, list scroll position, thumbnail jobs, and clipboard paste issues.

## [1.1.0] - 2026-09-03

- Expanded history filters with time, content length, source application, and sort conditions.
- Added move-pasted-item-to-top, stable record metadata, and shortcut conflict detection.
- Improved clipboard capture, startup recovery, duplicate updates, and filter-menu interaction.

## [1.0.5] - 2026-08-26

- Fixed history search focus and added Chinese IME compatibility mode.
- Refined history layout, text-preview formatting, and search interaction.

## [1.0.4] - 2026-08-25

- Reduced documentation assets in installer and portable packages; UI screenshots are served from versioned GitHub URLs.

## [1.0.3] - 2026-08-25

- Improved the settings window, history-encryption guidance, and support workflow with an isolated helper process.
- Loaded support images asynchronously to reduce bundled release assets.

## [1.0.2] - 2026-08-24

- Fixed the history popup taking input focus from the original application.
- Fixed UTF-8 preview-boundary display issues and refined keyboard navigation and paste interaction.

## [1.0.1] - 2026-08-24

- Fixed duplicate records caused by staged clipboard notifications.
- Hardened image, storage, search-task, and settings-write failure paths; fixed history ordering and HTML search issues.

## [1.0.0] - 2026-08-23

- First stable Windows x64 release with installer and portable packages.
- Added clipboard history capture, search, filtering, pinning, deletion, automatic paste, and text, HTML, file-list, and image support.
- Added English and Simplified Chinese interfaces, themes, retention controls, source filtering, sensitive-text expiry, and optional DPAPI encryption.
- Added append-only v4 history storage with validation, corruption recovery, and on-demand payload loading.
