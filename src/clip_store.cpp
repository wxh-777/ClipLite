#include "clip_store.h"

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <limits>

namespace {

constexpr std::uint32_t kMagic = 0x314C4343; // CCL1
constexpr std::uint16_t kVersion = 4;
constexpr std::uint32_t kMaxPayload = 32u * 1024u * 1024u;
constexpr std::uint32_t kMaxSource = 256;
constexpr std::size_t kMaxNoteBytes = 200;
constexpr std::uint8_t kMetadataFlag = 4;
constexpr std::uint8_t kDeletedFlag = 8;
// Metadata v2 reserves the high contentSize bits for state without changing record size.
constexpr std::uint64_t kPinnedMetadataBit = 1ULL << 63;
constexpr std::uint64_t kFavoriteMetadataBit = 1ULL << 62;
constexpr std::uint64_t kCompactionThreshold = 8u * 1024u * 1024u;
constexpr std::uint32_t kMetadataMagic = 0x31444D43; // CMD1
constexpr std::uint16_t kMetadataVersion = 3;
constexpr std::uint32_t kMaxMetadataSize = 4096;
constexpr std::uint32_t kTransactionMagic = 0x314E5854; // TXN1
constexpr std::uint16_t kTransactionVersion = 1;
constexpr std::uint8_t kTransactionPrepared = 1;
constexpr std::uint8_t kTransactionAppended = 2;
constexpr std::uint32_t kStoredHtmlMagic = 0x314D5448; // HTM1
constexpr std::size_t kStoredHtmlHeaderSize = 12;
constexpr std::size_t kPreviewMaxBytes = 256;

#pragma pack(push, 1)
struct DiskHeader {
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

struct LegacyDiskMetadata {
    std::uint64_t createdAt;
    std::uint64_t lastUsedAt;
    std::uint64_t useCount;
    std::uint64_t contentSize;
    std::uint32_t crc;
};

struct DiskMetadataV1 {
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

struct DiskMetadataV2 {
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

struct DiskMetadataV3 {
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
    std::uint32_t noteSize;
    char note[kMaxNoteBytes];
    std::uint32_t crc;
};

struct DiskTransaction {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint8_t state;
    std::uint8_t reserved;
    std::uint64_t targetRecordId;
    std::uint64_t duplicateRecordId;
    std::uint64_t appendOffset;
    std::uint32_t crc;
};
#pragma pack(pop)

static_assert(sizeof(DiskMetadataV1) == sizeof(DiskMetadataV2));
static_assert(sizeof(DiskMetadataV3) == 272);

std::string truncateUtf8(const std::string& value, std::size_t maxBytes);
static_assert(sizeof(DiskTransaction) == 36);

const std::array<std::uint32_t, 256>& crc32Table() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::size_t index = 0; index < values.size(); ++index) {
            std::uint32_t value = static_cast<std::uint32_t>(index);
            for (int bit = 0; bit < 8; ++bit) {
                value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
            }
            values[index] = value;
        }
        return values;
    }();
    return table;
}

std::uint32_t crc32Update(std::uint32_t crc, const char* data, std::size_t size) {
    const auto& table = crc32Table();
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ static_cast<unsigned char>(data[i])) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

std::uint32_t crc32(const std::string& data) {
    return crc32Update(0xFFFFFFFFu, data.data(), data.size()) ^ 0xFFFFFFFFu;
}

std::uint32_t metadataCrc(const void* data, std::size_t size) {
    return crc32Update(0xFFFFFFFFu, static_cast<const char*>(data), size) ^ 0xFFFFFFFFu;
}

std::wstring transactionPath(const std::wstring& historyPath) {
    return historyPath + L".txn";
}

std::wstring transactionTempPath(const std::wstring& historyPath) {
    return historyPath + L".txn.tmp";
}

bool writeTransaction(const std::wstring& historyPath, DiskTransaction transaction) {
    transaction.magic = kTransactionMagic;
    transaction.version = kTransactionVersion;
    transaction.reserved = 0;
    transaction.crc = metadataCrc(&transaction, offsetof(DiskTransaction, crc));
    const std::wstring tempPath = transactionTempPath(historyPath);
    std::FILE* file = nullptr;
    _wfopen_s(&file, tempPath.c_str(), L"wb");
    if (!file) return false;
    const bool written = std::fwrite(&transaction, sizeof(transaction), 1, file) == 1;
    const bool flushed = written && std::fflush(file) == 0;
    const bool committed = flushed && _commit(_fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !committed || !closed ||
        !MoveFileExW(tempPath.c_str(), transactionPath(historyPath).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    return true;
}

bool readTransaction(const std::wstring& historyPath, DiskTransaction& transaction) {
    std::FILE* file = nullptr;
    _wfopen_s(&file, transactionPath(historyPath).c_str(), L"rb");
    if (!file) return false;
    const bool read = std::fread(&transaction, sizeof(transaction), 1, file) == 1;
    std::fclose(file);
    return read && transaction.magic == kTransactionMagic &&
        transaction.version == kTransactionVersion &&
        (transaction.state == kTransactionPrepared ||
         transaction.state == kTransactionAppended) &&
        transaction.crc == metadataCrc(&transaction, offsetof(DiskTransaction, crc));
}

void clearTransaction(const std::wstring& historyPath) {
    DeleteFileW(transactionPath(historyPath).c_str());
    DeleteFileW(transactionTempPath(historyPath).c_str());
}

bool truncateHistory(const std::wstring& historyPath, std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) return false;
    std::FILE* file = nullptr;
    _wfopen_s(&file, historyPath.c_str(), L"r+b");
    if (!file) return false;
    const bool truncated = _chsize_s(_fileno(file), static_cast<__int64>(offset)) == 0;
    const bool closed = std::fclose(file) == 0;
    return truncated && closed;
}

std::uint64_t nowUnixMillis() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    constexpr std::uint64_t kUnixEpoch = 116444736000000000ULL;
    return value.QuadPart < kUnixEpoch ? 0 : (value.QuadPart - kUnixEpoch) / 10000ULL;
}

std::uint64_t nextCopyTime(const std::vector<ClipItem>& items) {
    const std::uint64_t now = nowUnixMillis();
    if (items.empty()) return now;
    const std::uint64_t latest = std::max_element(
        items.begin(), items.end(), [](const ClipItem& first, const ClipItem& second) {
            return first.lastCopiedAt < second.lastCopiedAt;
        })->lastCopiedAt;
    if (now > latest) return now;
    return latest == std::numeric_limits<std::uint64_t>::max() ? latest : latest + 1;
}

std::uint64_t activityTime(const ClipItem& item) {
    const std::uint64_t copiedAt = item.lastCopiedAt != 0 ? item.lastCopiedAt :
        (item.timestamp > std::numeric_limits<std::uint64_t>::max() / 1000ULL
            ? std::numeric_limits<std::uint64_t>::max() : item.timestamp * 1000ULL);
    return std::max(copiedAt, item.lastUsedAt);
}

std::uint64_t recordBytes(const ClipItem& item) {
    const std::uint32_t headerSize = item.headerSize == 0
        ? static_cast<std::uint32_t>(sizeof(DiskHeader)) : item.headerSize;
    return headerSize + item.source.size() + item.payloadSize;
}

bool sortByActivity(const ClipItem& first, const ClipItem& second) {
    const std::uint64_t firstActivity = activityTime(first);
    const std::uint64_t secondActivity = activityTime(second);
    if (firstActivity != secondActivity) return firstActivity > secondActivity;
    return first.recordId > second.recordId;
}

bool sortByCopyTime(const ClipItem& first, const ClipItem& second) {
    const std::uint64_t firstCopied = first.lastCopiedAt != 0 ? first.lastCopiedAt : first.timestamp * 1000ULL;
    const std::uint64_t secondCopied = second.lastCopiedAt != 0 ? second.lastCopiedAt : second.timestamp * 1000ULL;
    if (firstCopied != secondCopied) return firstCopied > secondCopied;
    return first.recordId > second.recordId;
}

bool sortPinnedFirst(const ClipItem& first, const ClipItem& second, bool byLastUsed) {
    if (first.pinned != second.pinned) return first.pinned;
    return byLastUsed ? sortByActivity(first, second) : sortByCopyTime(first, second);
}

void insertSortedItem(std::vector<ClipItem>& items, ClipItem item, bool byLastUsed) {
    const auto firstUnpinned = std::find_if(items.begin(), items.end(), [](const ClipItem& candidate) {
        return !candidate.pinned;
    });
    const auto rangeBegin = item.pinned ? items.begin() : firstUnpinned;
    const auto rangeEnd = item.pinned ? firstUnpinned : items.end();
    const auto position = std::lower_bound(rangeBegin, rangeEnd, item,
        [byLastUsed](const ClipItem& first, const ClipItem& second) {
            return sortPinnedFirst(first, second, byLastUsed);
        });
    items.insert(position, std::move(item));
}

bool writeRecordHeader(std::FILE* file, const ClipItem& item) {
    if (!file) return false;
    DiskHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.type = static_cast<std::uint8_t>(item.type);
    // Legacy readers treat bit 0 as pinned, so keep it set for either marker.
    header.flags = static_cast<std::uint8_t>((item.favorite || item.pinned ? 1 : 0) |
                                             (item.encrypted ? 2 : 0) | kMetadataFlag);
    header.timestamp = item.timestamp;
    header.hash = item.hash;
    header.category = item.category;
    header.payloadSize = item.payloadSize;
    header.payloadCrc = item.payloadCrc;
    header.sourceSize = static_cast<std::uint32_t>(item.source.size());
    header.sourceCrc = crc32(item.source);
    header.expiresAt = item.expiresAt;
    DiskMetadataV3 metadata{kMetadataMagic, kMetadataVersion,
                             static_cast<std::uint16_t>(sizeof(DiskMetadataV3)),
                             item.recordId, item.createdAt, item.lastCopiedAt, item.lastUsedAt,
                             item.useCount,
                             item.contentSize | (item.pinned ? kPinnedMetadataBit : 0) |
                                 (item.favorite ? kFavoriteMetadataBit : 0),
                             item.copyCount, 0, {}};
    const std::string note = truncateUtf8(item.note, kMaxNoteBytes);
    metadata.noteSize = static_cast<std::uint32_t>(note.size());
    if (!note.empty()) std::memcpy(metadata.note, note.data(), note.size());
    metadata.crc = metadataCrc(&metadata, offsetof(DiskMetadataV3, crc));
    return std::fwrite(&header, sizeof(header), 1, file) == 1 &&
           std::fwrite(&metadata, sizeof(metadata), 1, file) == 1;
}

bool protectPayload(const std::string& input, std::string& output) {
    DATA_BLOB source{static_cast<DWORD>(input.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
    DATA_BLOB protectedData{};
    if (!CryptProtectData(&source, L"ClipLite history", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &protectedData)) {
        return false;
    }
    output.assign(reinterpret_cast<const char*>(protectedData.pbData), protectedData.cbData);
    LocalFree(protectedData.pbData);
    return true;
}

bool unprotectPayload(const std::string& input, std::string& output) {
    DATA_BLOB source{static_cast<DWORD>(input.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
    DATA_BLOB plainData{};
    if (!CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plainData)) {
        return false;
    }
    output.assign(reinterpret_cast<const char*>(plainData.pbData), plainData.cbData);
    LocalFree(plainData.pbData);
    return true;
}

std::uint64_t nowUnix() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

bool ensureDirectory(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring executableDirectory() {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    if (length == 0 || length >= ARRAYSIZE(executable)) return {};
    std::wstring path(executable, length);
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator);
}

bool portableModeEnabled(std::wstring& dataDirectory) {
    const std::wstring executable = executableDirectory();
    if (executable.empty()) return false;
    const std::wstring marker = executable + L"\\portable.flag";
    const DWORD attributes = GetFileAttributesW(marker.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    dataDirectory = executable + L"\\data";
    return ensureDirectory(dataDirectory);
}

std::string lowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return value;
}

char lowerAsciiChar(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A')) : value;
}

std::string truncateUtf8(const std::string& value, std::size_t maxBytes) {
    const std::size_t limit = std::min(value.size(), maxBytes);
    std::size_t offset = 0;
    while (offset < limit) {
        const unsigned char lead = static_cast<unsigned char>(value[offset]);
        std::size_t sequenceSize = 0;
        if (lead <= 0x7F) {
            sequenceSize = 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            sequenceSize = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            sequenceSize = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            sequenceSize = 4;
        } else {
            return value.substr(0, limit);
        }

        if (sequenceSize > limit - offset) break;
        const unsigned char second = sequenceSize > 1
            ? static_cast<unsigned char>(value[offset + 1]) : 0;
        if (sequenceSize > 1 &&
            ((lead == 0xE0 && second < 0xA0) ||
             (lead == 0xED && second > 0x9F) ||
             (lead == 0xF0 && second < 0x90) ||
             (lead == 0xF4 && second > 0x8F))) {
            return value.substr(0, limit);
        }
        for (std::size_t index = 1; index < sequenceSize; ++index) {
            const unsigned char continuation = static_cast<unsigned char>(value[offset + index]);
            if ((continuation & 0xC0) != 0x80) return value.substr(0, limit);
        }
        offset += sequenceSize;
    }
    return value.substr(0, offset);
}

bool matchesType(ClipType requested, ClipType actual) {
    if (requested == ClipType::Text) {
        return actual == ClipType::Text || actual == ClipType::Html;
    }
    if (requested == ClipType::Image) {
        return actual == ClipType::Image || actual == ClipType::ImageV5;
    }
    return actual == requested;
}

std::string htmlTextPreview(const std::string& payload) {
    std::string html = payload;
    if (payload.size() >= kStoredHtmlHeaderSize) {
        std::uint32_t magic = 0;
        std::uint32_t textSize = 0;
        std::memcpy(&magic, payload.data(), sizeof(magic));
        std::memcpy(&textSize, payload.data() + 4, sizeof(textSize));
        const std::size_t contentSize = payload.size() - kStoredHtmlHeaderSize;
        if (magic == kStoredHtmlMagic) {
            const std::size_t availableText = std::min<std::size_t>(textSize, contentSize);
            if (availableText > 0) {
                return std::string(payload.data() + kStoredHtmlHeaderSize, availableText);
            }
            if (textSize == 0) {
                html.assign(payload.data() + kStoredHtmlHeaderSize, contentSize);
            }
        }
    }
    std::string text;
    text.reserve(html.size());
    bool inTag = false;
    for (const char character : html) {
        if (character == '<') {
            inTag = true;
        } else if (character == '>') {
            inTag = false;
        } else if (!inTag) {
            text.push_back(character);
        }
    }
    return text;
}

bool plainTextPayload(ClipType type, const std::string& payload, std::string& text) {
    if (type == ClipType::Text) {
        text = payload;
        return true;
    }
    if (type == ClipType::Html) {
        text = htmlTextPreview(payload);
        return !text.empty();
    }
    return false;
}

bool readPayloadFromFile(std::FILE* file, const ClipItem& item, std::string& payload) {
    const std::uint32_t headerSize = item.headerSize == 0
        ? static_cast<std::uint32_t>(sizeof(DiskHeader)) : item.headerSize;
    const std::uint64_t payloadOffset = item.fileOffset + headerSize + item.source.size();
    if (payloadOffset < item.fileOffset || payloadOffset >
        static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) {
        return false;
    }
    if (_fseeki64(file, static_cast<__int64>(payloadOffset), SEEK_SET) != 0) return false;
    payload.resize(item.payloadSize);
    if (std::fread(payload.data(), 1, payload.size(), file) != payload.size() ||
        crc32(payload) != item.payloadCrc) {
        return false;
    }
    if (!item.encrypted) return true;
    std::string plain;
    if (!unprotectPayload(payload, plain)) return false;
    payload = std::move(plain);
    return true;
}

bool streamContainsIgnoreCase(std::FILE* file, const ClipItem& item,
                              const std::string& needle,
                              const std::atomic<bool>* cancellation) {
    const std::uint32_t headerSize = item.headerSize == 0
        ? static_cast<std::uint32_t>(sizeof(DiskHeader)) : item.headerSize;
    const std::uint64_t payloadOffset = item.fileOffset + headerSize + item.source.size();
    if (payloadOffset < item.fileOffset || payloadOffset >
        static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) {
        return false;
    }
    if (_fseeki64(file, static_cast<__int64>(payloadOffset), SEEK_SET) != 0) return false;

    std::vector<std::size_t> prefix(needle.size());
    for (std::size_t i = 1; i < needle.size(); ++i) {
        std::size_t matched = prefix[i - 1];
        while (matched > 0 && needle[i] != needle[matched]) matched = prefix[matched - 1];
        if (needle[i] == needle[matched]) ++matched;
        prefix[i] = matched;
    }

    char buffer[64 * 1024];
    std::uint32_t remaining = item.payloadSize;
    std::uint32_t checksum = 0xFFFFFFFFu;
    std::size_t matched = 0;
    bool found = false;
    while (remaining > 0) {
        if (cancellation && cancellation->load(std::memory_order_relaxed)) return false;
        const std::size_t chunkSize = std::min<std::size_t>(remaining, sizeof(buffer));
        if (std::fread(buffer, 1, chunkSize, file) != chunkSize) return false;
        checksum = crc32Update(checksum, buffer, chunkSize);
        if (!found) {
            for (std::size_t i = 0; i < chunkSize; ++i) {
                const char value = lowerAsciiChar(buffer[i]);
                while (matched > 0 && value != needle[matched]) matched = prefix[matched - 1];
                if (value == needle[matched]) ++matched;
                if (matched == needle.size()) {
                    found = true;
                    matched = prefix[matched - 1];
                    break;
                }
            }
        }
        remaining -= static_cast<std::uint32_t>(chunkSize);
    }
    return (checksum ^ 0xFFFFFFFFu) == item.payloadCrc && found;
}

} // namespace

std::wstring clipLiteDataDirectory() {
    wchar_t testPath[MAX_PATH]{};
    const DWORD testPathLength = GetEnvironmentVariableW(
        L"CLIPLITE_TEST_DATA_DIR", testPath, ARRAYSIZE(testPath));
    if (testPathLength > 0 && testPathLength < ARRAYSIZE(testPath)) {
        std::wstring path(testPath, testPathLength);
        ensureDirectory(path);
        return path;
    }
    std::wstring portablePath;
    if (portableModeEnabled(portablePath)) return portablePath;
    wchar_t buffer[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buffer))) {
        return L".";
    }
    std::wstring path = buffer;
    path += L"\\ClipLite";
    ensureDirectory(path);
    return path;
}

std::uint64_t clipLiteHash(const std::string& data) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

ClipStore::ClipStore(std::size_t maxItems) : maxItems_(maxItems) {
    path_ = clipLiteDataDirectory() + L"\\history.bin";
}

bool ClipStore::setDataDirectory(const std::wstring& directory) {
    if (directory.empty()) return false;
    std::wstring normalized = directory;
    while (normalized.size() > 3 &&
           (normalized.back() == L'\\' || normalized.back() == L'/')) {
        normalized.pop_back();
    }
    if (!ensureDirectory(normalized)) return false;
    path_ = normalized + L"\\history.bin";
    ++revision_;
    return true;
}

bool ClipStore::open() {
    items_.clear();
    diskBytes_ = 0;
    deadBytes_ = 0;
    ++revision_;
    DeleteFileW((path_ + L".tmp").c_str());
    DiskTransaction transaction{};
    bool transactionValid = readTransaction(path_, transaction);
    if (transactionValid && transaction.state == kTransactionPrepared) {
        if (!truncateHistory(path_, transaction.appendOffset)) return false;
        clearTransaction(path_);
        transactionValid = false;
    } else if (!transactionValid) {
        clearTransaction(path_);
    }
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"rb");
    if (!file) return true;

