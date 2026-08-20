#include "clip_store.h"

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

constexpr std::uint32_t kMagic = 0x314C4343; // CCL1
constexpr std::uint16_t kLegacyVersion = 1;
constexpr std::uint16_t kVersion = 2;
constexpr std::uint32_t kMaxPayload = 32u * 1024u * 1024u;

#pragma pack(push, 1)
struct LegacyDiskHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint8_t type;
    std::uint8_t flags;
    std::uint64_t timestamp;
    std::uint64_t hash;
    std::uint32_t category;
    std::uint32_t payloadSize;
};

struct DiskHeader {
    LegacyDiskHeader base;
    std::uint32_t payloadCrc;
};
#pragma pack(pop)

std::uint32_t crc32Update(std::uint32_t crc, const char* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<unsigned char>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

std::uint32_t crc32(const std::string& data) {
    return crc32Update(0xFFFFFFFFu, data.data(), data.size()) ^ 0xFFFFFFFFu;
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

std::string lowerAscii(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    return value;
}

} // namespace

struct ClipStore::DiskRecord {};

std::wstring clipLiteDataDirectory() {
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

bool ClipStore::open() {
    items_.clear();
    diskBytes_ = 0;
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"rb");
    if (!file) return true;

    _fseeki64(file, 0, SEEK_END);
    const auto fileSize = static_cast<std::uint64_t>(_ftelli64(file));
    _fseeki64(file, 0, SEEK_SET);
    std::uint64_t validBytes = 0;

    while (true) {
        const auto offset = static_cast<std::uint64_t>(_ftelli64(file));
        LegacyDiskHeader base{};
        if (std::fread(&base, sizeof(base), 1, file) != 1) break;
        if (base.magic != kMagic || (base.version != kLegacyVersion && base.version != kVersion) ||
            base.payloadSize > kMaxPayload || base.type < 1 || base.type > 5) {
            break;
        }

        std::uint32_t expectedCrc = 0;
        const bool hasChecksum = base.version == kVersion;
        if (hasChecksum && std::fread(&expectedCrc, sizeof(expectedCrc), 1, file) != 1) break;
        const auto payloadOffset = static_cast<std::uint64_t>(_ftelli64(file));
        if (payloadOffset > fileSize || base.payloadSize > fileSize - payloadOffset) break;

        ClipItem item;
        item.type = static_cast<ClipType>(base.type);
        item.timestamp = base.timestamp;
        item.hash = base.hash;
        item.category = base.category;
        item.pinned = (base.flags & 1) != 0;
        item.fileOffset = offset;
        item.payloadSize = base.payloadSize;
        item.payloadCrc = expectedCrc;
        item.hasChecksum = hasChecksum;
        item.encrypted = (base.flags & 2) != 0;

        std::string preview;
        const bool isImage = base.type == static_cast<std::uint8_t>(ClipType::Image) ||
                             base.type == static_cast<std::uint8_t>(ClipType::ImageV5);
        const std::size_t previewLimit = item.encrypted || isImage ? 0 : 160;
        std::uint32_t remaining = base.payloadSize;
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
        if (hasChecksum && (checksum ^ 0xFFFFFFFFu) != expectedCrc) break;
        if (item.encrypted) preview = "[Protected]";
        else if (isImage) preview = "[Image]";
        if (base.type == static_cast<std::uint8_t>(ClipType::Html)) preview = "[HTML]";
        if (base.type == static_cast<std::uint8_t>(ClipType::Files)) preview = "[Files] " + preview;
        item.preview = std::move(preview);
        items_.push_back(std::move(item));
        validBytes = payloadOffset + base.payloadSize;
        if (items_.size() > maxItems_ * 2) {
            items_.erase(items_.begin(), items_.begin() + (items_.size() - maxItems_));
        }
    }
    diskBytes_ = validBytes;
    std::fclose(file);
    std::reverse(items_.begin(), items_.end());
    return rebuildFile();
}

bool ClipStore::writeRecord(std::FILE* file, const ClipItem& item, const std::string& payload) const {
    DiskHeader header{};
    header.base.magic = kMagic;
    header.base.version = kVersion;
    header.base.type = static_cast<std::uint8_t>(item.type);
    header.base.flags = (item.pinned ? 1 : 0) | (item.encrypted ? 2 : 0);
    header.base.timestamp = item.timestamp;
    header.base.hash = item.hash;
    header.base.category = item.category;
    header.base.payloadSize = static_cast<std::uint32_t>(payload.size());
    header.payloadCrc = crc32(payload);
    return std::fwrite(&header, sizeof(header), 1, file) == 1 &&
           (payload.empty() || std::fwrite(payload.data(), 1, payload.size(), file) == payload.size());
}

bool ClipStore::append(ClipType type, const std::string& payload, std::uint64_t hash) {
    if (payload.empty() || payload.size() > kMaxPayload || findHash(hash) != items_.size()) return false;
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"ab");
    if (!file) return false;
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }

    ClipItem item;
    item.type = type;
    item.timestamp = nowUnix();
    item.hash = hash;
    item.encrypted = encryptionEnabled_;
    std::string storedPayload;
    if (item.encrypted && !protectPayload(payload, storedPayload)) {
        std::fclose(file);
        return false;
    }
    if (!item.encrypted) storedPayload = payload;
    item.payloadSize = static_cast<std::uint32_t>(storedPayload.size());
    item.payloadCrc = crc32(storedPayload);
    item.hasChecksum = true;
    item.fileOffset = static_cast<std::uint64_t>(_ftelli64(file));
    item.preview = makePreview(type, payload);
    const bool ok = writeRecord(file, item, storedPayload);
    std::fflush(file);
    std::fclose(file);
    if (!ok) return false;

    items_.insert(items_.begin(), std::move(item));
    diskBytes_ += sizeof(DiskHeader) + storedPayload.size();
    const bool needsRebuild = items_.size() > maxItems_;
    while (items_.size() > maxItems_) items_.pop_back();
    if (needsRebuild) rebuildFile();
    return true;
}

