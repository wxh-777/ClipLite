#include "clip_store.h"
#include "thumbnail_cache.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <io.h>
#include <atomic>
#include <string>
#include <thread>
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

std::uint32_t testMetadataCrc(const void* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

std::string makeTestImagePayload(std::uint32_t marker, std::uint32_t width = 64,
                                 std::uint32_t height = 64) {
    const std::size_t pixelBytes = static_cast<std::size_t>(width) * height * sizeof(std::uint32_t);
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(BITMAPV5HEADER);
    header.bV5Width = static_cast<LONG>(width);
    header.bV5Height = static_cast<LONG>(height);
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = static_cast<DWORD>(pixelBytes);
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;
    std::string payload(sizeof(header) + pixelBytes, '\0');
    std::memcpy(payload.data(), &header, sizeof(header));
    for (std::uint32_t index = 0; index < width * height; ++index) {
        const std::uint32_t color = 0xFF000000u | ((marker + index) & 0x00FFFFFFu);
        std::memcpy(payload.data() + sizeof(header) + index * sizeof(color),
                    &color, sizeof(color));
    }
    return payload;
}

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
        DeleteFileW((path + L"\\history.bin.txn").c_str());
        DeleteFileW((path + L"\\history.bin.txn.tmp").c_str());
        ThumbnailCache thumbnails;
        if (thumbnails.setDirectory(path)) {
            thumbnails.clear();
        }
        _wputenv_s(L"CLIPLITE_TEST_DATA_DIR", L"");
        RemoveDirectoryW(path.c_str());
    }
};