    _fseeki64(file, 0, SEEK_END);
    const auto fileSize = static_cast<std::uint64_t>(_ftelli64(file));
    _fseeki64(file, 0, SEEK_SET);
    std::uint64_t validBytes = 0;
    std::uint64_t maximumRecordId = 0;

    while (true) {
        const auto offset = static_cast<std::uint64_t>(_ftelli64(file));
        DiskHeader header{};
        if (std::fread(&header, sizeof(header), 1, file) != 1) break;
        if (header.magic != kMagic || header.version != kVersion ||
            header.payloadSize > kMaxPayload || header.sourceSize > kMaxSource ||
            header.type < 1 || header.type > 5 ||
            (header.flags & ~(0x07u | kDeletedFlag)) != 0) {
            break;
        }

        const bool hasMetadata = (header.flags & kMetadataFlag) != 0;
        std::uint32_t metadataSize = 0;
        std::uint64_t recordId = 0;
        std::uint64_t createdAt = header.timestamp;
        std::uint64_t lastCopiedAt = header.timestamp * 1000ULL;
        std::uint64_t lastUsedAt = 0;
        std::uint64_t useCount = 0;
        std::uint64_t copyCount = 1;
        std::uint64_t contentSize = header.payloadSize;
        std::string note;
        bool favorite = (header.flags & 1) != 0;
        bool pinned = false;
        if (hasMetadata) {
            std::uint32_t metadataWord = 0;
            if (std::fread(&metadataWord, sizeof(metadataWord), 1, file) != 1) break;
            if (metadataWord == kMetadataMagic) {
                std::uint16_t version = 0;
                std::uint16_t size = 0;
                if (std::fread(&version, sizeof(version), 1, file) != 1 ||
                    std::fread(&size, sizeof(size), 1, file) != 1 ||
                    size < sizeof(std::uint32_t) + sizeof(std::uint16_t) * 2 ||
                    size > kMaxMetadataSize) {
                    break;
                }
                std::vector<char> bytes(size);
                std::memcpy(bytes.data(), &metadataWord, sizeof(metadataWord));
                std::memcpy(bytes.data() + sizeof(metadataWord), &version, sizeof(version));
                std::memcpy(bytes.data() + sizeof(metadataWord) + sizeof(version), &size, sizeof(size));
                const std::size_t prefixSize = sizeof(metadataWord) + sizeof(version) + sizeof(size);
                if (std::fread(bytes.data() + prefixSize, 1, bytes.size() - prefixSize, file) !=
                    bytes.size() - prefixSize) break;
                metadataSize = size;
                if (version == 1 && size == sizeof(DiskMetadataV1)) {
                    DiskMetadataV1 metadata{};
                    std::memcpy(&metadata, bytes.data(), sizeof(metadata));
                    if (metadata.createdAt != 0 && metadata.contentSize <= kMaxPayload &&
                        metadata.crc == metadataCrc(bytes.data(), bytes.size() - sizeof(metadata.crc))) {
                        recordId = metadata.recordId;
                        createdAt = metadata.createdAt;
                        lastCopiedAt = metadata.lastCopiedAt != 0
                            ? metadata.lastCopiedAt : header.timestamp * 1000ULL;
                        lastUsedAt = metadata.lastUsedAt;
                        useCount = metadata.useCount;
                        copyCount = metadata.copyCount;
                        if (copyCount == 0) copyCount = 1;
                        contentSize = metadata.contentSize;
                    }
                } else if (version == 2 && size == sizeof(DiskMetadataV2)) {
                    DiskMetadataV2 metadata{};
                    std::memcpy(&metadata, bytes.data(), sizeof(metadata));
                    const bool recordFavorite = (metadata.contentSize & kFavoriteMetadataBit) != 0;
                    const bool recordPinned = (metadata.contentSize & kPinnedMetadataBit) != 0;
                    const std::uint64_t storedContentSize = metadata.contentSize &
                        ~(kFavoriteMetadataBit | kPinnedMetadataBit);
                    if (metadata.createdAt != 0 && storedContentSize <= kMaxPayload &&
                        metadata.crc == metadataCrc(bytes.data(), bytes.size() - sizeof(metadata.crc))) {
                        recordId = metadata.recordId;
                        createdAt = metadata.createdAt;
                        lastCopiedAt = metadata.lastCopiedAt != 0
                            ? metadata.lastCopiedAt : header.timestamp * 1000ULL;
                        lastUsedAt = metadata.lastUsedAt;
                        useCount = metadata.useCount;
                        copyCount = metadata.copyCount == 0 ? 1 : metadata.copyCount;
                        contentSize = storedContentSize;
                        favorite = recordFavorite;
                         pinned = recordPinned && (header.flags & 1) != 0;
                     }
                } else if (version == kMetadataVersion && size == sizeof(DiskMetadataV3)) {
                    DiskMetadataV3 metadata{};
                    std::memcpy(&metadata, bytes.data(), sizeof(metadata));
                    const bool recordFavorite = (metadata.contentSize & kFavoriteMetadataBit) != 0;
                    const bool recordPinned = (metadata.contentSize & kPinnedMetadataBit) != 0;
                    const std::uint64_t storedContentSize = metadata.contentSize &
                        ~(kFavoriteMetadataBit | kPinnedMetadataBit);
                    if (metadata.noteSize <= kMaxNoteBytes && metadata.createdAt != 0 &&
                        storedContentSize <= kMaxPayload &&
                        metadata.crc == metadataCrc(bytes.data(), bytes.size() - sizeof(metadata.crc))) {
                        recordId = metadata.recordId;
                        createdAt = metadata.createdAt;
                        lastCopiedAt = metadata.lastCopiedAt != 0
                            ? metadata.lastCopiedAt : header.timestamp * 1000ULL;
                        lastUsedAt = metadata.lastUsedAt;
                        useCount = metadata.useCount;
                        copyCount = metadata.copyCount == 0 ? 1 : metadata.copyCount;
                        contentSize = storedContentSize;
                        favorite = recordFavorite;
                        pinned = recordPinned && (header.flags & 1) != 0;
                        note.assign(metadata.note, metadata.noteSize);
                    }
                }
            } else {
                LegacyDiskMetadata metadata{};
                std::memcpy(&metadata, &metadataWord, sizeof(metadataWord));
                if (std::fread(reinterpret_cast<char*>(&metadata) + sizeof(metadataWord), 1,
                               sizeof(metadata) - sizeof(metadataWord), file) !=
                    sizeof(metadata) - sizeof(metadataWord)) break;
                metadataSize = sizeof(metadata);
                if (metadata.createdAt != 0 && metadata.contentSize <= kMaxPayload &&
                    metadata.crc == metadataCrc(&metadata, offsetof(LegacyDiskMetadata, crc))) {
                    createdAt = metadata.createdAt;
                    lastUsedAt = metadata.lastUsedAt;
                    useCount = metadata.useCount;
                    contentSize = metadata.contentSize;
                }
            }
        }

        std::string source;
        if (header.sourceSize > 0) {
            source.resize(header.sourceSize);
            if (std::fread(source.data(), 1, source.size(), file) != source.size() ||
                crc32(source) != header.sourceCrc) {
                break;
            }
        }
        const auto payloadOffset = static_cast<std::uint64_t>(_ftelli64(file));
        if (payloadOffset > fileSize || header.payloadSize > fileSize - payloadOffset) break;

        ClipItem item;
        item.type = static_cast<ClipType>(header.type);
        item.recordId = recordId;
        item.timestamp = header.timestamp;
        item.lastCopiedAt = lastCopiedAt;
        item.hash = header.hash;
        item.category = header.category;
        item.favorite = favorite;
        item.pinned = pinned;
        item.fileOffset = offset;
        item.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + metadataSize);
        item.payloadSize = header.payloadSize;
        item.payloadCrc = header.payloadCrc;
        item.encrypted = (header.flags & 2) != 0;
        item.source = std::move(source);
        item.expiresAt = header.expiresAt;
        item.createdAt = createdAt;
        item.lastUsedAt = lastUsedAt;
        item.useCount = useCount;
        item.copyCount = copyCount;
        item.contentSize = contentSize;
        item.note = std::move(note);
        maximumRecordId = std::max(maximumRecordId, item.recordId);

