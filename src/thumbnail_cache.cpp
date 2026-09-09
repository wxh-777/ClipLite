#include "thumbnail_cache.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kMagic = 0x314D4854; // THM1
constexpr std::uint16_t kVersion = 1;
constexpr std::uint16_t kEncryptedFlag = 1;
constexpr std::size_t kMaxThumbnailBytes = 2u * 1024u * 1024u;

#pragma pack(push, 1)
struct DiskHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t flags;
    std::uint64_t key;
    std::uint64_t lastAccess;
    std::uint32_t dataSize;
    std::uint32_t dataCrc;
};
#pragma pack(pop)

bool ensureDirectory(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::uint32_t crc32(const std::string& data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char value : data) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
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

bool protectData(const std::string& input, std::string& output) {
    DATA_BLOB source{static_cast<DWORD>(input.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
    DATA_BLOB protectedData{};
    if (!CryptProtectData(&source, L"ClipLite thumbnail", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &protectedData)) {
        return false;
    }
    output.assign(reinterpret_cast<const char*>(protectedData.pbData), protectedData.cbData);
    LocalFree(protectedData.pbData);
    return true;
}

bool unprotectData(const std::string& input, std::string& output) {
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

} // namespace

bool ThumbnailCache::setDirectory(const std::wstring& directory) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (directory.empty() || !ensureDirectory(directory)) return false;
    directory_ = directory;
    path_ = directory_ + L"\\thumbnails.bin";
    entries_.clear();
    diskBytes_ = 0;
    return loadIndex();
}

bool ThumbnailCache::moveDirectory(const std::wstring& from, const std::wstring& to) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (from.empty() || to.empty()) return false;
    if (_wcsicmp(from.c_str(), to.c_str()) == 0) return setDirectory(to);
    const std::wstring newPath = to + L"\\thumbnails.bin";
    if (!ensureDirectory(to)) return false;
    const std::wstring oldPath = from + L"\\thumbnails.bin";
    if (GetFileAttributesW(oldPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        MoveFileExW(oldPath.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    return setDirectory(to);
}

bool ThumbnailCache::loadIndex() const {
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"rb");
    if (!file) return true;
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const std::uint64_t fileSize = static_cast<std::uint64_t>(_ftelli64(file));
    _fseeki64(file, 0, SEEK_SET);
    std::uint64_t validBytes = 0;
    while (validBytes < fileSize) {
        DiskHeader header{};
        if (std::fread(&header, sizeof(header), 1, file) != 1 ||
            header.magic != kMagic || header.version != kVersion ||
            header.dataSize == 0 || header.dataSize > kMaxThumbnailBytes ||
            header.dataSize > fileSize - validBytes - sizeof(header)) {
            break;
        }
        std::string data(header.dataSize, '\0');
        if (std::fread(data.data(), 1, data.size(), file) != data.size() ||
            crc32(data) != header.dataCrc) {
            break;
        }
        entries_[header.key] = Entry{validBytes, header.dataSize, header.flags, header.lastAccess};
        validBytes += sizeof(header) + header.dataSize;
    }
    std::fclose(file);
    diskBytes_ = validBytes;
    if (validBytes != fileSize) {
        std::FILE* repair = nullptr;
        _wfopen_s(&repair, path_.c_str(), L"r+b");
        if (repair) {
            _chsize_s(_fileno(repair), static_cast<__int64>(validBytes));
            std::fclose(repair);
        }
    }
    return true;
}

bool ThumbnailCache::readStored(const Entry& entry, std::string& data) const {
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"rb");
    if (!file) return false;
    const bool positioned = _fseeki64(file, static_cast<__int64>(entry.offset), SEEK_SET) == 0;
    DiskHeader header{};
    const bool headerRead = positioned && std::fread(&header, sizeof(header), 1, file) == 1 &&
        header.magic == kMagic && header.version == kVersion &&
        header.dataSize == entry.storedSize;
    const bool dataPositioned = headerRead &&
        _fseeki64(file, static_cast<__int64>(entry.offset + sizeof(DiskHeader)), SEEK_SET) == 0;
    std::string stored(entry.storedSize, '\0');
    const bool readOk = dataPositioned && std::fread(stored.data(), 1, stored.size(), file) == stored.size() &&
        crc32(stored) == header.dataCrc;
    std::fclose(file);
    if (!readOk) return false;
    return (entry.flags & kEncryptedFlag) != 0
        ? unprotectData(stored, data) : (data = std::move(stored), true);
}

bool ThumbnailCache::read(std::uint64_t key, bool encrypted, std::string& data) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    (void)encrypted;
    const auto found = entries_.find(key);
    if (found == entries_.end() || !readStored(found->second, data)) return false;
    auto& entry = entries_[key];
    entry.lastAccess = nowUnixMillis();
    return true;
}

bool ThumbnailCache::write(std::uint64_t key, bool encrypted, const std::string& data) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (directory_.empty() || data.empty() || data.size() > kMaxThumbnailBytes) return false;
    std::string stored;
    if (encrypted && !protectData(data, stored)) return false;
    if (!encrypted) stored = data;
    DiskHeader header{kMagic, kVersion, static_cast<std::uint16_t>(encrypted ? kEncryptedFlag : 0),
                      key, nowUnixMillis(), static_cast<std::uint32_t>(stored.size()), crc32(stored)};
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"ab");
    if (!file) return false;
    const std::uint64_t offset = diskBytes_;
    const bool written = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
        std::fwrite(stored.data(), 1, stored.size(), file) == stored.size();
    const bool flushed = written && std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !flushed || !closed) return false;
    entries_[key] = Entry{offset, header.dataSize, header.flags, header.lastAccess};
    diskBytes_ += sizeof(header) + header.dataSize;
    return true;
}

