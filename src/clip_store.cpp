#include "clip_store.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

constexpr std::uint32_t kMagic = 0x314C4343; // CCL1
constexpr std::uint16_t kVersion = 1;
constexpr std::uint32_t kMaxPayload = 32u * 1024u * 1024u;

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
};
#pragma pack(pop)

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

    while (true) {
        const auto offset = static_cast<std::uint64_t>(_ftelli64(file));
        DiskHeader header{};
        if (std::fread(&header, sizeof(header), 1, file) != 1) break;
        if (header.magic != kMagic || header.version != kVersion ||
            header.payloadSize > kMaxPayload || header.type < 1 || header.type > 3) {
            break;
        }

        ClipItem item;
        item.type = static_cast<ClipType>(header.type);
        item.timestamp = header.timestamp;
        item.hash = header.hash;
        item.category = header.category;
        item.pinned = (header.flags & 1) != 0;
        item.fileOffset = offset;
        item.payloadSize = header.payloadSize;

        std::string preview;
        if (header.type == static_cast<std::uint8_t>(ClipType::Image)) {
            preview = "[Image]";
            if (header.payloadSize > 0 &&
                _fseeki64(file, static_cast<__int64>(header.payloadSize), SEEK_CUR) != 0) break;
        } else {
            const std::size_t previewSize = std::min<std::uint32_t>(header.payloadSize, 160);
            preview.resize(previewSize);
            if (previewSize && std::fread(preview.data(), 1, previewSize, file) != previewSize) break;
            if (header.payloadSize > previewSize &&
                _fseeki64(file, static_cast<__int64>(header.payloadSize - previewSize), SEEK_CUR) != 0) break;
        }
        if (header.type == static_cast<std::uint8_t>(ClipType::Files)) preview = "[Files] " + preview;
        item.preview = std::move(preview);
        items_.push_back(std::move(item));
        if (items_.size() > maxItems_ * 2) {
            items_.erase(items_.begin(), items_.begin() + (items_.size() - maxItems_));
        }
    }
    diskBytes_ = static_cast<std::uint64_t>(_ftelli64(file));
    std::fclose(file);
    std::reverse(items_.begin(), items_.end());
    return rebuildFile();
}

bool ClipStore::writeRecord(std::FILE* file, const ClipItem& item, const std::string& payload) const {
    DiskHeader header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.type = static_cast<std::uint8_t>(item.type);
    header.flags = item.pinned ? 1 : 0;
    header.timestamp = item.timestamp;
    header.hash = item.hash;
    header.category = item.category;
    header.payloadSize = static_cast<std::uint32_t>(payload.size());
    return std::fwrite(&header, sizeof(header), 1, file) == 1 &&
           (payload.empty() || std::fwrite(payload.data(), 1, payload.size(), file) == payload.size());
}

bool ClipStore::append(ClipType type, const std::string& payload, std::uint64_t hash) {
    if (payload.empty() || payload.size() > kMaxPayload || findHash(hash) != items_.size()) return false;
    std::FILE* file = nullptr;
    _wfopen_s(&file, path_.c_str(), L"ab");
    if (!file) return false;

    ClipItem item;
    item.type = type;
    item.timestamp = nowUnix();
    item.hash = hash;
    item.payloadSize = static_cast<std::uint32_t>(payload.size());
    item.fileOffset = static_cast<std::uint64_t>(_ftelli64(file));
    item.preview = makePreview(type, payload);
    const bool ok = writeRecord(file, item, payload);
    std::fflush(file);
    std::fclose(file);
    if (!ok) return false;

    items_.insert(items_.begin(), std::move(item));
    diskBytes_ += sizeof(DiskHeader) + payload.size();
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
    if (!file || _fseeki64(file, static_cast<__int64>(item.fileOffset + sizeof(DiskHeader)), SEEK_SET) != 0) {
        if (file) std::fclose(file);
        return false;
    }
    payload.resize(item.payloadSize);
    const bool ok = std::fread(payload.data(), 1, payload.size(), file) == payload.size();
    std::fclose(file);
    return ok;
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
        if (!writeRecord(out, item, payload)) {
            std::fclose(out);
            DeleteFileW(tempPath.c_str());
            return false;
        }
        offset += sizeof(DiskHeader) + payload.size();
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
            continue;
        }
        if (items_[i].type == ClipType::Image) continue;
        std::string payload;
        if (readPayload(i, payload) && containsIgnoreCase(payload, needle)) result.push_back(i);
    }
    return result;
}

std::string ClipStore::makePreview(ClipType type, const std::string& payload) {
    if (type == ClipType::Image) return "[Image]";
    std::string value = payload.substr(0, 160);
    if (type == ClipType::Files) value = "[Files] " + value;
    for (char& c : value) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    return value;
}

bool ClipStore::containsIgnoreCase(const std::string& text, const std::string& query) {
    return lowerAscii(text).find(query) != std::string::npos;
}
