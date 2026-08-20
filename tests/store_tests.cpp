#include "clip_store.h"

#include <windows.h>

#include <cstdio>
#include <io.h>

int main() {
    ClipStore store(10);
    if (!store.open()) return 1;
    store.clear();
    const std::string text = "ClipLite store test with a long searchable suffix";
    if (!store.append(ClipType::Text, text, clipLiteHash(text))) return 2;
    if (store.activeCount() != 1) return 3;
    std::string restored;
    if (!store.readPayload(0, restored) || restored != text) return 4;
    if (store.search("searchable suffix").size() != 1) return 5;
    if (!store.togglePinned(0) || !store.items()[0].pinned) return 6;
    if (!store.setCategory(0, 2) || store.items()[0].category != 2) return 7;
    if (!store.remove(0) || store.activeCount() != 0) return 8;

    ClipStore formats(10);
    formats.open();
    formats.clear();
    if (!formats.append(ClipType::Html, "<b>html</b>", clipLiteHash("<b>html</b>"))) return 23;
    if (!formats.append(ClipType::ImageV5, "dibv5", clipLiteHash("dibv5"))) return 24;
    ClipStore formatsReopened(10);
    if (!formatsReopened.open() || formatsReopened.activeCount() != 2 ||
        formatsReopened.items()[0].type != ClipType::ImageV5 ||
        formatsReopened.items()[1].type != ClipType::Html) return 25;
    formatsReopened.clear();
    formats.clear();

    if (!store.append(ClipType::Text, "older", clipLiteHash("older"))) return 9;
    if (!store.append(ClipType::Text, "newer", clipLiteHash("newer"))) return 10;
    ClipStore reopened(10);
    if (!reopened.open() || reopened.activeCount() != 2 || reopened.items()[0].preview != "newer") return 11;
    reopened.clear();
    store.clear();

    ClipStore damaged(10);
    damaged.open();
    damaged.clear();
    if (!damaged.append(ClipType::Text, "keep", clipLiteHash("keep"))) return 12;
    if (!damaged.append(ClipType::Text, "truncate", clipLiteHash("truncate"))) return 13;
    std::FILE* raw = nullptr;
    _wfopen_s(&raw, damaged.path().c_str(), L"r+b");
    if (!raw) return 14;
    if (_fseeki64(raw, -2, SEEK_END) != 0) return 15;
    const auto truncatedSize = _ftelli64(raw);
    if (_chsize_s(_fileno(raw), static_cast<__int64>(truncatedSize)) != 0) return 16;
    std::fclose(raw);

    ClipStore recovered(10);
    if (!recovered.open() || recovered.activeCount() != 1) return 17;
    if (!recovered.readPayload(0, restored) || restored != "keep") return 18;
    recovered.clear();

    ClipStore corrupted(10);
    corrupted.open();
    corrupted.clear();
    if (!corrupted.append(ClipType::Text, "checksum", clipLiteHash("checksum"))) return 19;
    _wfopen_s(&raw, corrupted.path().c_str(), L"r+b");
    if (!raw || _fseeki64(raw, -1, SEEK_END) != 0) return 20;
    int lastByte = std::fgetc(raw);
    if (lastByte == EOF || _fseeki64(raw, -1, SEEK_CUR) != 0) return 21;
    std::fputc(lastByte ^ 0x01, raw);
    std::fclose(raw);
    ClipStore checksumRejected(10);
    if (!checksumRejected.open() || checksumRejected.activeCount() != 0) return 22;
    checksumRejected.clear();
    return 0;
}