        std::string preview;
        const bool isImage = header.type == static_cast<std::uint8_t>(ClipType::Image) ||
                             header.type == static_cast<std::uint8_t>(ClipType::ImageV5);
        const std::size_t previewLimit = item.encrypted || isImage ? 0 :
            (header.type == static_cast<std::uint8_t>(ClipType::Html) ? 4096 : kPreviewMaxBytes);
        std::uint32_t remaining = header.payloadSize;
        std::uint32_t checksum = 0xFFFFFFFFu;
        char buffer[64 * 1024];
        bool payloadValid = true;
        while (remaining > 0) {
            const std::size_t chunkSize = std::min<std::size_t>(remaining, sizeof(buffer));
            if (std::fread(buffer, 1, chunkSize, file) != chunkSize) {
                payloadValid = false;
                break;
            }
            checksum = crc32Update(checksum, buffer, chunkSize);
            if (preview.size() < previewLimit) {
                const std::size_t previewBytes = std::min(chunkSize, previewLimit - preview.size());
                preview.append(buffer, previewBytes);
            }
            remaining -= static_cast<std::uint32_t>(chunkSize);
        }
        if (!payloadValid) break;
        if ((checksum ^ 0xFFFFFFFFu) != header.payloadCrc) break;
        if (item.encrypted) preview = "[Protected]";
        else if (isImage) preview = "[Image]";
        if (header.type == static_cast<std::uint8_t>(ClipType::Html)) {
            const std::string htmlPreview = htmlTextPreview(preview);
            preview = htmlPreview.empty() ? "[HTML]" : truncateUtf8(htmlPreview, kPreviewMaxBytes);
        } else {
            preview = truncateUtf8(preview, kPreviewMaxBytes);
        }
        if (header.type == static_cast<std::uint8_t>(ClipType::Files)) preview = "[Files] " + preview;
        item.preview = std::move(preview);
        validBytes = payloadOffset + header.payloadSize;
        if ((header.flags & kDeletedFlag) != 0) {
            deadBytes_ += recordBytes(item);
            continue;
        }
        items_.push_back(std::move(item));
    }
    diskBytes_ = validBytes;
    std::fclose(file);
    nextRecordId_ = maximumRecordId == std::numeric_limits<std::uint64_t>::max()
        ? maximumRecordId : maximumRecordId + 1;
    std::vector<ClipItem> latestRecords;
    latestRecords.reserve(items_.size());
    for (const ClipItem& item : items_) {
        if (item.recordId != 0) {
            const auto previous = std::find_if(latestRecords.begin(), latestRecords.end(),
                                               [&item](const ClipItem& candidate) {
                return candidate.recordId == item.recordId;
            });
            if (previous != latestRecords.end()) {
                deadBytes_ += recordBytes(*previous);
                *previous = item;
                continue;
            }
        }
        latestRecords.push_back(item);
    }
    items_ = std::move(latestRecords);
    std::reverse(items_.begin(), items_.end());
    std::stable_sort(items_.begin(), items_.end(), [this](const ClipItem& first, const ClipItem& second) {
        return sortPinnedFirst(first, second, sortByLastUsed_);
    });
    for (ClipItem& item : items_) {
        if (item.recordId == 0) {
            item.recordId = nextRecordId_ == 0 ? 1 : nextRecordId_;
            if (nextRecordId_ != std::numeric_limits<std::uint64_t>::max()) ++nextRecordId_;
        }
    }
    if (transactionValid && transaction.state == kTransactionAppended) {
        const auto target = std::find_if(items_.begin(), items_.end(),
                                         [&transaction](const ClipItem& item) {
            return item.recordId == transaction.targetRecordId;
        });
        const auto duplicate = std::find_if(items_.begin(), items_.end(),
                                            [&transaction](const ClipItem& item) {
            return transaction.duplicateRecordId != 0 &&
                item.recordId == transaction.duplicateRecordId;
        });
        if (target != items_.end() && duplicate != items_.end() && duplicate != target) {
            const ClipItem duplicateItem = *duplicate;
            if (!markDeletedBatch(std::vector<ClipItem>{duplicateItem}, true)) return false;
            deadBytes_ += recordBytes(duplicateItem);
            items_.erase(std::find_if(items_.begin(), items_.end(),
                                      [transaction](const ClipItem& item) {
                return item.recordId == transaction.duplicateRecordId;
            }));
        }
        clearTransaction(path_);
        transactionValid = false;
    }
    if (validBytes == fileSize) return true;

    // A damaged tail cannot contain a recoverable record. Truncate it in place
    // so opening a large history does not rewrite every valid payload.
    std::FILE* repair = nullptr;
    _wfopen_s(&repair, path_.c_str(), L"r+b");
    if (repair) {
        const bool truncated = _chsize_s(_fileno(repair), static_cast<__int64>(validBytes)) == 0;
        const bool closed = std::fclose(repair) == 0;
        if (truncated && closed) {
            diskBytes_ = validBytes;
            return true;
        }
    }
    return rebuildFile();
}

