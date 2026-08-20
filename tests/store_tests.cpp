#include "clip_store.h"

#include <windows.h>

#include <cstdio>
#include <io.h>
#include <string>

int main() {
    ClipStore store(10);
    if (!store.open()) return 1;
    store.clear();
    const std::wstring tempPath = store.path() + L".tmp";
    std::FILE* tempFile = nullptr;
    _wfopen_s(&tempFile, tempPath.c_str(), L"wb");
    if (!tempFile) return 46;
    std::fputs("stale temporary data", tempFile);
    std::fclose(tempFile);
    ClipStore tempRecovered(10);
    if (!tempRecovered.open() || GetFileAttributesW(tempPath.c_str()) != INVALID_FILE_ATTRIBUTES) return 47;
    if (store.append(ClipType::Text, {}, 1)) return 30;
    std::string oversized(32u * 1024u * 1024u + 1, 'x');
    if (store.append(ClipType::Text, oversized, clipLiteHash(oversized))) return 31;
    ClipStore payloadLimited(10);
    payloadLimited.setMaxPayloadBytes(4);
    payloadLimited.open();
    payloadLimited.clear();
    if (payloadLimited.append(ClipType::Text, "12345", clipLiteHash("12345"))) return 42;
    if (!payloadLimited.append(ClipType::Text, "1234", clipLiteHash("1234"))) return 43;
    payloadLimited.clear();
    ClipStore countLimited(0);
    countLimited.setMaxItems(2);
    countLimited.open();
    countLimited.clear();
    if (!countLimited.append(ClipType::Text, "a", clipLiteHash("a")) ||
        !countLimited.append(ClipType::Text, "b", clipLiteHash("b")) ||
        !countLimited.append(ClipType::Text, "c", clipLiteHash("c")) ||
        countLimited.activeCount() != 2) return 45;
    countLimited.clear();
    ClipStore expiry(10);
    expiry.open();
    expiry.clear();
    if (!expiry.append(ClipType::Text, "expire", clipLiteHash("expire"), {}, 1)) return 48;
    if (expiry.items()[0].expiresAt != 1 || !expiry.pruneExpired(2) || expiry.activeCount() != 0) return 49;
    const std::string text = "ClipLite store test with a long searchable suffix";
    if (!store.append(ClipType::Text, text, clipLiteHash(text), "VS Code")) return 2;
    if (store.activeCount() != 1) return 3;
    if (store.countType(ClipType::Text) != 1 || store.bytesType(ClipType::Text) != text.size()) return 44;
    if (!store.items()[0].hasSource || store.items()[0].source != "VS Code") return 40;
    std::string restored;
    if (!store.readPayload(0, restored) || restored != text) return 4;
    if (store.search("searchable suffix").size() != 1) return 5;
    if (!store.togglePinned(0) || !store.items()[0].pinned) return 6;
    if (!store.setCategory(0, 2) || store.items()[0].category != 2) return 7;
    if (!store.remove(0) || store.activeCount() != 0) return 8;

    ClipStore formats(10);
    formats.open();
    formats.clear();
    if (!formats.append(ClipType::Html, "<b>html</b>", clipLiteHash("<b>html</b>"), "Word")) return 23;
    if (!formats.append(ClipType::ImageV5, "dibv5", clipLiteHash("dibv5"))) return 24;
    ClipStore formatsReopened(10);
    if (!formatsReopened.open()) return 250;
    if (formatsReopened.activeCount() != 2) return 251;
    if (formatsReopened.items()[0].type != ClipType::ImageV5) return 252;
    if (formatsReopened.items()[1].type != ClipType::Html) return 253;
    if (formatsReopened.items()[1].source != "Word") return 254;
    if (!formatsReopened.clearType(ClipType::Html) || formatsReopened.activeCount() != 1 ||
        formatsReopened.items()[0].type != ClipType::ImageV5) return 41;
    formatsReopened.clear();
    formats.clear();

    ClipStore unicode(10);
    unicode.open();
    unicode.clear();
    const std::string chinese = u8"剪贴板搜索";
    if (!unicode.append(ClipType::Text, chinese, clipLiteHash(chinese))) return 38;
    if (unicode.search(u8"贴板").size() != 1) return 39;
    unicode.clear();

    if (!store.append(ClipType::Text, "older", clipLiteHash("older"))) return 9;
    if (!store.append(ClipType::Text, "newer", clipLiteHash("newer"))) return 10;
    ClipStore reopened(10);
    if (!reopened.open() || reopened.activeCount() != 2 || reopened.items()[0].preview != "newer" ||
        reopened.items()[0].source != "") return 11;
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

    ClipStore secure(10);
    secure.setEncryption(true);
    secure.open();
    secure.clear();
    const std::string secret = "user secret";
    if (!secure.append(ClipType::Text, secret, clipLiteHash(secret))) return 35;
    ClipStore secureReopened(10);
    secureReopened.setEncryption(true);
    if (!secureReopened.open() || !secureReopened.readPayload(0, restored) || restored != secret) return 36;
    if (!secureReopened.rekey(false) || !secureReopened.readPayload(0, restored) || restored != secret) return 37;
    secureReopened.clear();
    secure.clear();

    ClipStore limited(10);
    limited.open();
    limited.clear();
    if (!limited.append(ClipType::Text, "one", clipLiteHash("one"))) return 26;
    if (!limited.append(ClipType::Text, "two", clipLiteHash("two"))) return 27;
    if (!limited.append(ClipType::Text, "three", clipLiteHash("three"))) return 28;
    if (!limited.prune(2, 0, 0) || limited.activeCount() != 2) return 29;
    limited.clear();

    ClipStore pressure(10000);
    pressure.open();
    pressure.clear();
    for (int i = 0; i < 10000; ++i) {
        const std::string value = "pressure-" + std::to_string(i);
        if (!pressure.append(ClipType::Text, value, clipLiteHash(value))) return 32;
    }
    if (pressure.activeCount() != 10000) return 33;
    ClipStore pressureReopened(10000);
    if (!pressureReopened.open() || pressureReopened.activeCount() != 10000) return 34;
    pressureReopened.clear();
    pressure.clear();
    return 0;
}
