#ifndef CLIPLITE_CLIP_STORE_H
#define CLIPLITE_CLIP_STORE_H

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
    std::uint64_t timestamp = 0;
    std::uint64_t hash = 0;
    std::uint32_t category = 0;
    bool pinned = false;
    std::uint64_t fileOffset = 0;
    std::uint32_t payloadSize = 0;
    std::uint32_t payloadCrc = 0;
    bool hasChecksum = false;
    bool encrypted = false;
    bool hasSource = false;
    bool hasExpiry = false;
    std::string source;
    std::uint64_t expiresAt = 0;
    std::string preview;
};

class ClipStore {
public:
    explicit ClipStore(std::size_t maxItems = 1000);

    void setMaxItems(std::size_t maxItems) { maxItems_ = maxItems; }
    void setEncryption(bool enabled) { encryptionEnabled_ = enabled; }
    bool encryptionEnabled() const { return encryptionEnabled_; }
    void setMaxPayloadBytes(std::uint32_t bytes) { maxPayloadBytes_ = bytes; }
    bool rekey(bool enabled);
    bool open();
    bool append(ClipType type, const std::string& payload, std::uint64_t hash,
                const std::string& source = {}, std::uint64_t expiresAt = 0);
    bool readPayload(std::size_t index, std::string& payload) const;
    bool remove(std::size_t index);
    bool togglePinned(std::size_t index);
    bool setCategory(std::size_t index, std::uint32_t category);
    bool prune(std::size_t maxItems, std::uint64_t maxBytes, std::uint64_t minTimestamp);
    bool clear();
    bool clearType(ClipType type);
    bool pruneExpired(std::uint64_t timestamp);

    std::vector<std::size_t> search(const std::string& query) const;
    std::size_t findHash(std::uint64_t hash) const;
    const std::vector<ClipItem>& items() const { return items_; }
    std::size_t activeCount() const { return items_.size(); }
    std::size_t countType(ClipType type) const;
    std::uint64_t bytesType(ClipType type) const;
    std::uint64_t diskBytes() const { return diskBytes_; }
    const std::wstring& path() const { return path_; }

private:
    struct DiskRecord;

    bool rebuildFile();
    bool loadRecord(std::uint64_t offset, ClipItem& item) const;
    bool writeRecord(std::FILE* file, const ClipItem& item, const std::string& payload) const;
    static std::string makePreview(ClipType type, const std::string& payload);
    static bool containsIgnoreCase(const std::string& text, const std::string& query);

    std::wstring path_;
    std::size_t maxItems_;
    bool encryptionEnabled_ = false;
    std::uint32_t maxPayloadBytes_ = 32u * 1024u * 1024u;
    std::uint64_t diskBytes_ = 0;
    std::vector<ClipItem> items_;
};

std::wstring clipLiteDataDirectory();
std::uint64_t clipLiteHash(const std::string& data);

#endif
