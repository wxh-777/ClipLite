#include "clip_store.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <io.h>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct TestDiskHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint8_t type;
    std::uint8_t flags;
    std::uint64_t timestamp;
    std::uint64_t hash;
    std::uint32_t category;
    std::uint32_t payloadSize;
    std::uint32_t payloadCrc;
    std::uint32_t sourceSize;
    std::uint32_t sourceCrc;
    std::uint64_t expiresAt;
};

struct TestDiskMetadata {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t size;
    std::uint64_t recordId;
    std::uint64_t createdAt;
    std::uint64_t lastCopiedAt;
    std::uint64_t lastUsedAt;
    std::uint64_t useCount;
    std::uint64_t contentSize;
    std::uint64_t copyCount;
    std::uint32_t crc;
};

struct TestStoredHtmlHeader {
    std::uint32_t magic;
    std::uint32_t textSize;
    std::uint32_t htmlSize;
};
#pragma pack(pop)

struct TestDataScope {
    std::wstring path;

    TestDataScope() {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetTempPathW(MAX_PATH, buffer);
        path.assign(buffer, length);
        if (!path.empty() && path.back() != L'\\') path.push_back(L'\\');
        path += L"ClipLiteStoreTests-" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(path.c_str(), nullptr);
        _wputenv_s(L"CLIPLITE_TEST_DATA_DIR", path.c_str());
    }

    ~TestDataScope() {
        DeleteFileW((path + L"\\history.bin").c_str());
        DeleteFileW((path + L"\\history.bin.tmp").c_str());
        _wputenv_s(L"CLIPLITE_TEST_DATA_DIR", L"");
        RemoveDirectoryW(path.c_str());
    }
};