bool ClipStore::setSortByLastUsed(bool enabled) {
    if (sortByLastUsed_ == enabled) return true;
    sortByLastUsed_ = enabled;
    if (items_.empty()) return true;
    std::stable_sort(items_.begin(), items_.end(), [this](const ClipItem& first, const ClipItem& second) {
        return sortPinnedFirst(first, second, sortByLastUsed_);
    });
    ++revision_;
    return true;
}

bool ClipStore::writeRecord(std::FILE* file, const ClipItem& item, const std::string& payload) const {
    ClipItem headerItem = item;
    headerItem.payloadSize = static_cast<std::uint32_t>(payload.size());
    headerItem.payloadCrc = crc32(payload);
    return headerItem.payloadSize <= kMaxPayload && headerItem.source.size() <= kMaxSource &&
            writeRecordHeader(file, headerItem) &&
            (item.source.empty() || std::fwrite(item.source.data(), 1, item.source.size(), file) == item.source.size()) &&
            (payload.empty() || std::fwrite(payload.data(), 1, payload.size(), file) == payload.size());
}

bool ClipStore::append(ClipType type, const std::string& payload, std::uint64_t hash,
                       const std::string& source, std::uint64_t expiresAt) {
    if (payload.empty() || payload.size() > maxPayloadBytes_) return false;
    const std::size_t hashMatch = findHash(hash);
    if (hashMatch != items_.size()) {
        std::string existingPayload;
        if (!readPayload(hashMatch, existingPayload)) return false;
        if (items_[hashMatch].type == type && existingPayload == payload) return false;
    }
    const std::vector<ClipItem> backup = items_;
    const std::uint64_t oldDiskBytes = diskBytes_;
    const std::uint64_t oldDeadBytes = deadBytes_;
    std::vector<ClipItem> deletedItems;
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"ab");
    if (!file) return false;
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }

    ClipItem item;
    item.type = type;
    item.recordId = nextRecordId_ == 0 ? 1 : nextRecordId_;
    item.timestamp = nowUnix();
    item.lastCopiedAt = nextCopyTime(items_);
    item.createdAt = item.timestamp;
    item.hash = hash;
    item.encrypted = encryptionEnabled_;
    item.source = source.substr(0, kMaxSource);
    item.expiresAt = expiresAt;
    item.copyCount = 1;
    item.contentSize = payload.size();
    std::string storedPayload;
    if (item.encrypted && !protectPayload(payload, storedPayload)) {
        std::fclose(file);
        return false;
    }
    if (!item.encrypted) storedPayload = payload;
    if (storedPayload.empty() || storedPayload.size() > kMaxPayload) {
        std::fclose(file);
        return false;
    }
    item.payloadSize = static_cast<std::uint32_t>(storedPayload.size());
    item.payloadCrc = crc32(storedPayload);
    item.fileOffset = static_cast<std::uint64_t>(_ftelli64(file));
    item.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + sizeof(DiskMetadataV3));
    item.preview = makePreview(type, payload);
    const std::uint64_t recordSize = sizeof(DiskHeader) + sizeof(DiskMetadataV3) +
                                     item.source.size() + storedPayload.size();
    const bool ok = writeRecord(file, item, storedPayload);
    const bool flushed = ok && std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!flushed || !closed) {
        _wfopen_s(&file, path_.c_str(), L"r+b");
        if (file) {
            _chsize_s(_fileno(file), static_cast<__int64>(item.fileOffset));
            std::fclose(file);
        }
        return false;
    }

    insertSortedItem(items_, std::move(item), sortByLastUsed_);
    diskBytes_ += recordSize;
    while (maxItems_ > 0 && items_.size() > maxItems_) {
        const auto victim = std::find_if(items_.rbegin(), items_.rend(),
                                         [this](const ClipItem& candidate) {
                                             return !candidate.favorite &&
                                                 !(protectPinnedFromAutoCleanup_ && candidate.pinned);
                                         });
        if (victim == items_.rend()) break;
        const auto victimIterator = std::prev(victim.base());
        if (!markDeleted(*victimIterator, true)) {
            items_ = backup;
            diskBytes_ = oldDiskBytes;
            deadBytes_ = oldDeadBytes;
            for (const ClipItem& deleted : deletedItems) markDeleted(deleted, false);
            std::FILE* rollback = nullptr;
            _wfopen_s(&rollback, path_.c_str(), L"r+b");
            if (rollback) {
                _chsize_s(_fileno(rollback), static_cast<__int64>(item.fileOffset));
                std::fclose(rollback);
            }
            return false;
        }
        deadBytes_ += recordBytes(*victimIterator);
        deletedItems.push_back(*victimIterator);
        items_.erase(victimIterator);
    }
    ++revision_;
    if (nextRecordId_ != std::numeric_limits<std::uint64_t>::max()) ++nextRecordId_;
    return true;
}