bool ClipStore::readPayload(std::size_t index, std::string& payload) const {
    if (index >= items_.size()) return false;
    const ClipItem& item = items_[index];
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"rb");
    const std::uint64_t headerSize = item.hasChecksum ? sizeof(DiskHeader) : sizeof(LegacyDiskHeader);
    if (!file || _fseeki64(file, static_cast<__int64>(item.fileOffset + headerSize), SEEK_SET) != 0) {
        if (file) std::fclose(file);
        return false;
    }
    payload.resize(item.payloadSize);
    const bool readOk = std::fread(payload.data(), 1, payload.size(), file) == payload.size();
    std::fclose(file);
    if (!readOk || (item.hasChecksum && crc32(payload) != item.payloadCrc)) return false;
    if (!item.encrypted) return true;
    std::string plain;
    if (!unprotectPayload(payload, plain)) return false;
    payload = std::move(plain);
    return true;
}

bool ClipStore::rebuildFile() {
    const std::wstring tempPath = path_ + L".tmp";
    std::FILE* out = nullptr;
    _wfopen_s(&out, tempPath.c_str(), L"wb");
    if (!out) return false;

    std::vector<ClipItem> rebuilt;
    rebuilt.reserve(items_.size());
    std::uint64_t offset = 0;
    for (const ClipItem& old : items_) {
        std::string payload;
        if (!readPayload(&old - items_.data(), payload)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        ClipItem item = old;
        item.fileOffset = offset;
        std::string storedPayload;
        if (item.encrypted && !protectPayload(payload, storedPayload)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        if (!item.encrypted) storedPayload = payload;
        item.payloadSize = static_cast<std::uint32_t>(storedPayload.size());
        item.payloadCrc = crc32(storedPayload);
        item.hasChecksum = true;
        if (!writeRecord(out, item, storedPayload)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        offset += sizeof(DiskHeader) + storedPayload.size();
        rebuilt.push_back(std::move(item));
    }
    std::fflush(out);
    std::fclose(out);
    if (!MoveFileExW(tempPath.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    items_ = std::move(rebuilt);
    diskBytes_ = offset;
    return true;
}

bool ClipStore::remove(std::size_t index) {
    if (index >= items_.size()) return false;
    items_.erase(items_.begin() + index);
    return rebuildFile();
}

bool ClipStore::togglePinned(std::size_t index) {
    if (index >= items_.size()) return false;
    items_[index].pinned = !items_[index].pinned;
    return rebuildFile();
}

bool ClipStore::setCategory(std::size_t index, std::uint32_t category) {
    if (index >= items_.size()) return false;
    items_[index].category = category;
    return rebuildFile();
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
    for (std::size_t index = 0; index < items_.size(); ++index) {
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
        item.payloadSize = static_cast<std::uint32_t>(stored.size());
        item.payloadCrc = crc32(stored);
        item.hasChecksum = true;
        if (!writeRecord(out, item, stored)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        offset += sizeof(DiskHeader) + stored.size();
        rebuilt.push_back(std::move(item));
    }
    std::fflush(out);
    std::fclose(out);
    if (!MoveFileExW(tempPath.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    items_ = std::move(rebuilt);
    diskBytes_ = offset;
    encryptionEnabled_ = enabled;
    return true;
}

bool ClipStore::prune(std::size_t maxItems, std::uint64_t maxBytes, std::uint64_t minTimestamp) {
    if (items_.empty()) return true;
    std::vector<ClipItem> kept;
    kept.reserve(items_.size());
    std::uint64_t bytes = 0;
    for (const ClipItem& item : items_) {
        const std::uint64_t recordBytes = sizeof(DiskHeader) + item.payloadSize;
        const bool underCount = maxItems == 0 || kept.size() < maxItems;
        const bool underBytes = maxBytes == 0 || bytes + recordBytes <= maxBytes;
        const bool recent = minTimestamp == 0 || item.timestamp >= minTimestamp;
        if (item.pinned || (underCount && underBytes && recent)) {
            kept.push_back(item);
            bytes += recordBytes;
        }
    }
    if (kept.size() == items_.size()) return true;
    items_ = std::move(kept);
    return rebuildFile();
}

bool ClipStore::clear() {
    items_.clear();
    diskBytes_ = 0;
    return DeleteFileW(path_.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND;
}

std::size_t ClipStore::findHash(std::uint64_t hash) const {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].hash == hash) return i;
    }
    return items_.size();
}

std::vector<std::size_t> ClipStore::search(const std::string& query) const {
    std::vector<std::size_t> result;
    const std::string needle = lowerAscii(query);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (needle.empty() || containsIgnoreCase(items_[i].preview, needle)) {
            result.push_back(i);
            if (result.size() >= 5000) break;
            continue;
        }
        if (items_[i].type == ClipType::Image || items_[i].type == ClipType::ImageV5) continue;
        std::string payload;
        if (readPayload(i, payload) && containsIgnoreCase(payload, needle)) {
            result.push_back(i);
            if (result.size() >= 5000) break;
        }
    }
    return result;
}

std::string ClipStore::makePreview(ClipType type, const std::string& payload) {
    if (type == ClipType::Image || type == ClipType::ImageV5) return "[Image]";
    if (type == ClipType::Html) return "[HTML]";
    std::string value = payload.substr(0, 160);
    if (type == ClipType::Files) value = "[Files] " + value;
    for (char& c : value) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    return value;
}

bool ClipStore::containsIgnoreCase(const std::string& text, const std::string& query) {
    return lowerAscii(text).find(query) != std::string::npos;
}
