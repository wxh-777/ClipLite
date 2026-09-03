#ifndef CLIPLITE_CLIP_STORE_H
#define CLIPLITE_CLIP_STORE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

enum class ClipType : std::uint8_t {
    Text = 1,
    Files = 2,
    Image = 3,
    ImageV5 = 4,
    Html = 5,
};

struct ClipItem {
    ClipType type = ClipType::Text;
    std::uint64_t recordId = 0;
    std::uint64_t timestamp = 0;
    std::uint64_t lastCopiedAt = 0;
    std::uint64_t hash = 0;
    std::uint32_t category = 0;
    bool pinned = false;
    std::uint64_t fileOffset = 0;
    std::uint32_t headerSize = 0;
    std::uint32_t payloadSize = 0;
    std::uint32_t payloadCrc = 0;
    bool encrypted = false;
    std::string source;
    std::uint64_t expiresAt = 0;
    std::uint64_t createdAt = 0;
    std::uint64_t lastUsedAt = 0;
    std::uint64_t useCount = 0;
    std::uint64_t copyCount = 0;
    std::uint64_t contentSize = 0;
    std::string preview;
};

struct ClipSearchSnapshot {
    std::uint64_t revision = 0;
    std::wstring path;
    std::vector<ClipItem> items;
};

class ClipStore {
public:
    explicit ClipStore(std::size_t maxItems = 1000);

    void setMaxItems(std::size_t maxItems) { maxItems_ = maxItems; }
    bool setDataDirectory(const std::wstring& directory);
    bool setSortByLastUsed(bool enabled);
    void setEncryption(bool enabled) { encryptionEnabled_ = enabled; }
    bool encryptionEnabled() const { return encryptionEnabled_; }
    void setMaxPayloadBytes(std::uint32_t bytes) { maxPayloadBytes_ = bytes; }
    bool rekey(bool enabled);
    bool open();
    bool append(ClipType type, const std::string& payload, std::uint64_t hash,
                const std::string& source = {}, std::uint64_t expiresAt = 0);
    bool appendOrUpdate(ClipType type, const std::string& payload, std::uint64_t hash,
                        const std::string& source = {}, std::uint64_t expiresAt = 0);
    bool recordUse(std::size_t index, bool promote);
    bool readPayload(std::size_t index, std::string& payload) const;
    bool remove(std::size_t index);
    bool togglePinned(std::size_t index);
    bool setCategory(std::size_t index, std::uint32_t category);
    bool prune(std::size_t maxItems, std::uint64_t maxBytes, std::uint64_t minTimestamp);
    bool pruneCategory(ClipType type, std::size_t maxItems, std::uint64_t maxBytes);
    bool clear();
    bool clearType(ClipType type);
    bool pruneExpired(std::uint64_t timestamp);

    std::vector<std::size_t> search(const std::string& query) const;
    ClipSearchSnapshot searchSnapshot() const;
    std::uint64_t revision() const { return revision_; }
    static std::vector<std::size_t> search(const ClipSearchSnapshot& snapshot,
                                           const std::string& query,
                                           const std::atomic<bool>* cancellation = nullptr);
    std::size_t findHash(std::uint64_t hash) const;
    const std::vector<ClipItem>& items() const { return items_; }
    std::size_t activeCount() const { return items_.size(); }
    std::size_t countType(ClipType type) const;
    std::uint64_t bytesType(ClipType type) const;
    std::uint64_t diskBytes() const { return diskBytes_; }
    const std::wstring& path() const { return path_; }

private:
    bool rebuildFile();
    bool writeRecord(std::FILE* file, const ClipItem& item, const std::string& payload) const;
    static std::string makePreview(ClipType type, const std::string& payload);
    static bool containsIgnoreCase(const std::string& text, const std::string& query);

    std::wstring path_;
    std::size_t maxItems_;
    bool encryptionEnabled_ = false;
    std::uint32_t maxPayloadBytes_ = 32u * 1024u * 1024u;
    std::uint64_t diskBytes_ = 0;
    std::uint64_t revision_ = 0;
    bool sortByLastUsed_ = false;
    std::uint64_t nextRecordId_ = 1;
    std::vector<ClipItem> items_;
};

std::wstring clipLiteDataDirectory();
std::uint64_t clipLiteHash(const std::string& data);

#endif