bool ClipStore::recordUse(std::size_t index, bool promote) {
    if (index >= items_.size()) return false;
    const std::vector<ClipItem> backup = items_;
    ClipItem& item = items_[index];
    std::uint64_t latestActivity = 0;
    for (std::size_t otherIndex = 0; otherIndex < items_.size(); ++otherIndex) {
        if (otherIndex != index) latestActivity = std::max(latestActivity, activityTime(items_[otherIndex]));
    }
    item.lastUsedAt = nowUnixMillis();
    if (item.lastUsedAt <= latestActivity && latestActivity != std::numeric_limits<std::uint64_t>::max()) {
        item.lastUsedAt = latestActivity + 1;
    }
    if (item.useCount != std::numeric_limits<std::uint64_t>::max()) ++item.useCount;

    bool persisted = false;
    if (item.headerSize >= sizeof(DiskHeader) + sizeof(DiskMetadataV3)) {
        const std::uint64_t metadataOffset = item.fileOffset + sizeof(DiskHeader);
        if (metadataOffset >= item.fileOffset &&
            metadataOffset <= static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) {
            DiskMetadataV3 metadata{kMetadataMagic, kMetadataVersion,
                                    static_cast<std::uint16_t>(sizeof(DiskMetadataV3)),
                                    item.recordId, item.createdAt, item.lastCopiedAt, item.lastUsedAt,
                                    item.useCount,
                                    item.contentSize | (item.pinned ? kPinnedMetadataBit : 0) |
                                        (item.favorite ? kFavoriteMetadataBit : 0),
                                     item.copyCount, 0, {}};
            const std::string note = truncateUtf8(item.note, kMaxNoteBytes);
            metadata.noteSize = static_cast<std::uint32_t>(note.size());
            if (!note.empty()) std::memcpy(metadata.note, note.data(), note.size());
            metadata.crc = metadataCrc(&metadata, offsetof(DiskMetadataV3, crc));
            std::FILE* file = nullptr;
            _wfopen_s(&file, path_.c_str(), L"r+b");
            if (file) {
                const bool written = _fseeki64(file, static_cast<__int64>(metadataOffset), SEEK_SET) == 0 &&
                    std::fwrite(&metadata, sizeof(metadata), 1, file) == 1;
                const bool flushed = written && std::fflush(file) == 0;
                const bool closed = std::fclose(file) == 0;
                persisted = written && flushed && closed;
            }
        }
    } else {
        persisted = rebuildFile();
    }
    if (!persisted) {
        items_ = backup;
        return false;
    }
    if (promote && sortByLastUsed_ && index != 0) {
        std::stable_sort(items_.begin(), items_.end(), [](const ClipItem& first, const ClipItem& second) {
            return sortPinnedFirst(first, second, true);
        });
    }
    ++revision_;
    return true;
}

bool ClipStore::appendOrUpdate(ClipType type, const std::string& payload, std::uint64_t hash,
                               const std::string& source, std::uint64_t expiresAt) {
    if (payload.empty() || payload.size() > maxPayloadBytes_) return false;
    const std::size_t existing = findHash(hash);
    if (existing == items_.size()) return append(type, payload, hash, source, expiresAt);

    const std::vector<ClipItem> backup = items_;
    if (backup[existing].favorite) {
        std::string favoritePayload;
        if (!readPayload(existing, favoritePayload)) return false;
        return backup[existing].type == type && favoritePayload == payload;
    }
    std::string existingPayload;
    if (backup[existing].type == type &&
        backup[existing].headerSize == sizeof(DiskHeader) + sizeof(DiskMetadataV3) &&
        (source.empty() || source.size() == backup[existing].source.size()) &&
        readPayload(existing, existingPayload) && existingPayload == payload) {
        ClipItem item = backup[existing];
        item.timestamp = nowUnix();
        item.lastCopiedAt = nextCopyTime(items_);
        if (item.copyCount != std::numeric_limits<std::uint64_t>::max()) ++item.copyCount;
        if (!source.empty()) item.source = source.substr(0, kMaxSource);
        item.expiresAt = expiresAt;

        std::FILE* file = nullptr;
        _wfopen_s(&file, path_.c_str(), L"r+b");
        if (file) {
            const bool positioned = _fseeki64(file, static_cast<__int64>(item.fileOffset), SEEK_SET) == 0;
            const bool written = positioned && writeRecordHeader(file, item) &&
                (item.source.empty() || std::fwrite(item.source.data(), 1, item.source.size(), file) ==
                 item.source.size());
            const bool flushed = written && std::fflush(file) == 0;
            const bool closed = std::fclose(file) == 0;
            if (written && flushed && closed) {
                items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(existing));
                insertSortedItem(items_, std::move(item), sortByLastUsed_);
                ++revision_;
                return true;
            }
        }
    }

    const bool keepExistingRichText = backup[existing].type == ClipType::Html &&
                                      type == ClipType::Text;
    ClipItem replacement = backup[existing];
    std::string replacementPayload = payload;
    if (keepExistingRichText) {
        if (!readPayload(existing, replacementPayload)) return false;
        replacement.preview = makePreview(replacement.type, replacementPayload);
    } else {
        replacement.type = type;
        replacement.hash = hash;
        replacement.preview = makePreview(type, payload);
        replacement.contentSize = payload.size();
    }
    replacement.timestamp = nowUnix();
    replacement.lastCopiedAt = nextCopyTime(items_);
    if (replacement.copyCount != std::numeric_limits<std::uint64_t>::max()) {
        ++replacement.copyCount;
    }
    if (!source.empty()) replacement.source = source.substr(0, kMaxSource);
    replacement.expiresAt = expiresAt;
    replacement.hash = hash;
    if (!appendRecordReplacement(existing, std::move(replacement), replacementPayload,
                                 items_.size())) {
        return false;
    }
    std::stable_sort(items_.begin(), items_.end(), [this](const ClipItem& first, const ClipItem& second) {
        return sortPinnedFirst(first, second, sortByLastUsed_);
    });
    return true;
}

std::size_t ClipStore::findPlainTextMatch(const std::string& payload, std::uint64_t hash,
                                          std::size_t ignoredIndex) const {
    for (std::size_t index = 0; index < items_.size(); ++index) {
        const ClipItem& item = items_[index];
        if (index == ignoredIndex || item.hash != hash ||
            (item.type != ClipType::Text && item.type != ClipType::Html)) {
            continue;
        }
        std::string existingPayload;
        if (!readPayload(index, existingPayload)) return items_.size();
        std::string existingText;
        if (plainTextPayload(item.type, existingPayload, existingText) &&
            existingText == payload) {
            return index;
        }
    }
    return items_.size();
}

