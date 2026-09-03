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
constexpr std::uint8_t kMetadataFlag = 4;
constexpr std::uint32_t kMetadataMagic = 0x31444D43; // CMD1
constexpr std::uint16_t kMetadataVersion = 1;
constexpr std::uint32_t kMaxMetadataSize = 4096;
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
#pragma pack(pop)

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

std::uint64_t nowUnixMillis() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    constexpr std::uint64_t kUnixEpoch = 116444736000000000ULL;
    return value.QuadPart < kUnixEpoch ? 0 : (value.QuadPart - kUnixEpoch) / 10000ULL;
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

bool writeRecordHeader(std::FILE* file, const ClipItem& item) {
    if (!file) return false;
    DiskHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.type = static_cast<std::uint8_t>(item.type);
    header.flags = static_cast<std::uint8_t>((item.pinned ? 1 : 0) |
                                             (item.encrypted ? 2 : 0) | kMetadataFlag);
    header.timestamp = item.timestamp;
    header.hash = item.hash;
    header.category = item.category;
    header.payloadSize = item.payloadSize;
    header.payloadCrc = item.payloadCrc;
    header.sourceSize = static_cast<std::uint32_t>(item.source.size());
    header.sourceCrc = crc32(item.source);
    header.expiresAt = item.expiresAt;
    DiskMetadataV1 metadata{kMetadataMagic, kMetadataVersion,
                            static_cast<std::uint16_t>(sizeof(DiskMetadataV1)),
                            item.recordId, item.createdAt, item.lastCopiedAt, item.lastUsedAt,
                            item.useCount, item.contentSize, item.copyCount, 0};
    metadata.crc = metadataCrc(&metadata, offsetof(DiskMetadataV1, crc));
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
        if (lead <= 0x7F) sequenceSize = 1;
        else if (lead >= 0xC2 && lead <= 0xDF) sequenceSize = 2;
        else if (lead >= 0xE0 && lead <= 0xEF) sequenceSize = 3;
        else if (lead >= 0xF0 && lead <= 0xF4) sequenceSize = 4;
        else return value.substr(0, limit);

        if (sequenceSize > limit - offset) break;
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
        if (magic == kStoredHtmlMagic && textSize <= contentSize) {
            const std::string text(payload.data() + kStoredHtmlHeaderSize, textSize);
            if (!text.empty()) return text;
            const std::uint32_t htmlSize = static_cast<std::uint32_t>(contentSize - textSize);
            html.assign(payload.data() + kStoredHtmlHeaderSize + textSize, htmlSize);
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
    ++revision_;
    DeleteFileW((path_ + L".tmp").c_str());
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
            header.type < 1 || header.type > 5 || (header.flags & ~0x07u) != 0) {
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
                if (version == kMetadataVersion && size == sizeof(DiskMetadataV1)) {
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
        item.pinned = (header.flags & 1) != 0;
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
        items_.push_back(std::move(item));
        validBytes = payloadOffset + header.payloadSize;
        if (maxItems_ > 0 && items_.size() > maxItems_ * 2) {
            items_.erase(items_.begin(), items_.begin() + (items_.size() - maxItems_));
        }
    }
    diskBytes_ = validBytes;
    std::fclose(file);
    nextRecordId_ = maximumRecordId == std::numeric_limits<std::uint64_t>::max()
        ? maximumRecordId : maximumRecordId + 1;
    std::reverse(items_.begin(), items_.end());
    std::stable_sort(items_.begin(), items_.end(), [this](const ClipItem& first, const ClipItem& second) {
        return sortByLastUsed_ ? sortByActivity(first, second) : sortByCopyTime(first, second);
    });
    for (ClipItem& item : items_) {
        if (item.recordId == 0) {
            item.recordId = nextRecordId_ == 0 ? 1 : nextRecordId_;
            if (nextRecordId_ != std::numeric_limits<std::uint64_t>::max()) ++nextRecordId_;
        }
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
        return sortByLastUsed_ ? sortByActivity(first, second) : sortByCopyTime(first, second);
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
    if (payload.empty() || payload.size() > maxPayloadBytes_ || findHash(hash) != items_.size()) return false;
    const std::vector<ClipItem> backup = items_;
    const std::uint64_t oldDiskBytes = diskBytes_;
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
    item.lastCopiedAt = nowUnixMillis();
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
    item.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + sizeof(DiskMetadataV1));
    item.preview = makePreview(type, payload);
    const std::uint64_t recordSize = sizeof(DiskHeader) + sizeof(DiskMetadataV1) +
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

    items_.insert(items_.begin(), std::move(item));
    diskBytes_ += recordSize;
    const bool needsRebuild = maxItems_ > 0 && items_.size() > maxItems_;
    while (maxItems_ > 0 && items_.size() > maxItems_) items_.pop_back();
    if (needsRebuild && !rebuildFile()) {
        items_ = backup;
        diskBytes_ = oldDiskBytes;
        std::FILE* rollback = nullptr;
        _wfopen_s(&rollback, path_.c_str(), L"r+b");
        if (rollback) {
            _chsize_s(_fileno(rollback), static_cast<__int64>(item.fileOffset));
            std::fclose(rollback);
        }
        return false;
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
    if (item.headerSize >= sizeof(DiskHeader) + sizeof(DiskMetadataV1)) {
        const std::uint64_t metadataOffset = item.fileOffset + sizeof(DiskHeader);
        if (metadataOffset >= item.fileOffset &&
            metadataOffset <= static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) {
            DiskMetadataV1 metadata{kMetadataMagic, kMetadataVersion,
                                    static_cast<std::uint16_t>(sizeof(DiskMetadataV1)),
                                    item.recordId, item.createdAt, item.lastCopiedAt, item.lastUsedAt,
                                    item.useCount, item.contentSize, item.copyCount, 0};
            metadata.crc = metadataCrc(&metadata, offsetof(DiskMetadataV1, crc));
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
        ClipItem used = std::move(items_[index]);
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        items_.insert(items_.begin(), std::move(used));
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
    std::string existingPayload;
    if (backup[existing].type == type &&
        backup[existing].headerSize == sizeof(DiskHeader) + sizeof(DiskMetadataV1) &&
        (source.empty() || source.size() == backup[existing].source.size()) &&
        readPayload(existing, existingPayload) && existingPayload == payload) {
        ClipItem item = backup[existing];
        item.timestamp = nowUnix();
        item.lastCopiedAt = nowUnixMillis();
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
                items_[existing] = std::move(item);
                std::stable_sort(items_.begin(), items_.end(), [this](const ClipItem& first,
                                                                       const ClipItem& second) {
                    return sortByLastUsed_ ? sortByActivity(first, second) :
                                             sortByCopyTime(first, second);
                });
                ++revision_;
                return true;
            }
        }
    }

    std::vector<std::size_t> order;
    order.reserve(items_.size());
    for (std::size_t reverseIndex = items_.size(); reverseIndex > 0; --reverseIndex) {
        const std::size_t index = reverseIndex - 1;
        if (index != existing) order.push_back(index);
    }
    order.push_back(existing);

    const bool keepExistingRichText = backup[existing].type == ClipType::Html &&
                                      type == ClipType::Text;
    const std::wstring tempPath = path_ + L".tmp";
    std::FILE* out = nullptr;
    _wfopen_s(&out, tempPath.c_str(), L"wb");
    if (!out) return false;

    std::vector<ClipItem> rebuilt;
    rebuilt.reserve(order.size());
    std::uint64_t offset = 0;
    bool success = true;
    for (std::size_t position = 0; position < order.size(); ++position) {
        const std::size_t oldIndex = order[position];
        const bool promoted = position + 1 == order.size();
        ClipItem item = backup[oldIndex];
        std::string currentPayload;
        if (promoted && !keepExistingRichText) {
            item.type = type;
            item.hash = hash;
            item.preview = makePreview(type, payload);
            item.contentSize = payload.size();
            currentPayload = payload;
        } else if (!readPayload(oldIndex, currentPayload)) {
            success = false;
            break;
        }
        item.timestamp = promoted ? nowUnix() : item.timestamp;
        if (promoted) {
            item.hash = hash;
            item.lastCopiedAt = nowUnixMillis();
            if (item.copyCount != std::numeric_limits<std::uint64_t>::max()) ++item.copyCount;
            if (!source.empty()) item.source = source.substr(0, kMaxSource);
            item.expiresAt = expiresAt;
            if (keepExistingRichText) item.preview = makePreview(item.type, currentPayload);
        }
        std::string storedPayload = currentPayload;
        if (item.encrypted && !protectPayload(currentPayload, storedPayload)) {
            success = false;
            break;
        }
        if (storedPayload.empty() || storedPayload.size() > kMaxPayload) {
            success = false;
            break;
        }
        item.fileOffset = offset;
        item.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + sizeof(DiskMetadataV1));
        item.payloadSize = static_cast<std::uint32_t>(storedPayload.size());
        item.payloadCrc = crc32(storedPayload);
        if (!writeRecord(out, item, storedPayload)) {
            success = false;
            break;
        }
        offset += sizeof(DiskHeader) + sizeof(DiskMetadataV1) + item.source.size() + storedPayload.size();
        rebuilt.push_back(std::move(item));
    }
    success = success && rebuilt.size() == order.size() && std::fflush(out) == 0;
    success = std::fclose(out) == 0 && success;
    if (!success || !MoveFileExW(tempPath.c_str(), path_.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    std::reverse(rebuilt.begin(), rebuilt.end());
    items_ = std::move(rebuilt);
    diskBytes_ = offset;
    ++revision_;
    return true;
}

bool ClipStore::readPayload(std::size_t index, std::string& payload) const {
    if (index >= items_.size()) return false;
    const ClipItem& item = items_[index];
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"rb");
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
        item.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + sizeof(DiskMetadataV1));
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
        offset += sizeof(DiskHeader) + sizeof(DiskMetadataV1) + item.source.size() + storedPayload.size();
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
    return true;
}

bool ClipStore::remove(std::size_t index) {
    if (index >= items_.size()) return false;
    const std::vector<ClipItem> backup = items_;
    items_.erase(items_.begin() + index);
    if (rebuildFile()) {
        ++revision_;
        return true;
    }
    items_ = backup;
    return false;
}

bool ClipStore::togglePinned(std::size_t index) {
    if (index >= items_.size()) return false;
    const ClipItem& item = items_[index];
    const std::uint64_t flagsOffset = item.fileOffset + offsetof(DiskHeader, flags);
    if (flagsOffset < item.fileOffset ||
        flagsOffset > static_cast<std::uint64_t>(std::numeric_limits<__int64>::max())) {
        return false;
    }

    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"r+b");
    if (!file) return false;

    const bool pinned = !item.pinned;
    const std::uint8_t flags = static_cast<std::uint8_t>((pinned ? 1u : 0u) |
                                                          (item.encrypted ? 2u : 0u) |
                                                          (item.headerSize > sizeof(DiskHeader)
                                                               ? kMetadataFlag : 0u));
    const bool written = _fseeki64(file, static_cast<__int64>(flagsOffset), SEEK_SET) == 0 &&
        std::fwrite(&flags, sizeof(flags), 1, file) == 1;
    const bool flushed = written && std::fflush(file) == 0;
    const bool committed = flushed && _commit(_fileno(file)) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !committed || !closed) {
        if (written) rebuildFile(); // Restore the original flag if the direct write was only partial.
        return false;
    }

    items_[index].pinned = pinned;
    ++revision_;
    return true;
}

