#ifndef CLIPLITE_THUMBNAIL_CACHE_H
#define CLIPLITE_THUMBNAIL_CACHE_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class ThumbnailCache {
public:
    bool setDirectory(const std::wstring& directory);
    bool moveDirectory(const std::wstring& from, const std::wstring& to);
    bool read(std::uint64_t key, bool encrypted, std::string& data) const;
    bool write(std::uint64_t key, bool encrypted, const std::string& data) const;
    void prune(std::uint64_t maxBytes, std::size_t maxItems) const;
    void clear() const;

private:
    struct Entry {
        std::uint64_t offset = 0;
        std::uint32_t storedSize = 0;
        std::uint16_t flags = 0;
        std::uint64_t lastAccess = 0;
    };

    bool loadIndex() const;
    bool compact() const;
    bool readStored(const Entry& entry, std::string& data) const;
    std::wstring directory_;
    std::wstring path_;
    mutable std::unordered_map<std::uint64_t, Entry> entries_;
    mutable std::uint64_t diskBytes_ = 0;
    mutable std::recursive_mutex mutex_;
};

#endif