bool ClipStore::appendRecordReplacement(std::size_t targetIndex, ClipItem replacement,
                                        const std::string& payload,
                                        std::size_t duplicateIndex) {
    if (targetIndex >= items_.size() || payload.empty() || payload.size() > maxPayloadBytes_ ||
        (duplicateIndex != items_.size() && duplicateIndex >= items_.size()) ||
        duplicateIndex == targetIndex) {
        return false;
    }

    const ClipItem oldTarget = items_[targetIndex];
    const ClipItem oldDuplicate = duplicateIndex == items_.size()
        ? ClipItem{} : items_[duplicateIndex];
    std::string storedPayload = payload;
    if (replacement.encrypted && !protectPayload(payload, storedPayload)) return false;
    if (storedPayload.empty() || storedPayload.size() > kMaxPayload) return false;

    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"ab");
    if (!file || _fseeki64(file, 0, SEEK_END) != 0) {
        if (file) std::fclose(file);
        return false;
    }
    const __int64 endOffset = _ftelli64(file);
    if (endOffset < 0) {
        std::fclose(file);
        return false;
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(endOffset);
    DiskTransaction transaction{kTransactionMagic, kTransactionVersion,
                               kTransactionPrepared, 0, replacement.recordId,
                               duplicateIndex == items_.size() ? 0 : oldDuplicate.recordId,
                               offset, 0};
    if (!writeTransaction(path_, transaction)) {
        std::fclose(file);
        return false;
    }
    replacement.fileOffset = offset;
    replacement.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + sizeof(DiskMetadataV3));
    replacement.payloadSize = static_cast<std::uint32_t>(storedPayload.size());
    replacement.payloadCrc = crc32(storedPayload);
    const std::uint64_t recordSize = sizeof(DiskHeader) + sizeof(DiskMetadataV3) +
                                     replacement.source.size() + storedPayload.size();
    const bool written = writeRecord(file, replacement, storedPayload);
    const bool flushed = written && std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    const auto rollbackAppend = [&]() {
        std::FILE* rollback = nullptr;
        _wfopen_s(&rollback, path_.c_str(), L"r+b");
        if (rollback) {
            _chsize_s(_fileno(rollback), endOffset);
            std::fclose(rollback);
        }
    };
    if (!written || !flushed || !closed) {
        rollbackAppend();
        clearTransaction(path_);
        return false;
    }

    transaction.state = kTransactionAppended;
    if (!writeTransaction(path_, transaction)) {
        rollbackAppend();
        clearTransaction(path_);
        return false;
    }
    std::vector<ClipItem> deleted;
    deleted.reserve(duplicateIndex == items_.size() ? 1 : 2);
    if (duplicateIndex != items_.size()) deleted.push_back(oldDuplicate);
    deleted.push_back(oldTarget);
    if (!markDeletedBatch(deleted, true)) {
        rollbackAppend();
        clearTransaction(path_);
        return false;
    }
    clearTransaction(path_);

    items_[targetIndex] = std::move(replacement);
    deadBytes_ += recordBytes(oldTarget);
    if (duplicateIndex != items_.size()) {
        deadBytes_ += recordBytes(oldDuplicate);
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(duplicateIndex));
    }
    diskBytes_ += recordSize;
    ++revision_;
    return true;
}

// 编辑只追加新版本并标记旧记录，避免修改一条历史时重写全部 payload。
bool ClipStore::appendTextReplacement(std::size_t targetIndex, const std::string& payload,
                                       std::size_t duplicateIndex) {
    if (targetIndex >= items_.size()) return false;
    ClipItem replacement = items_[targetIndex];
    replacement.type = ClipType::Text;
    replacement.hash = clipLiteHash(payload);
    replacement.preview = makePreview(ClipType::Text, payload);
    replacement.contentSize = payload.size();
    return appendRecordReplacement(targetIndex, std::move(replacement), payload, duplicateIndex);
}

bool ClipStore::replaceWithText(std::uint64_t recordId, const std::string& payload) {
    if (recordId == 0 || payload.empty() || payload.size() > maxPayloadBytes_) return false;
    const auto target = std::find_if(items_.begin(), items_.end(), [recordId](const ClipItem& item) {
        return item.recordId == recordId;
    });
    if (target == items_.end()) return false;
    const std::size_t targetIndex = static_cast<std::size_t>(target - items_.begin());
    const std::size_t duplicateIndex = findPlainTextMatch(
        payload, clipLiteHash(payload), targetIndex);
    if (duplicateIndex == items_.size() && target->type == ClipType::Text) {
        std::string existingPayload;
        if (!readPayload(targetIndex, existingPayload)) return false;
        if (existingPayload == payload) return true;
    }
    return appendTextReplacement(targetIndex, payload, duplicateIndex);
}

bool ClipStore::appendUniqueText(const std::string& payload, const std::string& source,
                                 std::uint64_t expiresAt, std::uint64_t& savedRecordId) {
    savedRecordId = 0;
    if (payload.empty() || payload.size() > maxPayloadBytes_) return false;
    const std::uint64_t hash = clipLiteHash(payload);
    const std::size_t existing = findPlainTextMatch(payload, hash, items_.size());
    if (existing != items_.size()) {
        savedRecordId = items_[existing].recordId;
        if (items_[existing].type == ClipType::Html &&
            !appendTextReplacement(existing, payload, items_.size())) {
            savedRecordId = 0;
            return false;
        }
        return true;
    }

    const std::uint64_t candidateId = nextRecordId_ == 0 ? 1 : nextRecordId_;
    if (!append(ClipType::Text, payload, hash, source, expiresAt)) return false;
    const auto saved = std::find_if(items_.begin(), items_.end(), [candidateId](const ClipItem& item) {
        return item.recordId == candidateId;
    });
    if (saved == items_.end()) return false;
    savedRecordId = candidateId;
    return true;
}

bool ClipStore::readPayload(std::size_t index, std::string& payload) const {
    if (index >= items_.size()) return false;
    return readPayloadSnapshot(path_, items_[index], payload);
}

bool ClipStore::readPayloadSnapshot(const std::wstring& path, const ClipItem& item,
                                    std::string& payload) const {
    std::FILE* file = nullptr;
    _wfopen_s(&file, path.c_str(), L"rb");
    if (!file) {
        return false;
    }
    const bool readOk = readPayloadFromFile(file, item, payload);
    std::fclose(file);
    return readOk;
}

bool ClipStore::rebuildFile() {
    const std::wstring tempPath = path_ + L".tmp";
    std::FILE* out = nullptr;
    _wfopen_s(&out, tempPath.c_str(), L"wb");
    if (!out) return false;

    std::vector<ClipItem> rebuilt;
    rebuilt.reserve(items_.size());
    std::uint64_t offset = 0;
    for (auto iterator = items_.rbegin(); iterator != items_.rend(); ++iterator) {
        const ClipItem& old = *iterator;
        std::string payload;
        if (!readPayload(&old - items_.data(), payload)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        ClipItem item = old;
        item.fileOffset = offset;
        item.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + sizeof(DiskMetadataV3));
        std::string storedPayload;
        if (item.encrypted && !protectPayload(payload, storedPayload)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        if (!item.encrypted) storedPayload = payload;
        item.payloadSize = static_cast<std::uint32_t>(storedPayload.size());
        item.payloadCrc = crc32(storedPayload);
        if (!writeRecord(out, item, storedPayload)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        offset += sizeof(DiskHeader) + sizeof(DiskMetadataV3) + item.source.size() + storedPayload.size();
        rebuilt.push_back(std::move(item));
    }
    const bool flushed = std::fflush(out) == 0;
    const bool closed = std::fclose(out) == 0;
    if (!flushed || !closed) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    if (!MoveFileExW(tempPath.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    std::reverse(rebuilt.begin(), rebuilt.end());
    items_ = std::move(rebuilt);
    diskBytes_ = offset;
    deadBytes_ = 0;
    return true;
}

bool ClipStore::markDeletedBatch(const std::vector<ClipItem>& items, bool deleted) {
    if (items.empty()) return true;
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"r+b");
    if (!file) return false;

    struct FlagChange {
        std::uint64_t offset;
        std::uint8_t oldValue;
    };
    std::vector<FlagChange> changes;
    changes.reserve(items.size());
    for (const ClipItem& item : items) {
        const std::uint64_t flagsOffset = item.fileOffset + offsetof(DiskHeader, flags);
        if (flagsOffset < item.fileOffset ||
            flagsOffset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max()) ||
            _fseeki64(file, static_cast<__int64>(flagsOffset), SEEK_SET) != 0) {
            std::fclose(file);
            return false;
        }
        std::uint8_t oldValue = 0;
        if (std::fread(&oldValue, sizeof(oldValue), 1, file) != 1) {
            std::fclose(file);
            return false;
        }
        changes.push_back({flagsOffset, oldValue});
        const std::uint8_t flags = static_cast<std::uint8_t>(
            ((item.favorite || item.pinned) ? 1u : 0u) | (item.encrypted ? 2u : 0u) |
            (item.headerSize > sizeof(DiskHeader) ? kMetadataFlag : 0u) |
            (deleted ? kDeletedFlag : 0u));
        if (_fseeki64(file, static_cast<__int64>(flagsOffset), SEEK_SET) != 0 ||
            std::fwrite(&flags, sizeof(flags), 1, file) != 1) {
            for (const FlagChange& change : changes) {
                if (_fseeki64(file, static_cast<__int64>(change.offset), SEEK_SET) == 0) {
                    std::fwrite(&change.oldValue, sizeof(change.oldValue), 1, file);
                }
            }
            std::fflush(file);
            _commit(_fileno(file));
            std::fclose(file);
            return false;
        }
    }

    const bool flushed = std::fflush(file) == 0;
    const bool committed = flushed && _commit(_fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (flushed && committed && closed) return true;
    std::FILE* rollback = nullptr;
    _wfopen_s(&rollback, path_.c_str(), L"r+b");
    if (rollback) {
        for (const FlagChange& change : changes) {
            if (_fseeki64(rollback, static_cast<__int64>(change.offset), SEEK_SET) != 0 ||
                std::fwrite(&change.oldValue, sizeof(change.oldValue), 1, rollback) != 1) {
                break;
            }
        }
        std::fflush(rollback);
        _commit(_fileno(rollback));
        std::fclose(rollback);
    }
    return false;
}

bool ClipStore::markDeleted(const ClipItem& item, bool deleted) {
    return markDeletedBatch(std::vector<ClipItem>{item}, deleted);
}

bool ClipStore::removeItems(const std::vector<ClipItem>& items) {
    if (items.empty()) return true;
    if (!markDeletedBatch(items, true)) return false;
    for (const ClipItem& item : items) deadBytes_ += recordBytes(item);
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&items](const ClipItem& current) {
        return std::find_if(items.begin(), items.end(), [&current](const ClipItem& deleted) {
            return deleted.recordId == current.recordId;
        }) != items.end();
    }), items_.end());
    ++revision_;
    return true;
}