int main() {
    TestDataScope testData;
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
    if (store.items()[0].recordId == 0 || store.items()[0].createdAt == 0 ||
        store.items()[0].lastCopiedAt == 0 ||
        store.items()[0].lastUsedAt != 0 || store.items()[0].useCount != 0 ||
        store.items()[0].copyCount != 1 || store.items()[0].contentSize != text.size()) return 88;
    if (store.countType(ClipType::Text) != 1 || store.bytesType(ClipType::Text) != text.size()) return 44;
    if (store.items()[0].source != "VS Code") return 40;
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
    if (formatsReopened.items()[1].preview != "html") return 255;
    if (formatsReopened.countType(ClipType::Text) != 1 ||
        formatsReopened.bytesType(ClipType::Text) != std::string("<b>html</b>").size()) return 256;
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

    ClipStore previewBoundary(10);
    previewBoundary.open();
    previewBoundary.clear();
    const std::string boundaryText = std::string(255, 'a') + u8"中";
    if (!previewBoundary.append(ClipType::Text, boundaryText, clipLiteHash(boundaryText)) ||
        previewBoundary.items()[0].preview != std::string(255, 'a')) return 84;
    ClipStore previewBoundaryReopened(10);
    if (!previewBoundaryReopened.open()) return 851;
    if (previewBoundaryReopened.items().size() != 1) return 852;
    if (previewBoundaryReopened.items()[0].preview != std::string(255, 'a')) return 85;
    previewBoundaryReopened.clear();
    previewBoundary.clear();

    ClipStore previewWhitespace(10);
    previewWhitespace.open();
    previewWhitespace.clear();
    const std::string whitespaceText = "first\r\nsecond\tthird\nfourth";
    if (!previewWhitespace.append(ClipType::Text, whitespaceText,
                                  clipLiteHash(whitespaceText)) ||
        previewWhitespace.items()[0].preview != whitespaceText) return 86;
    ClipStore previewWhitespaceReopened(10);
    if (!previewWhitespaceReopened.open() || previewWhitespaceReopened.items().size() != 1 ||
        previewWhitespaceReopened.items()[0].preview != whitespaceText) return 87;
    previewWhitespaceReopened.clear();
    previewWhitespace.clear();

    const std::string visibleHtmlText = "personal token";
    const std::string hiddenHtml = "<p>personal token</p><span data-offset=\"23\"></span>";
    const TestStoredHtmlHeader htmlHeader{0x314D5448,
                                          static_cast<std::uint32_t>(visibleHtmlText.size()),
                                          static_cast<std::uint32_t>(hiddenHtml.size())};
    std::string searchableHtml(reinterpret_cast<const char*>(&htmlHeader), sizeof(htmlHeader));
    searchableHtml += visibleHtmlText;
    searchableHtml += hiddenHtml;
    ClipStore htmlSearch(10);
    htmlSearch.open();
    htmlSearch.clear();
    if (!htmlSearch.append(ClipType::Html, searchableHtml, clipLiteHash(searchableHtml)) ||
        htmlSearch.search("personal").size() != 1 || htmlSearch.search("23").size() != 0) return 83;
    htmlSearch.clear();

    ClipStore streamedSearch(10);
    streamedSearch.open();
    streamedSearch.clear();
    std::string streamedPayload(64 * 1024 - 3, 'x');
    streamedPayload += "Needle";
    streamedPayload += std::string(32, 'y');
    if (!streamedSearch.append(ClipType::Text, streamedPayload,
                              clipLiteHash(streamedPayload)) ||
        streamedSearch.search("NEEDLE").size() != 1) return 60;
    streamedSearch.clear();

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

    ClipStore categoryLimited(10);
    categoryLimited.open();
    categoryLimited.clear();
    if (!categoryLimited.append(ClipType::Text, "text-one", clipLiteHash("text-one")) ||
        !categoryLimited.append(ClipType::Text, "text-two", clipLiteHash("text-two")) ||
        !categoryLimited.append(ClipType::Image, "image-one", clipLiteHash("image-one"))) return 55;
    if (!categoryLimited.pruneCategory(ClipType::Text, 1, 0) ||
        categoryLimited.countType(ClipType::Text) != 1 || categoryLimited.countType(ClipType::Image) != 1) return 56;
    categoryLimited.clear();

    ClipStore textCategory(10);
    textCategory.open();
    textCategory.clear();
    if (!textCategory.append(ClipType::Text, "plain", clipLiteHash("plain")) ||
        !textCategory.append(ClipType::Html, "<b>rich</b>", clipLiteHash("<b>rich</b>"))) return 57;
    if (textCategory.countType(ClipType::Text) != 2) return 58;
    if (!textCategory.clearType(ClipType::Text) || textCategory.activeCount() != 0) return 59;

    ClipStore merged(10);
    merged.open();
    merged.clear();
    const std::uint64_t sameTextHash = clipLiteHash("same text");
    if (!merged.appendOrUpdate(ClipType::Text, "same text", sameTextHash, "Editor")) return 61;
    if (!merged.appendOrUpdate(ClipType::Text, "same text", sameTextHash, {})) return 62;
    if (merged.activeCount() != 1 || merged.items()[0].source != "Editor" ||
        merged.items()[0].copyCount != 2) return 63;
    if (!merged.appendOrUpdate(ClipType::Html, "<b>same text</b>", sameTextHash, "Browser") ||
        merged.activeCount() != 1 || merged.items()[0].type != ClipType::Html ||
        merged.items()[0].copyCount != 3) return 64;
    if (!merged.readPayload(0, restored) || restored != "<b>same text</b>") return 65;
    if (!merged.appendOrUpdate(ClipType::Text, "different", clipLiteHash("different"), "Other") ||
        merged.activeCount() != 2) return 66;
    ClipStore mergedReopened(10);
    if (!mergedReopened.open() || mergedReopened.activeCount() != 2 ||
        mergedReopened.items()[0].type != ClipType::Text ||
        mergedReopened.items()[1].type != ClipType::Html) return 67;
    mergedReopened.clear();
    merged.clear();

    ClipStore ordering(10);
    ordering.open();
    ordering.clear();
    if (!ordering.append(ClipType::Text, "first", clipLiteHash("first")) ||
        !ordering.append(ClipType::Text, "second", clipLiteHash("second")) ||
        !ordering.append(ClipType::Text, "third", clipLiteHash("third")) ||
        ordering.items()[0].preview != "third") return 68;
    if (!ordering.togglePinned(0)) return 69;
    ClipStore orderingReopened(10);
    if (!orderingReopened.open() || orderingReopened.items().size() != 3 ||
        orderingReopened.items()[0].preview != "third" ||
        orderingReopened.items()[1].preview != "second" ||
        orderingReopened.items()[2].preview != "first") return 70;
    if (!orderingReopened.appendOrUpdate(ClipType::Text, "second", clipLiteHash("second")) ||
        orderingReopened.items()[0].preview != "second" ||
        orderingReopened.items()[1].preview != "third" ||
        orderingReopened.items()[2].preview != "first") return 71;
    ClipStore orderingReopenedAgain(10);
    if (!orderingReopenedAgain.open() || orderingReopenedAgain.items().size() != 3 ||
        orderingReopenedAgain.items()[0].preview != "second" ||
        orderingReopenedAgain.items()[1].preview != "third" ||
        orderingReopenedAgain.items()[2].preview != "first") return 72;
    orderingReopenedAgain.clear();
    orderingReopened.clear();
    ordering.clear();

    ClipStore usage(10);
    usage.setSortByLastUsed(true);
    usage.open();
    usage.clear();
    if (!usage.append(ClipType::Text, "usage-old", clipLiteHash("usage-old")) ||
        !usage.append(ClipType::Text, "usage-new", clipLiteHash("usage-new"))) return 89;
    if (!usage.recordUse(1, true) || usage.items()[0].preview != "usage-old" ||
        usage.items()[0].useCount != 1 || usage.items()[0].lastUsedAt == 0) return 90;
    ClipStore usageReopened(10);
    usageReopened.setSortByLastUsed(true);
    if (!usageReopened.open() || usageReopened.items()[0].preview != "usage-old" ||
        usageReopened.items()[0].useCount != 1 ||
        usageReopened.items()[0].contentSize != std::string("usage-old").size()) return 91;
    if (!usageReopened.recordUse(0, false) || usageReopened.items()[0].useCount != 2 ||
        usageReopened.items()[0].preview != "usage-old") return 92;
    usageReopened.clear();
    usage.clear();

    ClipStore legacyOrdering(10);
    legacyOrdering.open();
    legacyOrdering.clear();
    if (!legacyOrdering.append(ClipType::Text, "older", clipLiteHash("older"))) return 73;
    Sleep(1100);
    if (!legacyOrdering.append(ClipType::Text, "newer", clipLiteHash("newer"))) return 74;
    std::FILE* legacyFile = nullptr;
    _wfopen_s(&legacyFile, legacyOrdering.path().c_str(), L"rb");
    if (!legacyFile) return 75;
    if (_fseeki64(legacyFile, 0, SEEK_END) != 0) return 76;
    const __int64 legacySize = _ftelli64(legacyFile);
    if (legacySize <= static_cast<__int64>(sizeof(TestDiskHeader)) ||
        _fseeki64(legacyFile, 0, SEEK_SET) != 0) return 77;
    std::vector<char> legacyBytes(static_cast<std::size_t>(legacySize));
    if (std::fread(legacyBytes.data(), 1, legacyBytes.size(), legacyFile) != legacyBytes.size()) return 78;
    std::fclose(legacyFile);
    TestDiskHeader firstHeader{};
    TestDiskHeader secondHeader{};
    std::memcpy(&firstHeader, legacyBytes.data(), sizeof(firstHeader));
    const std::size_t firstSize = sizeof(firstHeader) + sizeof(TestDiskMetadata) +
        firstHeader.sourceSize + firstHeader.payloadSize;
    if (firstSize >= legacyBytes.size()) return 79;
    std::memcpy(&secondHeader, legacyBytes.data() + firstSize, sizeof(secondHeader));
    const std::size_t secondSize = sizeof(secondHeader) + sizeof(TestDiskMetadata) +
        secondHeader.sourceSize + secondHeader.payloadSize;
    if (firstSize + secondSize != legacyBytes.size()) return 80;
    std::vector<char> reversedBytes;
    reversedBytes.insert(reversedBytes.end(), legacyBytes.begin() + firstSize, legacyBytes.end());
    reversedBytes.insert(reversedBytes.end(), legacyBytes.begin(), legacyBytes.begin() + firstSize);
    _wfopen_s(&legacyFile, legacyOrdering.path().c_str(), L"wb");
    if (!legacyFile || std::fwrite(reversedBytes.data(), 1, reversedBytes.size(), legacyFile) != reversedBytes.size() ||
        std::fclose(legacyFile) != 0) return 81;
    ClipStore repairedOrdering(10);
    if (!repairedOrdering.open() || repairedOrdering.items().size() != 2 ||
        repairedOrdering.items()[0].preview != "newer" ||
        repairedOrdering.items()[1].preview != "older") return 82;
    repairedOrdering.clear();
    legacyOrdering.clear();

    ClipStore pressure(10000);
    pressure.open();
    pressure.clear();
    for (int i = 0; i < 10000; ++i) {
        const std::string value = "pressure-" + std::to_string(i);
        if (!pressure.append(ClipType::Text, value, clipLiteHash(value))) return 32;
    }
    if (pressure.activeCount() != 10000) return 33;
    constexpr std::size_t pinnedPressureIndex = 5000;
    if (!pressure.togglePinned(pinnedPressureIndex) ||
        !pressure.items()[pinnedPressureIndex].pinned) return 84;
    ClipStore pressureReopened(10000);
    if (!pressureReopened.open() || pressureReopened.activeCount() != 10000 ||
        !pressureReopened.items()[pinnedPressureIndex].pinned) return 34;
    pressureReopened.clear();
    pressure.clear();
    return 0;
}