int main() {
    TestDataScope testData;
    ClipStore store(10);
    if (!store.open()) return 1;
    store.clear();
    ThumbnailCache thumbnailCache;
    if (!thumbnailCache.setDirectory(testData.path)) return 2;
    const std::string thumbnailData("png-thumbnail-data");
    if (!thumbnailCache.write(1, false, thumbnailData)) return 3;
    std::string restoredThumbnail;
    if (!thumbnailCache.read(1, false, restoredThumbnail) || restoredThumbnail != thumbnailData) return 4;
    if (!thumbnailCache.write(2, true, thumbnailData)) return 5;
    restoredThumbnail.clear();
    if (!thumbnailCache.read(2, true, restoredThumbnail) || restoredThumbnail != thumbnailData) return 6;
    thumbnailCache.unloadIndex();
    restoredThumbnail.clear();
    if (!thumbnailCache.read(1, false, restoredThumbnail) || restoredThumbnail != thumbnailData) return 48;
    thumbnailCache.unloadIndex();
    restoredThumbnail.clear();
    if (!thumbnailCache.read(2, true, restoredThumbnail) || restoredThumbnail != thumbnailData) return 49;
    if (GetFileAttributesW((testData.path + L"\\thumbnails.bin").c_str()) == INVALID_FILE_ATTRIBUTES) {
        return 7;
    }
    std::atomic<bool> concurrentCacheFailure{false};
    std::vector<std::thread> cacheWorkers;
    for (std::uint64_t worker = 0; worker < 4; ++worker) {
        cacheWorkers.emplace_back([&thumbnailCache, &concurrentCacheFailure, worker]() {
            for (std::uint64_t index = 0; index < 100; ++index) {
                const std::uint64_t key = 1000 + worker * 100 + index;
                const std::string data = "thumbnail-" + std::to_string(key);
                std::string restored;
                if (!thumbnailCache.write(key, false, data) ||
                    !thumbnailCache.read(key, false, restored) || restored != data) {
                    concurrentCacheFailure.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (std::thread& worker : cacheWorkers) worker.join();
    if (concurrentCacheFailure.load(std::memory_order_relaxed)) return 8;
    std::FILE* damagedThumbnail = nullptr;
    _wfopen_s(&damagedThumbnail, (testData.path + L"\\thumbnails.bin").c_str(), L"ab");
    if (!damagedThumbnail) return 50;
    const char incompleteHeader[] = "bad";
    const bool wroteTail = std::fwrite(incompleteHeader, 1, sizeof(incompleteHeader),
                                       damagedThumbnail) == sizeof(incompleteHeader);
    std::fclose(damagedThumbnail);
    if (!wroteTail) return 51;
    thumbnailCache.unloadIndex();
    restoredThumbnail.clear();
    if (!thumbnailCache.read(2, true, restoredThumbnail) || restoredThumbnail != thumbnailData) return 52;
    thumbnailCache.unloadIndex();
    restoredThumbnail.clear();
    if (!thumbnailCache.read(1, false, restoredThumbnail) || restoredThumbnail != thumbnailData) return 53;
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
    if (!expiry.append(ClipType::Text, "pinned-expires", clipLiteHash("pinned-expires"), {}, 1) ||
        !expiry.togglePinned(0) || !expiry.pruneExpired(2) || expiry.activeCount() != 0) return 100;
    if (!expiry.append(ClipType::Text, "protected-pin", clipLiteHash("protected-pin"), {}, 1) ||
        !expiry.togglePinned(0)) return 101;
    expiry.setProtectPinnedFromAutoCleanup(true);
    if (!expiry.pruneExpired(2) || expiry.activeCount() != 1 || !expiry.items()[0].pinned) return 102;
    if (!expiry.toggleFavorite(0) || !expiry.items()[0].favorite || !expiry.items()[0].pinned) return 103;
    if (!expiry.append(ClipType::Text, "pinned-expire", clipLiteHash("pinned-expire"), {}, 1) ||
        !expiry.pruneExpired(2) || expiry.activeCount() != 1) return 94;
    ClipStore dualStateReopened(10);
    if (!dualStateReopened.open() || dualStateReopened.activeCount() != 1 ||
        !dualStateReopened.items()[0].favorite || !dualStateReopened.items()[0].pinned) return 110;
    dualStateReopened.clear();
    expiry.clear();

    ClipStore pinCleanupDefault(10);
    pinCleanupDefault.open();
    pinCleanupDefault.clear();
    if (!pinCleanupDefault.append(ClipType::Text, "pin-default", clipLiteHash("pin-default"), {}, 1) ||
        !pinCleanupDefault.togglePinned(0) || !pinCleanupDefault.pruneExpired(2) ||
        pinCleanupDefault.activeCount() != 0) return 104;
    pinCleanupDefault.clear();

    ClipStore pinnedUpdate(10);
    pinnedUpdate.open();
    pinnedUpdate.clear();
    const std::uint64_t pinnedHash = clipLiteHash("pinned-original");
    std::string pinnedRestored;
    if (!pinnedUpdate.append(ClipType::Text, "pinned-original", pinnedHash) ||
        !pinnedUpdate.toggleFavorite(0) ||
        pinnedUpdate.appendOrUpdate(ClipType::Text, "pinned-replacement", pinnedHash) ||
        pinnedUpdate.activeCount() != 1 || !pinnedUpdate.items()[0].favorite ||
        !pinnedUpdate.readPayload(0, pinnedRestored) || pinnedRestored != "pinned-original") return 95;
    pinnedUpdate.clear();

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
    if (!store.toggleFavorite(0) || !store.items()[0].favorite) return 6;
    if (!store.setCategory(0, 2) || store.items()[0].category != 2) return 7;
    if (!store.remove(0) || store.activeCount() != 0) return 8;

    ClipStore removeMany(10);
    removeMany.open();
    removeMany.clear();
    if (!removeMany.append(ClipType::Text, "keep-old", clipLiteHash("keep-old")) ||
        !removeMany.append(ClipType::Image, "remove", clipLiteHash("remove")) ||
        !removeMany.append(ClipType::Text, "keep-new", clipLiteHash("keep-new")) ||
        !removeMany.remove(1) || removeMany.activeCount() != 2) return 85;
    ClipStore removeManyReopened(10);
    if (!removeManyReopened.open() || removeManyReopened.activeCount() != 2 ||
        removeManyReopened.countType(ClipType::Image) != 0 ||
        removeManyReopened.items()[0].preview != "keep-new" ||
        removeManyReopened.items()[1].preview != "keep-old") return 86;
    removeManyReopened.clear();
    removeMany.clear();

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

    const std::string longHtmlText(5000, 'x');
    const std::string longHtmlBody = "<p>formatting</p>";
    const TestStoredHtmlHeader longHtmlHeader{0x314D5448,
                                              static_cast<std::uint32_t>(longHtmlText.size()),
                                              static_cast<std::uint32_t>(longHtmlBody.size())};
    std::string longHtmlPayload(reinterpret_cast<const char*>(&longHtmlHeader),
                                sizeof(longHtmlHeader));
    longHtmlPayload += longHtmlText;
    longHtmlPayload += longHtmlBody;
    ClipStore longHtml(10);
    longHtml.open();
    longHtml.clear();
    if (!longHtml.append(ClipType::Html, longHtmlPayload, clipLiteHash(longHtmlPayload)) ||
        longHtml.items()[0].preview != std::string(256, 'x')) return 264;
    ClipStore longHtmlReopened(10);
    if (!longHtmlReopened.open() || longHtmlReopened.activeCount() != 1 ||
        longHtmlReopened.items()[0].preview != std::string(256, 'x')) return 265;
    longHtmlReopened.clear();
    longHtml.clear();

    ClipStore textEditing(10);
    textEditing.open();
    textEditing.clear();
    const std::string originalText = "original text";
    if (!textEditing.append(ClipType::Text, originalText, clipLiteHash(originalText), "Editor") ||
        !textEditing.toggleFavorite(0) || !textEditing.togglePinned(0)) return 257;
    const std::uint64_t originalRecordId = textEditing.items()[0].recordId;
    if (!textEditing.replaceWithText(originalRecordId, u8"修改后的文本")) return 258;
    std::string editedPayload;
    if (textEditing.activeCount() != 1 || textEditing.items()[0].recordId != originalRecordId ||
        textEditing.items()[0].type != ClipType::Text || !textEditing.items()[0].favorite ||
        !textEditing.items()[0].pinned || !textEditing.readPayload(0, editedPayload) ||
        editedPayload != u8"修改后的文本") return 259;
    ClipStore textEditingReopened(10);
    if (!textEditingReopened.open() || textEditingReopened.activeCount() != 1 ||
        textEditingReopened.items()[0].recordId != originalRecordId ||
        !textEditingReopened.readPayload(0, editedPayload) ||
        editedPayload != u8"修改后的文本") return 260;
    textEditingReopened.clear();
    textEditing.clear();

    ClipStore uniqueEditedText(10);
    uniqueEditedText.open();
    uniqueEditedText.clear();
    const std::string duplicateVisibleText = "same visible text";
    const std::string duplicateHtml = "<p>same visible text</p>";
    if (!uniqueEditedText.append(ClipType::Html, duplicateHtml,
                                 clipLiteHash(duplicateVisibleText), "Browser")) return 261;
    const std::uint64_t htmlRecordId = uniqueEditedText.items()[0].recordId;
    std::uint64_t savedRecordId = 0;
    if (!uniqueEditedText.appendUniqueText(duplicateVisibleText, "Editor", 0, savedRecordId) ||
        savedRecordId != htmlRecordId || uniqueEditedText.activeCount() != 1 ||
        uniqueEditedText.items()[0].type != ClipType::Text ||
        !uniqueEditedText.readPayload(0, editedPayload) || editedPayload != duplicateVisibleText) {
        return 262;
    }
    if (!uniqueEditedText.appendUniqueText(duplicateVisibleText, "Editor", 0, savedRecordId) ||
        savedRecordId != htmlRecordId || uniqueEditedText.activeCount() != 1) return 263;
    uniqueEditedText.clear();

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
    if (!categoryLimited.pruneCategory(ClipType::Image, 0, 1) ||
        categoryLimited.countType(ClipType::Image) != 0) return 57;
    categoryLimited.clear();

    ClipStore pinnedLimit(10);
    pinnedLimit.open();
    pinnedLimit.clear();
    if (!pinnedLimit.append(ClipType::Text, "pinned", clipLiteHash("pinned")) ||
        !pinnedLimit.toggleFavorite(0) ||
        !pinnedLimit.append(ClipType::Text, "normal", clipLiteHash("normal"))) return 73;
    if (!pinnedLimit.pruneCategory(ClipType::Text, 1, 1) ||
        pinnedLimit.activeCount() != 1 || !pinnedLimit.items()[0].favorite) return 74;
    if (!pinnedLimit.append(ClipType::Text, "normal-two", clipLiteHash("normal-two")) ||
        !pinnedLimit.clearTypeUnfavorited(ClipType::Text) ||
        pinnedLimit.activeCount() != 1 || !pinnedLimit.items()[0].favorite) return 75;
    if (!pinnedLimit.clearType(ClipType::Text) || pinnedLimit.activeCount() != 0) return 76;
    pinnedLimit.clear();

    ClipStore appendLimit(2);
    appendLimit.open();
    appendLimit.clear();
    if (!appendLimit.append(ClipType::Text, "keep-pinned", clipLiteHash("keep-pinned")) ||
        !appendLimit.toggleFavorite(0) ||
        !appendLimit.append(ClipType::Text, "first-normal", clipLiteHash("first-normal")) ||
        appendLimit.activeCount() != 2 || !appendLimit.items()[1].favorite ||
        !appendLimit.append(ClipType::Text, "second-normal", clipLiteHash("second-normal")) ||
        appendLimit.activeCount() != 2 || !appendLimit.items()[1].favorite ||
        appendLimit.items()[0].preview != "second-normal") return 93;
    appendLimit.clear();

    ClipStore limitReopened(2);
    if (!limitReopened.open() || !limitReopened.append(ClipType::Text, "old", clipLiteHash("old")) ||
        !limitReopened.append(ClipType::Text, "new", clipLiteHash("new")) ||
        !limitReopened.append(ClipType::Text, "latest", clipLiteHash("latest")) ||
        limitReopened.activeCount() != 2 || limitReopened.items()[0].preview != "latest" ||
        limitReopened.items()[1].preview != "new") return 98;
    ClipStore limitReopenedAgain(2);
    if (!limitReopenedAgain.open() || limitReopenedAgain.activeCount() != 2 ||
        limitReopenedAgain.items()[0].preview != "latest" ||
        limitReopenedAgain.items()[1].preview != "new") return 99;
    limitReopenedAgain.clear();
    limitReopened.clear();

    ClipStore loadLimit(0);
    loadLimit.open();
    loadLimit.clear();
    if (!loadLimit.append(ClipType::Text, "load-old", clipLiteHash("load-old")) ||
        !loadLimit.append(ClipType::Text, "load-middle", clipLiteHash("load-middle")) ||
        !loadLimit.append(ClipType::Text, "load-new", clipLiteHash("load-new")) ||
        !loadLimit.togglePinned(2)) return 96;
    ClipStore loadLimitReopened(1);
    if (!loadLimitReopened.open() || loadLimitReopened.activeCount() != 3 ||
        !loadLimitReopened.items()[0].pinned ||
        loadLimitReopened.items()[0].preview != "load-old") return 97;
    loadLimitReopened.clear();
    loadLimit.clear();

    ClipStore allUnpinned(10);
    allUnpinned.open();
    allUnpinned.clear();
    if (!allUnpinned.append(ClipType::Text, "pinned-all", clipLiteHash("pinned-all")) ||
        !allUnpinned.toggleFavorite(0) ||
        !allUnpinned.append(ClipType::Text, "normal-all", clipLiteHash("normal-all")) ||
        !allUnpinned.clearUnfavorited() || allUnpinned.activeCount() != 1 ||
        !allUnpinned.items()[0].favorite) return 77;
    if (!allUnpinned.clear() || allUnpinned.activeCount() != 0) return 78;
    allUnpinned.clear();

    ClipStore textCategory(10);
    textCategory.open();
    textCategory.clear();
    if (!textCategory.append(ClipType::Text, "plain", clipLiteHash("plain")) ||
        !textCategory.append(ClipType::Html, "<b>rich</b>", clipLiteHash("<b>rich</b>"))) return 79;
    if (textCategory.countType(ClipType::Text) != 2) return 80;
    if (!textCategory.clearType(ClipType::Text) || textCategory.activeCount() != 0) return 81;

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
    if (!ordering.items()[0].pinned ||
        !ordering.append(ClipType::Text, "fourth", clipLiteHash("fourth")) ||
        !ordering.items()[0].pinned || ordering.items()[0].preview != "third") return 111;
    ClipStore orderingReopened(10);
    if (!orderingReopened.open() || orderingReopened.items().size() != 4 ||
        !orderingReopened.items()[0].pinned || orderingReopened.items()[0].preview != "third") return 70;
    if (!orderingReopened.appendOrUpdate(ClipType::Text, "second", clipLiteHash("second")) ||
        !orderingReopened.items()[0].pinned || orderingReopened.items()[0].preview != "third") return 71;
    ClipStore orderingReopenedAgain(10);
    if (!orderingReopenedAgain.open() || orderingReopenedAgain.items().size() != 4 ||
        !orderingReopenedAgain.items()[0].pinned ||
        orderingReopenedAgain.items()[0].preview != "third") return 72;
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

    ClipStore historyClear(10);
    if (!historyClear.open() || !historyClear.clear() ||
        !historyClear.clearUnfavorited(true)) return 150;
    for (int flags = 0; flags < 4; ++flags) {
        const std::string value = "clear-" + std::to_string(flags);
        if (!historyClear.append(ClipType::Text, value, clipLiteHash(value))) return 151;
        if ((flags & 1) && !historyClear.toggleFavorite(historyClear.findHash(clipLiteHash(value)))) return 152;
        if ((flags & 2) && !historyClear.togglePinned(historyClear.findHash(clipLiteHash(value)))) return 153;
    }
    const std::uint64_t clearRevision = historyClear.revision();
    HANDLE lockedHistory = CreateFileW(historyClear.path().c_str(), GENERIC_READ, FILE_SHARE_READ,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lockedHistory == INVALID_HANDLE_VALUE) return 154;
    const bool lockedClear = historyClear.clearUnfavorited(true);
    CloseHandle(lockedHistory);
    if (lockedClear || historyClear.activeCount() != 4 || historyClear.revision() != clearRevision) return 155;
    if (!historyClear.clearUnfavorited(true) || historyClear.activeCount() != 3 ||
        historyClear.findHash(clipLiteHash("clear-0")) != historyClear.activeCount()) return 156;
    ClipStore clearReopened(10);
    if (!clearReopened.open() || clearReopened.activeCount() != 3) return 157;
    for (const ClipItem& item : clearReopened.items()) {
        if ((!item.pinned && !item.favorite) || !clearReopened.readPayload(
            clearReopened.findHash(item.hash), restored) || restored != item.preview) return 158;
    }
    if (!clearReopened.clearUnfavorited() || clearReopened.activeCount() != 2) return 159;
    ClipStore favoritesReopened(10);
    if (!favoritesReopened.open() || favoritesReopened.activeCount() != 2) return 160;
    for (const ClipItem& item : favoritesReopened.items()) {
        if (!item.favorite) return 161;
    }
    if (!favoritesReopened.clear()) return 162;
    ClipStore emptyReopened(10);
    if (!emptyReopened.open() || emptyReopened.activeCount() != 0) return 163;

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
        !pressure.items()[0].pinned || pressure.items()[0].preview != "pressure-4999") return 84;
    std::FILE* compatibleHeaderFile = nullptr;
    _wfopen_s(&compatibleHeaderFile, pressure.path().c_str(), L"rb");
    std::uint8_t compatibleFlags = 0;
    if (!compatibleHeaderFile ||
        _fseeki64(compatibleHeaderFile,
                  static_cast<__int64>(pressure.items()[0].fileOffset +
                                       offsetof(TestDiskHeader, flags)), SEEK_SET) != 0 ||
        std::fread(&compatibleFlags, sizeof(compatibleFlags), 1, compatibleHeaderFile) != 1 ||
        std::fclose(compatibleHeaderFile) != 0 || (compatibleFlags & 0xF0u) != 0 ||
        (compatibleFlags & 1u) == 0) return 109;
    ClipStore pressureReopened(10000);
    if (!pressureReopened.open() || pressureReopened.activeCount() != 10000 ||
        !pressureReopened.items()[0].pinned ||
        pressureReopened.items()[0].preview != "pressure-4999") return 34;
    pressureReopened.clear();
    pressure.clear();

    ClipStore maintenance(1200);
    maintenance.open();
    maintenance.clear();
    const std::string maintenancePayload(12u * 1024u, 'm');
    for (int i = 0; i < 1000; ++i) {
        const std::string value = maintenancePayload + std::to_string(i);
        if (!maintenance.append(ClipType::Text, value, clipLiteHash(value))) return 164;
    }
    const std::uint64_t diskBeforeDeletes = maintenance.diskBytes();
    for (int i = 0; i < 800; ++i) {
        if (!maintenance.remove(0)) return 165;
    }
    if (maintenance.activeCount() != 200 || maintenance.diskBytes() != diskBeforeDeletes ||
        !maintenance.compactIfNeeded() || maintenance.diskBytes() >= diskBeforeDeletes) return 166;
    ClipStore maintenanceReopened(1200);
    if (!maintenanceReopened.open() || maintenanceReopened.activeCount() != 200) return 167;
    maintenanceReopened.clear();
    maintenance.clear();

    ClipStore imageMaintenance(1200);
    imageMaintenance.open();
    imageMaintenance.clear();
    for (int i = 0; i < 1000; ++i) {
        const std::string value = makeTestImagePayload(static_cast<std::uint32_t>(i));
        if (!imageMaintenance.append(ClipType::ImageV5, value, clipLiteHash(value))) return 169;
    }
    const std::uint64_t imageDiskBeforeDeletes = imageMaintenance.diskBytes();
    for (int i = 0; i < 800; ++i) {
        if (!imageMaintenance.remove(0)) return 170;
    }
    if (imageMaintenance.countType(ClipType::Image) != 200 ||
        imageMaintenance.diskBytes() != imageDiskBeforeDeletes ||
        !imageMaintenance.compactIfNeeded() ||
        imageMaintenance.diskBytes() >= imageDiskBeforeDeletes) return 171;
    ClipStore imageMaintenanceReopened(1200);
    if (!imageMaintenanceReopened.open() ||
        imageMaintenanceReopened.countType(ClipType::Image) != 200) return 172;
    imageMaintenanceReopened.clear();
    imageMaintenance.clear();

    wchar_t largeStorageFlag[8]{};
    const DWORD largeStorageFlagLength = GetEnvironmentVariableW(
        L"CLIPLITE_LARGE_STORAGE_TEST", largeStorageFlag, ARRAYSIZE(largeStorageFlag));
    if (largeStorageFlagLength > 0 && largeStorageFlag[0] == L'1') {
        constexpr int kLargeImageCount = 1024;
        constexpr std::uint32_t kLargeImageWidth = 512;
        constexpr std::uint32_t kLargeImageHeight = 256;
        constexpr std::uint64_t kLargeDataMinimum = 500ULL * 1024ULL * 1024ULL;
        ClipStore largeImages(1200);
        largeImages.open();
        largeImages.clear();
        const ULONGLONG largeAppendStarted = GetTickCount64();
        for (int index = 0; index < kLargeImageCount; ++index) {
            const std::string payload = makeTestImagePayload(
                static_cast<std::uint32_t>(index), kLargeImageWidth, kLargeImageHeight);
            if (!largeImages.append(ClipType::ImageV5, payload, clipLiteHash(payload))) return 173;
        }
        std::printf("large-image append: %llu ms\n",
                    static_cast<unsigned long long>(GetTickCount64() - largeAppendStarted));
        if (largeImages.diskBytes() < kLargeDataMinimum ||
            largeImages.countType(ClipType::Image) != kLargeImageCount) return 174;

        const std::string updatedPayload = makeTestImagePayload(
            0xABCDEFu, kLargeImageWidth, kLargeImageHeight);
        const std::string firstPayload = makeTestImagePayload(
            0, kLargeImageWidth, kLargeImageHeight);
        const ULONGLONG largeUpdateStarted = GetTickCount64();
        if (!largeImages.appendOrUpdate(ClipType::ImageV5, updatedPayload,
                                        clipLiteHash(firstPayload))) return 175;
        std::printf("large-image update: %llu ms\n",
                    static_cast<unsigned long long>(GetTickCount64() - largeUpdateStarted));
        const ULONGLONG largePruneStarted = GetTickCount64();
        if (!largeImages.pruneCategory(ClipType::Image, 0, 128ULL * 1024ULL * 1024ULL) ||
            largeImages.activeCount() == 0 || largeImages.activeCount() >= kLargeImageCount) {
            return 176;
        }
        std::printf("large-image prune: %llu ms\n",
                    static_cast<unsigned long long>(GetTickCount64() - largePruneStarted));
        const std::uint64_t beforeLargeCompaction = largeImages.diskBytes();
        const ULONGLONG largeCompactStarted = GetTickCount64();
        if (!largeImages.compactIfNeeded() || largeImages.diskBytes() >= beforeLargeCompaction) {
            return 177;
        }
        std::printf("large-image compact: %llu ms\n",
                    static_cast<unsigned long long>(GetTickCount64() - largeCompactStarted));
        const std::size_t remainingLargeImages = largeImages.activeCount();
        const ULONGLONG largeReopenStarted = GetTickCount64();
        ClipStore largeImagesReopened(1200);
        if (!largeImagesReopened.open() ||
            largeImagesReopened.countType(ClipType::Image) != remainingLargeImages) return 178;
        std::printf("large-image reopen: %llu ms\n",
                    static_cast<unsigned long long>(GetTickCount64() - largeReopenStarted));
        largeImagesReopened.clear();
        largeImages.clear();
    }

    ClipStore legacyFavorite(10);
    legacyFavorite.open();
    legacyFavorite.clear();
    if (!legacyFavorite.append(ClipType::Text, "legacy-pin", clipLiteHash("legacy-pin"))) return 105;
    std::FILE* legacyFlagFile = nullptr;
    _wfopen_s(&legacyFlagFile, legacyFavorite.path().c_str(), L"r+b");
    TestDiskHeader legacyFavoriteHeader{};
    if (!legacyFlagFile || std::fread(&legacyFavoriteHeader, sizeof(legacyFavoriteHeader), 1,
                                      legacyFlagFile) != 1) return 106;
    TestDiskMetadata legacyFavoriteMetadata{};
    if (std::fread(&legacyFavoriteMetadata, sizeof(legacyFavoriteMetadata), 1,
                   legacyFlagFile) != 1) return 106;
    legacyFavoriteHeader.flags |= 1;
    legacyFavoriteMetadata.version = 1;
    legacyFavoriteMetadata.size = sizeof(TestDiskMetadata);
    legacyFavoriteMetadata.crc = testMetadataCrc(
        &legacyFavoriteMetadata, offsetof(TestDiskMetadata, crc));
    if (_fseeki64(legacyFlagFile, 0, SEEK_SET) != 0 ||
        std::fwrite(&legacyFavoriteHeader, sizeof(legacyFavoriteHeader), 1, legacyFlagFile) != 1 ||
        std::fwrite(&legacyFavoriteMetadata, sizeof(legacyFavoriteMetadata), 1, legacyFlagFile) != 1 ||
        std::fclose(legacyFlagFile) != 0) return 107;
    ClipStore migratedLegacyFavorite(10);
    if (!migratedLegacyFavorite.open() || migratedLegacyFavorite.activeCount() != 1 ||
        !migratedLegacyFavorite.items()[0].favorite || migratedLegacyFavorite.items()[0].pinned) return 108;
    migratedLegacyFavorite.clear();
    legacyFavorite.clear();
    return 0;
}