// 删除单条记录时直接复制未删除的原始记录，避免重新处理大型 payload。
bool ClipStore::rebuildFileRaw() {
    const std::wstring tempPath = path_ + L".tmp";
    std::FILE* input = nullptr;
    std::FILE* output = nullptr;
    _wfopen_s(&input, path_.c_str(), L"rb");
    if (!input) return false;
    _wfopen_s(&output, tempPath.c_str(), L"wb");
    if (!output) {
        std::fclose(input);
        return false;
    }

    std::vector<ClipItem> rebuilt;
    rebuilt.reserve(items_.size());
    std::uint64_t offset = 0;
    char buffer[64 * 1024];
    const auto fail = [&]() {
        std::fclose(input);
        std::fclose(output);
        DeleteFileW(tempPath.c_str());
        return false;
    };
    for (auto iterator = items_.rbegin(); iterator != items_.rend(); ++iterator) {
        const ClipItem& old = *iterator;
        const std::uint64_t size = recordBytes(old);
        if (old.fileOffset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max()) - size ||
            offset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max()) - size ||
            _fseeki64(input, static_cast<__int64>(old.fileOffset), SEEK_SET) != 0) {
            return fail();
        }
        std::uint64_t remaining = size;
        while (remaining > 0) {
            const std::size_t chunkSize = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, sizeof(buffer)));
            if (std::fread(buffer, 1, chunkSize, input) != chunkSize ||
                std::fwrite(buffer, 1, chunkSize, output) != chunkSize) {
                return fail();
            }
            remaining -= chunkSize;
        }
        ClipItem item = old;
        item.fileOffset = offset;
        rebuilt.push_back(std::move(item));
        offset += size;
    }
    const bool inputClosed = std::fclose(input) == 0;
    input = nullptr;
    const bool flushed = std::fflush(output) == 0;
    const bool outputClosed = std::fclose(output) == 0;
    output = nullptr;
    if (!inputClosed || !flushed || !outputClosed ||
        !MoveFileExW(tempPath.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    std::reverse(rebuilt.begin(), rebuilt.end());
    items_ = std::move(rebuilt);
    diskBytes_ = offset;
    deadBytes_ = 0;
    return true;
}

bool ClipStore::compactIfNeeded() {
    if (deadBytes_ < kCompactionThreshold) return true;
    if (!rebuildFileRaw()) return false;
    ++revision_;
    return true;
}

bool ClipStore::remove(std::size_t index) {
    if (index >= items_.size()) return false;
    return removeItems(std::vector<ClipItem>{items_[index]});
}

bool ClipStore::toggleFavorite(std::size_t index) {
    if (index >= items_.size()) return false;
    const ClipItem& item = items_[index];
    const bool favorite = !item.favorite;
    const std::uint8_t flags = static_cast<std::uint8_t>(((favorite || item.pinned) ? 1u : 0u) |
        (item.encrypted ? 2u : 0u) |
        (item.headerSize > sizeof(DiskHeader) ? kMetadataFlag : 0u));
    const std::uint64_t flagsOffset = item.fileOffset + offsetof(DiskHeader, flags);
    if (flagsOffset < item.fileOffset ||
        flagsOffset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) return false;

    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"r+b");
    if (!file) return false;
    bool written = false;
    if (item.headerSize >= sizeof(DiskHeader) + sizeof(DiskMetadataV3)) {
        const std::uint64_t metadataOffset = item.fileOffset + sizeof(DiskHeader);
            DiskMetadataV3 metadata{kMetadataMagic, kMetadataVersion,
                                    static_cast<std::uint16_t>(sizeof(DiskMetadataV3)),
                                item.recordId, item.createdAt, item.lastCopiedAt, item.lastUsedAt,
                                item.useCount,
                                item.contentSize | (item.pinned ? kPinnedMetadataBit : 0) |
                                    (favorite ? kFavoriteMetadataBit : 0),
                                     item.copyCount, 0, {}};
            const std::string note = truncateUtf8(item.note, kMaxNoteBytes);
            metadata.noteSize = static_cast<std::uint32_t>(note.size());
            if (!note.empty()) std::memcpy(metadata.note, note.data(), note.size());
            metadata.crc = metadataCrc(&metadata, offsetof(DiskMetadataV3, crc));
        written = metadataOffset >= item.fileOffset &&
            metadataOffset <= static_cast<std::uint64_t>(std::numeric_limits<__int64>::max()) &&
            _fseeki64(file, static_cast<__int64>(metadataOffset), SEEK_SET) == 0 &&
            std::fwrite(&metadata, sizeof(metadata), 1, file) == 1 &&
            _fseeki64(file, static_cast<__int64>(flagsOffset), SEEK_SET) == 0 &&
            std::fwrite(&flags, sizeof(flags), 1, file) == 1;
    } else {
        written = _fseeki64(file, static_cast<__int64>(flagsOffset), SEEK_SET) == 0 &&
            std::fwrite(&flags, sizeof(flags), 1, file) == 1;
    }
    const bool flushed = written && std::fflush(file) == 0;
    const bool committed = flushed && _commit(_fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !committed || !closed) {
        if (written) rebuildFile();
        return false;
    }
    items_[index].favorite = favorite;
    ++revision_;
    return true;
}

bool ClipStore::togglePinned(std::size_t index) {
    if (index >= items_.size()) return false;
    const ClipItem backup = items_[index];
    items_[index].pinned = !backup.pinned;
    if (backup.headerSize < sizeof(DiskHeader) + sizeof(DiskMetadataV3)) {
        if (!rebuildFile()) {
            items_[index] = backup;
            return false;
        }
    } else {
        DiskMetadataV3 metadata{kMetadataMagic, kMetadataVersion,
                                static_cast<std::uint16_t>(sizeof(DiskMetadataV3)),
                                backup.recordId, backup.createdAt, backup.lastCopiedAt, backup.lastUsedAt,
                                backup.useCount,
                                backup.contentSize | (items_[index].pinned ? kPinnedMetadataBit : 0) |
                                    (items_[index].favorite ? kFavoriteMetadataBit : 0),
                                 backup.copyCount, 0, {}};
        const std::string note = truncateUtf8(backup.note, kMaxNoteBytes);
        metadata.noteSize = static_cast<std::uint32_t>(note.size());
        if (!note.empty()) std::memcpy(metadata.note, note.data(), note.size());
        metadata.crc = metadataCrc(&metadata, offsetof(DiskMetadataV3, crc));
        const std::uint64_t metadataOffset = backup.fileOffset + sizeof(DiskHeader);
        const std::uint64_t flagsOffset = backup.fileOffset + offsetof(DiskHeader, flags);
        if (metadataOffset < backup.fileOffset ||
            metadataOffset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max()) ||
            flagsOffset < backup.fileOffset ||
            flagsOffset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) {
            items_[index] = backup;
            return false;
        }
        const std::uint8_t flags = static_cast<std::uint8_t>(
            ((backup.favorite || items_[index].pinned) ? 1u : 0u) |
            (backup.encrypted ? 2u : 0u) | kMetadataFlag);
        std::FILE* file = nullptr;
        _wfopen_s(&file, path_.c_str(), L"r+b");
        if (!file) {
            items_[index] = backup;
            return false;
        }
        const bool written = _fseeki64(file, static_cast<__int64>(metadataOffset), SEEK_SET) == 0 &&
            std::fwrite(&metadata, sizeof(metadata), 1, file) == 1 &&
            _fseeki64(file, static_cast<__int64>(flagsOffset), SEEK_SET) == 0 &&
            std::fwrite(&flags, sizeof(flags), 1, file) == 1;
        const bool flushed = written && std::fflush(file) == 0;
        const bool committed = flushed && _commit(_fileno(file)) == 0;
        const bool closed = std::fclose(file) == 0;
        if (!written || !flushed || !committed || !closed) {
            items_[index] = backup;
            if (written) rebuildFile();
            return false;
        }
    }
    ++revision_;
    std::stable_sort(items_.begin(), items_.end(), [this](const ClipItem& first, const ClipItem& second) {
        return sortPinnedFirst(first, second, sortByLastUsed_);
    });
    return true;
}

