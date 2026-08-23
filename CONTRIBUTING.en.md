# Contributing

中文：[CONTRIBUTING.md](CONTRIBUTING.md)

Thank you for contributing to ClipLite. The project targets Windows and prioritizes low idle memory usage, reliable clipboard format handling, recoverable storage, and stable native interaction.

## Reporting Issues

Before opening an issue, verify the problem on the latest release and include:

- ClipLite version, Windows version, and architecture.
- Reproduction steps and the actual result.
- Expected behavior and whether the issue depends on elevation or DPI.
- Relevant redacted log excerpts. Remove clipboard contents, tokens, paths, and other sensitive information.

## Development

Requirements are Windows, Visual Studio 2019 or later, and CMake 3.15 or later. Use the x64 generator and the repository's `build-x64` directory:

```pwsh
cmake -S . -B build-x64 -A x64 -DBUILD_TESTING=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release --output-on-failure
```

The code uses C++17, Win32, and GDI/GDI+. Do not add Electron, WebView, WinUI, XAML, Qt, SQLite, or resident GPU resources. Do not commit user history, logs, dumps, or build outputs.

## Change Requirements

- Storage changes must cover success, boundary, corruption, truncation, and restart recovery cases.
- Storage tests must use `CLIPLITE_TEST_DATA_DIR` under `%TEMP%`, never the real `%LOCALAPPDATA%\\ClipLite` directory.
- Update `README.md` and `CHANGELOG.md` for user-visible behavior changes.
- Rebuild Release after resource or build configuration changes.
- Check DPI, themes, rounded geometry, and scroll boundaries for UI changes. Screenshots must use PNG, JPEG, GIF, or WebP.
- Run `git diff --check`, a Release build, and CTest before submitting.

## License

By submitting code, you confirm that you have the right to submit it and agree that it may be provided under the [PolyForm Noncommercial License 1.0.0](LICENSE.md). Commercial use, commercial integration, and paid distribution require a separate written license from the copyright holder.