bool ClipStore::setCategory(std::size_t index, std::uint32_t category) {
    if (index >= items_.size()) return false;
    const std::vector<ClipItem> backup = items_;
    items_[index].category = category;
    if (rebuildFile()) {
        ++revision_;
        return true;
    }
    items_ = backup;
    return false;
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
        item.headerSize = static_cast<std::uint32_t>(sizeof(DiskHeader) + sizeof(DiskMetadataV1));
        item.payloadSize = static_cast<std::uint32_t>(stored.size());
        item.payloadCrc = crc32(stored);
        if (!writeRecord(out, item, stored)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        offset += sizeof(DiskHeader) + sizeof(DiskMetadataV1) + item.source.size() + stored.size();
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
    const std::vector<ClipItem> backup = items_;
    std::vector<ClipItem> kept;
    kept.reserve(items_.size());
    std::uint64_t bytes = 0;
    for (const ClipItem& item : items_) {
        const std::uint64_t recordSize = recordBytes(item);
        const bool underCount = maxItems == 0 || kept.size() < maxItems;
        const bool underBytes = maxBytes == 0 || bytes + recordSize <= maxBytes;
        const bool recent = minTimestamp == 0 || item.timestamp >= minTimestamp;
        if (item.pinned || (underCount && underBytes && recent)) {
            kept.push_back(item);
            bytes += recordSize;
        }
    }
    if (kept.size() == items_.size()) return true;
    items_ = std::move(kept);
    if (rebuildFile()) {
        ++revision_;
        return true;
    }
    items_ = backup;
    return false;
}

bool ClipStore::pruneCategory(ClipType type, std::size_t maxItems, std::uint64_t maxBytes) {
    if (items_.empty()) return true;
    const std::vector<ClipItem> backup = items_;
    std::vector<ClipItem> kept;
    kept.reserve(items_.size());
    std::size_t categoryCount = 0;
    std::uint64_t categoryBytes = 0;
    for (const ClipItem& item : items_) {
        if (!matchesType(type, item.type)) {
            kept.push_back(item);
            continue;
        }
        const std::uint64_t recordSize = recordBytes(item);
        const bool underCount = maxItems == 0 || categoryCount < maxItems;
        const bool underBytes = maxBytes == 0 || categoryBytes + recordSize <= maxBytes;
        if (item.pinned || (underCount && underBytes)) {
            kept.push_back(item);
            ++categoryCount;
            categoryBytes += recordSize;
        }
    }
    if (kept.size() == items_.size()) return true;
    items_ = std::move(kept);
    if (rebuildFile()) {
        ++revision_;
        return true;
    }
    items_ = backup;
    return false;
}

bool ClipStore::clear() {
    const bool removed = DeleteFileW(path_.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
    if (removed) {
        items_.clear();
        diskBytes_ = 0;
        ++revision_;
    }
    return removed;
}

bool ClipStore::clearType(ClipType type) {
    const auto oldSize = items_.size();
    const std::vector<ClipItem> backup = items_;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [type](const ClipItem& item) {
                                    return matchesType(type, item.type);
    }), items_.end());
    if (items_.size() == oldSize) return true;
    if (rebuildFile()) {
        ++revision_;
        return true;
    }
    items_ = backup;
    return false;
}

bool ClipStore::pruneExpired(std::uint64_t timestamp) {
    if (timestamp == 0 || items_.empty()) return true;
    const std::vector<ClipItem> backup = items_;
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [timestamp](const ClipItem& item) {
                                    return item.expiresAt != 0 && item.expiresAt <= timestamp;
    }), items_.end());
    if (items_.size() == backup.size()) return true;
    if (rebuildFile()) {
        ++revision_;
        return true;
    }
    items_ = backup;
    return false;
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
        if (needle.empty() || containsIgnoreCase(item.preview, needle) ||
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