bool ClipStore::setCategory(std::size_t index, std::uint32_t category) {
    if (index >= items_.size()) return false;
    const ClipItem& item = items_[index];
    const std::uint64_t categoryOffset = item.fileOffset + offsetof(DiskHeader, category);
    if (categoryOffset < item.fileOffset ||
        categoryOffset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) {
        return false;
    }
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"r+b");
    if (!file) return false;
    const bool written = _fseeki64(file, static_cast<__int64>(categoryOffset), SEEK_SET) == 0 &&
        std::fwrite(&category, sizeof(category), 1, file) == 1;
    const bool flushed = written && std::fflush(file) == 0;
    const bool committed = flushed && _commit(_fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !committed || !closed) return false;
    items_[index].category = category;
    ++revision_;
    return true;
}

bool ClipStore::setNote(std::size_t index, const std::string& note) {
    if (index >= items_.size() || note.find_first_of("\r\n") != std::string::npos ||
        note.size() > kMaxNoteBytes) return false;
    if (items_[index].note == note) return true;
    std::string payload;
    if (!readPayload(index, payload)) return false;
    ClipItem replacement = items_[index];
    replacement.note = note;
    return appendRecordReplacement(index, std::move(replacement), payload, items_.size());
}

bool ClipStore::rekey(bool enabled) {
    if (encryptionEnabled_ == enabled) return true;
    const std::wstring tempPath = path_ + L".tmp";
    std::FILE* out = nullptr;
    _wfopen_s(&out, tempPath.c_str(), L"wb");
    if (!out) return false;

    std::vector<ClipItem> rebuilt;
    rebuilt.reserve(items_.size());
    std::uint64_t offset = 0;
    for (std::size_t reverseIndex = items_.size(); reverseIndex > 0; --reverseIndex) {
        const std::size_t index = reverseIndex - 1;
        std::string plain;
        if (!readPayload(index, plain)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        ClipItem item = items_[index];
        item.encrypted = enabled;
        std::string stored;
        if (enabled && !protectPayload(plain, stored)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        if (!enabled) stored = std::move(plain);
        item.fileOffset = offset;
        item.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + sizeof(DiskMetadataV3));
        item.payloadSize = static_cast<std::uint32_t>(stored.size());
        item.payloadCrc = crc32(stored);
        if (!writeRecord(out, item, stored)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        offset += sizeof(DiskHeader) + sizeof(DiskMetadataV3) + item.source.size() + stored.size();
        rebuilt.push_back(std::move(item));
    }
    const bool flushed = std::fflush(out) == 0;
    const bool closed = std::fclose(out) == 0;
    if (!flushed || !closed) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    if (!MoveFileExW(tempPath.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    std::reverse(rebuilt.begin(), rebuilt.end());
    items_ = std::move(rebuilt);
    diskBytes_ = offset;
    encryptionEnabled_ = enabled;
    ++revision_;
    return true;
}

bool ClipStore::prune(std::size_t maxItems, std::uint64_t maxBytes, std::uint64_t minTimestamp) {
    if (items_.empty()) return true;
    std::vector<ClipItem> removed;
    removed.reserve(items_.size());
    std::size_t keptCount = 0;
    std::uint64_t bytes = 0;
    for (const ClipItem& item : items_) {
        const std::uint64_t recordSize = recordBytes(item);
        const bool underCount = maxItems == 0 || keptCount < maxItems;
        const bool underBytes = maxBytes == 0 || bytes + recordSize <= maxBytes;
        const bool recent = minTimestamp == 0 || item.timestamp >= minTimestamp;
        if (item.favorite || (protectPinnedFromAutoCleanup_ && item.pinned) ||
            (underCount && underBytes && recent)) {
            ++keptCount;
            bytes += recordSize;
        } else {
            removed.push_back(item);
        }
    }
    return removeItems(removed);
}

bool ClipStore::pruneCategory(ClipType type, std::size_t maxItems, std::uint64_t maxBytes) {
    if (items_.empty()) return true;
    std::vector<ClipItem> removed;
    removed.reserve(items_.size());
    std::size_t categoryCount = 0;
    std::uint64_t categoryBytes = 0;
    for (const ClipItem& item : items_) {
        if (!matchesType(type, item.type)) {
            continue;
        }
        const std::uint64_t recordSize = recordBytes(item);
        const bool underCount = maxItems == 0 || categoryCount < maxItems;
        const bool underBytes = maxBytes == 0 || categoryBytes + recordSize <= maxBytes;
        if (item.favorite || (protectPinnedFromAutoCleanup_ && item.pinned) ||
            (underCount && underBytes)) {
            ++categoryCount;
            categoryBytes += recordSize;
        } else {
            removed.push_back(item);
        }
    }
    return removeItems(removed);
}

bool ClipStore::clear() {
    const bool removed = DeleteFileW(path_.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    if (removed) {
        clearTransaction(path_);
        items_.clear();
        diskBytes_ = 0;
        deadBytes_ = 0;
        ++revision_;
    }
    return removed;
}

bool ClipStore::clearType(ClipType type) {
    std::vector<ClipItem> removed;
    for (const ClipItem& item : items_) {
        if (matchesType(type, item.type)) removed.push_back(item);
    }
    return removeItems(removed);
}

bool ClipStore::clearUnfavorited(bool protectPinned) {
    std::vector<ClipItem> removed;
    for (const ClipItem& item : items_) {
        if (!item.favorite && !(protectPinned && item.pinned)) removed.push_back(item);
    }
    return removeItems(removed);
}

bool ClipStore::clearTypeUnfavorited(ClipType type) {
    std::vector<ClipItem> removed;
    for (const ClipItem& item : items_) {
        if (!item.favorite && matchesType(type, item.type)) removed.push_back(item);
    }
    return removeItems(removed);
}

bool ClipStore::pruneExpired(std::uint64_t timestamp) {
    if (timestamp == 0 || items_.empty()) return true;
    std::vector<ClipItem> removed;
    for (const ClipItem& item : items_) {
        if (!item.favorite && !(protectPinnedFromAutoCleanup_ && item.pinned) &&
            item.expiresAt != 0 && item.expiresAt <= timestamp) {
            removed.push_back(item);
        }
    }
    return removeItems(removed);
}

std::size_t ClipStore::findHash(std::uint64_t hash) const {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].hash == hash) return i;
    }
    return items_.size();
}

std::size_t ClipStore::countType(ClipType type) const {
    std::size_t count = 0;
    for (const ClipItem& item : items_) {
        if (matchesType(type, item.type)) ++count;
    }
    return count;
}

std::uint64_t ClipStore::bytesType(ClipType type) const {
    std::uint64_t bytes = 0;
    for (const ClipItem& item : items_) {
        if (matchesType(type, item.type)) bytes += item.payloadSize;
    }
    return bytes;
}

std::vector<std::size_t> ClipStore::search(const std::string& query) const {
    return search(searchSnapshot(), query);
}

ClipSearchSnapshot ClipStore::searchSnapshot() const {
    return ClipSearchSnapshot{revision_, path_, items_};
}

std::vector<std::size_t> ClipStore::search(const ClipSearchSnapshot& snapshot,
                                           const std::string& query,
                                           const std::atomic<bool>* cancellation) {
    std::vector<std::size_t> result;
    const std::string needle = lowerAscii(query);
    std::FILE* file = nullptr;
    for (std::size_t i = 0; i < snapshot.items.size(); ++i) {
        if (cancellation && cancellation->load(std::memory_order_relaxed)) break;
        const ClipItem& item = snapshot.items[i];
        if (needle.empty() || containsIgnoreCase(item.note, needle) ||
            containsIgnoreCase(item.preview, needle) ||
            containsIgnoreCase(item.source, needle)) {
            result.push_back(i);
            if (result.size() >= 5000) break;
            continue;
        }
        if (item.type == ClipType::Image || item.type == ClipType::ImageV5) continue;
        if (!file) _wfopen_s(&file, snapshot.path.c_str(), L"rb");
        if (!file) continue;
        bool matches = false;
        if (item.encrypted || item.type == ClipType::Html) {
            std::string payload;
            if (readPayloadFromFile(file, item, payload)) {
                const std::string searchable = item.type == ClipType::Html
                    ? htmlTextPreview(payload) : payload;
                matches = containsIgnoreCase(searchable, needle);
            }
        } else {
            matches = streamContainsIgnoreCase(file, item, needle, cancellation);
        }
        if (matches) {
            result.push_back(i);
            if (result.size() >= 5000) break;
        }
    }
    if (file) std::fclose(file);
    return result;
}

std::string ClipStore::makePreview(ClipType type, const std::string& payload) {
    if (type == ClipType::Image || type == ClipType::ImageV5) return "[Image]";
    std::string value = type == ClipType::Html ? htmlTextPreview(payload) : payload;
    if (value.empty() && type == ClipType::Html) return "[HTML]";
    value = truncateUtf8(value, kPreviewMaxBytes);
    if (type == ClipType::Files) value = "[Files] " + value;
    return value;
}

bool ClipStore::containsIgnoreCase(const std::string& text, const std::string& query) {
    if (query.empty()) return true;
    if (text.size() < query.size()) return false;
    for (std::size_t start = 0; start <= text.size() - query.size(); ++start) {
        std::size_t offset = 0;
        while (offset < query.size() &&
               lowerAsciiChar(text[start + offset]) == lowerAsciiChar(query[offset])) {
            ++offset;
        }
        if (offset == query.size()) return true;
    }
    return false;
}