bool ThumbnailCache::compact() const {
    const std::wstring tempPath = path_ + L".tmp";
    std::FILE* out = nullptr;
    _wfopen_s(&out, tempPath.c_str(), L"wb");
    if (!out) return false;
    std::unordered_map<std::uint64_t, Entry> rebuilt;
    std::uint64_t offset = 0;
    for (const auto& pair : entries_) {
        std::string stored;
        if (!readStored(pair.second, stored)) continue;
        DiskHeader header{kMagic, kVersion, pair.second.flags, pair.first, pair.second.lastAccess,
                          static_cast<std::uint32_t>(stored.size()), crc32(stored)};
        if (std::fwrite(&header, sizeof(header), 1, out) != 1 ||
            std::fwrite(stored.data(), 1, stored.size(), out) != stored.size()) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        rebuilt[pair.first] = Entry{offset, header.dataSize, header.flags, header.lastAccess};
        offset += sizeof(header) + header.dataSize;
    }
    const bool flushed = std::fflush(out) == 0;
    const bool closed = std::fclose(out) == 0;
    if (!flushed || !closed ||
        !MoveFileExW(tempPath.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tempPath.c_str());
        return false;
    }
    entries_ = std::move(rebuilt);
    diskBytes_ = offset;
    return true;
}

void ThumbnailCache::prune(std::uint64_t maxBytes, std::size_t maxItems) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::uint64_t totalBytes = 0;
    for (const auto& pair : entries_) totalBytes += sizeof(DiskHeader) + pair.second.storedSize;
    if (entries_.size() <= maxItems && totalBytes <= maxBytes && diskBytes_ <= maxBytes * 2) return;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> order;
    order.reserve(entries_.size());
    for (const auto& pair : entries_) order.emplace_back(pair.first, pair.second.lastAccess);
    std::sort(order.begin(), order.end(), [](const auto& first, const auto& second) {
        return first.second < second.second;
    });
    while (!order.empty() && (entries_.size() > maxItems || totalBytes > maxBytes)) {
        const std::uint64_t key = order.front().first;
        order.erase(order.begin());
        const auto found = entries_.find(key);
        if (found == entries_.end()) continue;
        totalBytes -= std::min<std::uint64_t>(totalBytes,
                                              sizeof(DiskHeader) + found->second.storedSize);
        entries_.erase(found);
    }
    compact();
}

void ThumbnailCache::clear() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    entries_.clear();
    diskBytes_ = 0;
    DeleteFileW(path_.c_str());
    DeleteFileW((path_ + L".tmp").c_str());
}
